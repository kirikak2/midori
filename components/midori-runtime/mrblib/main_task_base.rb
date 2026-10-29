# BoardConfig is defined above (concatenated by CMake)

# ============================================================================
# Global MIDI Devices Module
# Provides pre-initialized MIDI devices based on board configuration
# SD card scripts should use these devices instead of creating their own
# ============================================================================

# Use global variables instead of class instance variables for PicoRuby compatibility
$midi_sam2695 = nil
$midi_usb_midi_host = nil
$midi_usb_midi_device = nil
$midi_sam2695_init_attempted = false
$midi_usb_midi_host_init_attempted = false
$midi_usb_midi_device_init_attempted = false

module MIDIDevices
  def self.sam2695
    # Lazy initialization
    if $midi_sam2695.nil? && !$midi_sam2695_init_attempted
      $midi_sam2695_init_attempted = true
      init_sam2695
    end
    $midi_sam2695
  end

  def self.usb_midi_host
    # Lazy initialization
    if $midi_usb_midi_host.nil? && !$midi_usb_midi_host_init_attempted
      $midi_usb_midi_host_init_attempted = true
      init_usb_midi_host
    end
    $midi_usb_midi_host
  end

  def self.usb_midi_device
    # Lazy initialization
    if $midi_usb_midi_device.nil? && !$midi_usb_midi_device_init_attempted
      $midi_usb_midi_device_init_attempted = true
      init_usb_midi_device
    end
    $midi_usb_midi_device
  end

  # AMY software synthesizer on the board's speaker (Tab5 / CrowPanel).
  # A MIDI transport like the others: MIDI::Device.new(MIDIDevices.amy).
  def self.amy
    # Lazy initialization
    if $midi_amy.nil? && !$midi_amy_init_attempted
      $midi_amy_init_attempted = true
      init_amy
    end
    $midi_amy
  end

  def self.init_sam2695
    if BoardConfig::HAS_SAM2695
      require 'sam2695'
      $midi_sam2695 = SAM2695.new(BoardConfig::SAM2695_TX_PIN, BoardConfig::SAM2695_RX_PIN)
      puts "SAM2695 initialized (TX=#{BoardConfig::SAM2695_TX_PIN}, RX=#{BoardConfig::SAM2695_RX_PIN})"
    else
      puts "SAM2695 not available on this board"
    end
  end

  def self.init_usb_midi_host
    if BoardConfig::HAS_USB_MIDI_HOST
      require 'usb_midi_host'
      $midi_usb_midi_host = USB_MIDI_HOST.instance
      puts "USB-MIDI Host initialized"
    else
      puts "USB-MIDI Host not available on this board"
    end
  end

  def self.init_usb_midi_device
    if BoardConfig::HAS_USB_MIDI_DEVICE
      require 'usb_midi_device'
      $midi_usb_midi_device = USB_MIDI_DEVICE.instance
      puts "USB-MIDI Device initialized"
    else
      puts "USB-MIDI Device not available on this board"
    end
  end

  def self.init_amy
    if BoardConfig::HAS_AMY
      require 'amy'
      $midi_amy = AMY::Transport.instance
      puts "AMY synthesizer initialized"
      # The MIDI Devices list had it on standby: patches saved to it route now
      UI._mididev_activate("amy", $midi_amy.transport_id)
    else
      puts "AMY not available on this board"
    end
  end

  def self.cleanup
    $midi_amy = nil
    $midi_amy_init_attempted = false
    $midi_sam2695 = nil
    $midi_usb_midi_host = nil
    $midi_usb_midi_device = nil
    $midi_sam2695_init_attempted = false
    $midi_usb_midi_host_init_attempted = false
    $midi_usb_midi_device_init_attempted = false
  end
end

# ============================================================================
# Wrap MIDI.start! to inject midori-side concerns
# ============================================================================
#
# picoruby-midi's MIDI.start! is intentionally free of application-layer
# concerns: no UI.process tick, no ScriptManager stop check. We add both
# here so SD-card scripts can write a plain MIDI.start! call and still
# get UI redraw and graceful script-switch behavior. Pattern:
#
#   MIDI.start! bpm: 120, output: dev do |c|
#     player.tick(c)
#   end
#
# Internally:
#   - Wrapper-injected block runs before the user block on every yield.
#   - When ScriptManager#stop_requested? becomes true, the block `break`s
#     out of MIDI.start!'s loop, letting its `ensure` send MIDI Stop and
#     then control returns to the script-switch driver.
#
# Note: require 'midi' here is essential. picoruby's C bindings define
# the MIDI module shell at VM init, but the Ruby-side methods (including
# start!) are defined by the gem's mrblib only when the gem is required.
# Without this require, alias_method below sees an undefined start!.
require 'midi'
require 'ui'
module MIDI
  class << self
    alias_method :_original_start!, :start!
    alias_method :_original_bpm_loop, :bpm_loop

    def start!(bpm: nil, output: nil, sync: nil, sync_from: nil, subdivisions: 24, &user_block)
      # The tempo needs no seeding: a BPM given here becomes MIDI::Clock's,
      # which the Tempo screen shows, and an omitted one follows it.
      sm = begin
             ScriptManager.new
           rescue
             nil
           end
      _original_start!(bpm: bpm, output: output, sync: sync, sync_from: sync_from,
                       subdivisions: subdivisions) do |c|
        if sm && sm.stop_requested?
          break
        end
        UI.process
        user_block.call(c) if user_block
      end
    end

    # bpm_loop is the legacy entry point. The upstream gem has been
    # stripped of ScriptManager references; we re-inject the stop check
    # here so existing SD-card scripts that rely on bpm_loop's old
    # midori-flavoured behavior keep working unchanged.
    #
    # The stop check rides on the user block (`break` exits _original_
    # bpm_loop). For scripts with sparse subdivisions this may delay
    # script-switch by up to one subdivision interval - acceptable for
    # the legacy path; new code should use MIDI.start! instead.
    def bpm_loop(bpm = nil, output: nil, subdivisions: 1, send_start: true,
                 sync: false, input: nil, sync_from: nil, on_loop: nil, on_error: nil,
                 bpm_source: nil, &user_block)
      sm = begin
             ScriptManager.new
           rescue
             nil
           end
      _original_bpm_loop(bpm, output: output, subdivisions: subdivisions,
                         send_start: send_start, sync: sync, input: input, sync_from: sync_from,
                         on_loop: on_loop, on_error: on_error,
                         bpm_source: bpm_source) do |c|
        if sm && sm.stop_requested?
          output.send_stop if output && send_start
          break
        end
        user_block.call(c) if user_block
      end
    end
  end
end

# Scripts that drive themselves with UI.process run the MIDI filters
# (MIDI.filter / MIDI.on) there too, so a UI loop with a plain sleep_ms does
# not leave them queued.
module UI
  class << self
    alias_method :_process_ui_only, :process

    def process
      MIDI.run_filters
      UI::MIDIDevices._poll
      result = _process_ui_only
      # After the events: the AMY panel redefines its layout here
      UI::AMYPanel._poll
      result
    end
  end
end

# ============================================================================
# Function definitions (used in both modes)
# ============================================================================

# The board's MIDI devices on the MIDI Devices screen (docs/UI_MIDI_DEVICES.md),
# then the patches saved there. Every mode does this at start: the supervisor
# clears the routes between scripts (picoruby_esp32_midi_cleanup). The built-in
# transports have fixed registry bits (USB host 0x01, UART 0x02, USB device
# 0x04); AMY gets one when it starts, so it is listed on standby (bit 0) and
# started only when a saved patch or the screen needs it.
def setup_midi_devices
  md = UI::MIDIDevices
  if BoardConfig::HAS_USB_MIDI_HOST
    md.register_builtin(:usb_host, "USB Host", 0x01, color: :green) { MIDIDevices.usb_midi_host }
  end
  if BoardConfig::HAS_SAM2695
    md.register_builtin(:uart, "SAM2695", 0x02, color: :orange) { MIDIDevices.sam2695 }
  end
  if BoardConfig::HAS_USB_MIDI_DEVICE
    md.register_builtin(:usb_dev, "PC", 0x04, color: :cyan) { MIDIDevices.usb_midi_device }
  end
  if BoardConfig::HAS_AMY
    md.register_builtin(:amy, "AMY", 0, color: :magenta) { MIDIDevices.amy }
  end
  # (The on-screen keys list themselves when their model is made.)
  # /sd/devices: after the built-ins, before the saved patches are routed
  load_devices
  md.boot
rescue => e
  puts "MIDI devices setup failed: #{e.message}"
end

# The Sequencer's song (docs/SEQUENCER.md): the autosave is read the first
# time after power-up and after a script's own song (UI::Sequencer.new), and
# from then on the File sheet and the autosave are served from every run of
# the MIDI filters (UI.process / MIDI.sleep_ms). Every mode, after the SD card.
def setup_sequencer
  UI::Sequencer._setup
rescue => e
  puts "Sequencer setup failed: #{e.message}"
end

# The AMY panel (docs/AMY_PANEL.md): a Home tile for building AMY parts by
# touch. UI mode only, after setup_midi_devices; AMY itself starts when the
# first part is added, through MIDIDevices.amy so the MIDI Devices screen
# lists it.
def setup_amy_panel
  return unless BoardConfig::HAS_AMY
  UI::AMYPanel.new(engine: Proc.new { MIDIDevices.amy })
rescue => e
  puts "AMY panel setup failed: #{e.message}"
end

# Ruby kept on the SD card, loaded at every start:
#
#   /sd/filters  named MIDI filters (docs/MIDI_FILTER_REGISTRY.md), each
#                expected to MIDI.register_filter what it defines
#   /sd/devices  MIDI devices for the MIDI Devices screen
#                (UI::MIDIDevices.register / register_builtin)
#   /sd/layouts  layouts (UI.layout), each a Home tile. UI mode only:
#                a script's screen is its own
#
# Each directory's *.rb and *.mrb (bytecode from this firmware's mrbc; load
# tells them apart by the RITE header) are read by name. Where foo.rb and
# foo.mrb are both there only foo.mrb is, as require would pick it.
# Subdirectories and files starting with "_" are left for the files to load
# themselves. A broken file is reported and
# skipped. The filters come first so the saved chains find theirs, the
# devices between the built-ins and the saved patches (setup_midi_devices)
# so the patches find them, the layouts last so they find both.
#
# Meanwhile the screen shows a loading box and takes no touches
# (UI._loading): on from power-up, and from the start of every other mode,
# until SDLoader.finish.
FILTER_DIR = "/sd/filters"
DEVICE_DIR = "/sd/devices"
LAYOUT_DIR = "/sd/layouts"

module SDLoader
  @files = {}
  @done = 0
  @total = 0

  # Lock the screen (before the SD card is up: nothing to count yet)
  def self.start(label = "Starting")
    @files = {}
    @done = 0
    @total = 0
    UI._loading(label, 0, 0)
  end

  # List what the directories hold, so the loading bar knows the total
  def self.scan(dirs)
    dirs.each do |dir|
      @files[dir] = ruby_files(dir)
      @total += @files[dir].size
    end
    UI._loading("SD card", @done, @total)
  end

  # Load what scan found in dir
  def self.load_dir(dir, tag)
    (@files[dir] || []).each do |path|
      UI._loading(path, @done, @total)
      begin
        Kernel.load(path)
        puts "[#{tag}] #{path}"
      rescue => e
        puts "[#{tag}] #{path}: #{e.message} (#{e.class})"
      end
      @done += 1
    end
  end

  # Unlock the screen
  def self.finish
    UI._loading(nil, 0, 0)
  end

  # *.rb and *.mrb in dir, by name, as paths; foo.mrb hides foo.rb
  # ([] without the SD card or the directory)
  def self.ruby_files(dir)
    return [] unless $sd_available
    exts = {}   # base name => ".rb" / ".mrb"
    begin
      Dir.open(dir) do |d|
        while entry = d.read
          next if entry == "." || entry == ".." || entry.start_with?("_")
          # FAT may hand back an 8.3 name in capitals (PERFORM.MRB)
          ext = entry.downcase
          if ext.end_with?(".mrb")
            exts[entry[0, entry.size - 4]] = entry[entry.size - 4, 4]
          elsif ext.end_with?(".rb")
            base = entry[0, entry.size - 3]
            exts[base] = entry[entry.size - 3, 3] unless exts[base]
          end
        end
      end
    rescue => e
      return []   # No such directory
    end
    exts.keys.sort.map { |base| "#{dir}/#{base}#{exts[base]}" }
  end
end

def load_filters
  SDLoader.load_dir(FILTER_DIR, "Filter")
  puts "Filters: #{MIDI.filters.keys.join(', ')}" if MIDI.filters.size > 0
end

def load_devices
  SDLoader.load_dir(DEVICE_DIR, "Device")
end

def load_layouts
  SDLoader.load_dir(LAYOUT_DIR, "Layout")
end

# Scan SD card for Ruby scripts and notify C side
def notify_scripts_to_c
  sm = ScriptManager.new
  sm.clear

  # Only scan if SD card is mounted
  if $sd_available
    begin
      names = []
      Dir.open("/sd") do |dir|
        while entry = dir.read
          next if entry == "." || entry == ".."
          next unless entry.end_with?(".rb")
          names << entry
        end
      end
      # Sort first so that, if the list overflows, the entries kept do not
      # depend on the FAT directory order.
      names = names.sort { |a, b| a.downcase <=> b.downcase }
      skipped = names.count { |name| !sm.add(name) }
      puts "Scripts notified to C side (#{names.size - skipped} of #{names.size})"
      if skipped > 0
        puts "Warning: #{skipped} script(s) not listed (too many, or name too long)"
      end
    rescue => e
      puts "Failed to scan scripts: #{e.message}"
    end
  else
    puts "SD card not available, skipping script scan"
  end

  # Always mark as ready regardless of SD card status
  sm.set_ready
end

# Unmount SD card if currently mounted. Used before remounting on refresh
# so that a physically removed/reinserted card is re-read from scratch
# (FatFs caches directory entries otherwise).
def unmount_sd_card
  idx = VFS.volume_index("/sd")
  return unless idx
  begin
    driver = VFS::VOLUMES[idx][:driver]
    VFS.unmount(driver, true)
    puts "SD card unmounted"
  rescue => e
    puts "SD unmount error: #{e.message}"
  end
end

# The SD card is FAT (picoruby-filesystem-fat, a midori gem), mounted as
# "/sd". Shell.setup_sdcard would put littlefs on it instead. (The internal
# flash, "/", is littlefs: Shell.setup_root_volume.)
def mount_sd_card(driver, sdmmc_slot: -1, sdmmc_freq_khz: -1)
  print "Initializing SD card(#{driver.class})... "
  sd = FAT.new(:sd, label: "SD", driver: driver,
               sdmmc_slot: sdmmc_slot, sdmmc_freq_khz: sdmmc_freq_khz)
  VFS.mount(sd, "/sd")
  puts "Available at /sd"
rescue => e
  puts "Not available"
  puts "#{e.message} (#{e.class})"
end

# SD card initialization function (can be called at startup and on refresh)
def try_init_sd_card
  return false if BoardConfig::SD_MODE == "none"

  # Skip if already mounted
  if VFS.volume_index("/sd")
    puts "SD card already mounted"
    return true
  end

  begin
    puts "Initializing SD card (#{BoardConfig::SD_MODE} mode)..."
    if BoardConfig::SD_MODE == "sdmmc"
      # SDMMC mode
      require "sdmmc"
      puts "  CLK=#{BoardConfig::SD_CLK_PIN}, CMD=#{BoardConfig::SD_CMD_PIN}, D0=#{BoardConfig::SD_D0_PIN}, slot=#{BoardConfig::SD_SLOT}"
      sdmmc = SDMMC.new(
        clk_pin:  BoardConfig::SD_CLK_PIN,
        cmd_pin:  BoardConfig::SD_CMD_PIN,
        d0_pin:   BoardConfig::SD_D0_PIN
      )
      mount_sd_card(sdmmc, sdmmc_slot: BoardConfig::SD_SLOT,
                           sdmmc_freq_khz: BoardConfig::SD_FREQ_KHZ)
    else
      # SPI mode (M5Stack)
      require "spi"
      puts "  SCK=#{BoardConfig::SD_SCK_PIN}, MISO=#{BoardConfig::SD_MISO_PIN}, MOSI=#{BoardConfig::SD_MOSI_PIN}, CS=#{BoardConfig::SD_CS_PIN}"
      spi = SPI.new(
        frequency: 5_000_000,
        unit: BoardConfig::SD_SPI_UNIT,
        sck_pin:  BoardConfig::SD_SCK_PIN,
        cipo_pin: BoardConfig::SD_MISO_PIN,
        copi_pin: BoardConfig::SD_MOSI_PIN,
        cs_pin:   BoardConfig::SD_CS_PIN
      )
      mount_sd_card(spi)
    end
  rescue => e
    puts "SD card driver error: #{e.message}"
  end

  # Check if SD card was actually mounted using VFS
  puts "Checking SD card mount status..."
  if VFS.volume_index("/sd")
    puts "SD card mounted successfully"
    return true
  else
    puts "SD card not mounted"
    return false
  end
end

# Execute a script
def run_script(script_path)
  puts "Running: #{script_path}"
  sm = ScriptManager.new
  begin
    unless $sd_available
      puts "SD card not available, skipping script"
      return true
    end

    unless File.exist?(script_path)
      puts "Script not found: #{script_path}"
      return true
    end

    # Clear UI pad settings before loading new script
    require 'ui'
    UI.pad_clear_all

    # Execute the script
    Kernel.load(script_path)
    puts "Script finished: #{script_path}"
    return true
  rescue => e
    error_msg = "Error: #{e.message}"
    puts error_msg
    sm.add_log(error_msg)
    return true
  ensure
    # MIDI cleanup is handled by Supervisor C code
    GC.start
  end
end

# Interactive Ruby session on the serial console (USB CDC in midi_device
# mode). See docs/IRB.md.
#
# The supervisor starts this like any script - own PicoRuby task, fresh
# VM, back to UI mode when it returns - and Shell#start(:irb) evaluates
# every line in a PicoRuby Sandbox, the same mechanism Kernel.load uses
# for scripts. A syntax error or an exception therefore lands in the
# sandbox, not in this task.
#
# (Named _session to stay clear of Shell#run_irb, which is the method this
# one ends up calling through Shell#start.)
def run_irb_session
  sm = ScriptManager.new
  # Hand the console over: from here the console task stops echoing and
  # line editing and pushes raw bytes into PicoRuby's stdin. Editor::Line
  # does its own echo, so leaving the C side in line mode would show
  # every keystroke twice and swallow the arrow keys.
  sm.irb_begin
  begin
    STDIN.echo = false
    # Popped by the first Sandbox#wait (the warm-up run inside
    # Shell#run_irb), so that one cannot be interrupted. Every later wait
    # checks signals again, which is what makes Ctrl-C abort a long
    # running expression.
    Machine.signal_self_manage
    # clean: true probes the terminal (ENV['TERM']) before the editor asks
    # for the screen size; without it every redraw waits out a 500 ms
    # cursor-position timeout on terminals that do not answer, such as
    # idf.py monitor.
    shell = Shell.new(clean: true)
    puts "PicoRuby irb - #{BoardConfig::BOARD_NAME}"
    puts "quit / exit / Ctrl-D to leave, Ctrl-C to abort an expression"
    puts ""
    shell.start(:irb)
    puts "Leaving irb"
  rescue => e
    error_msg = "irb error: #{e.message}"
    puts error_msg
    sm.add_log(error_msg)
  ensure
    sm.irb_end
    GC.start
  end
end

# ============================================================================
# Main execution: irb Mode vs UI Mode vs Script Mode
# ============================================================================
# The supervisor tells us which mode this task was started in:
# - irb_requested?: irb Mode - interactive console session, then exit
# - get_autorun_script: Script Mode - run the script then exit
# - neither: UI Mode - enter the script selection loop
# ============================================================================

# From power-up the screen is locked (UI._loading) until the boot sequence has
# loaded /sd; every later mode locks it again while it does the same.
SDLoader.start

sm = ScriptManager.new
script_to_run = sm.get_autorun_script

if sm.irb_requested?
  # ========== irb Mode ==========
  # Same minimal bring-up as Script Mode: irb is expected to poke at the
  # same things a script does, /sd included.
  require 'machine'  # This loads picoruby-machine/mrblib/kernel.rb which defines STDIN/STDOUT
  require "watchdog"
  Watchdog.disable
  require "shell"

  puts "irb Mode"

  # Each script gets a fresh VM, so VFS starts empty: check and re-init if needed
  $sd_available = VFS.volume_index("/sd") ? true : false
  if !$sd_available && BoardConfig::SD_MODE != "none"
    puts "SD card not mounted, re-initializing..."
    $sd_available = try_init_sd_card
  end

  SDLoader.scan([FILTER_DIR, DEVICE_DIR])
  load_filters
  setup_midi_devices
  setup_sequencer
  SDLoader.finish
  run_irb_session
  puts "irb finished, exiting to supervisor"

elsif script_to_run
  # ========== Script Mode ==========
  # Load minimal required gems
  require 'machine'  # This loads picoruby-machine/mrblib/kernel.rb which defines STDIN/STDOUT
  require "watchdog"
  Watchdog.disable
  require "shell"

  # STDIN/STDOUT are already defined in kernel.rb, no need to redefine

  puts "Script Mode: #{script_to_run}"

  # Each script gets a fresh VM, so VFS starts empty: check and re-init if needed
  $sd_available = VFS.volume_index("/sd") ? true : false
  if !$sd_available && BoardConfig::SD_MODE != "none"
    puts "SD card not mounted, re-initializing..."
    $sd_available = try_init_sd_card
  else
    puts "SD card ready"
  end

  # The built-in MIDI devices start here (setup_midi_devices); AMY and the
  # rest of MIDIDevices stay lazy

  SDLoader.scan([FILTER_DIR, DEVICE_DIR])
  load_filters
  setup_midi_devices
  setup_sequencer
  SDLoader.finish
  run_script(script_to_run)
  puts "Script completed, exiting to supervisor"
  # main_task.rb ends here, supervisor will detect and restart in UI mode

else
  # ========== UI Mode ==========
  # Perform full initialization only in UI mode
  require 'machine'  # This loads picoruby-machine/mrblib/kernel.rb which defines STDIN/STDOUT
  require "watchdog"
  Watchdog.disable
  require "shell"

  # STDIN/STDOUT are already defined in kernel.rb, no need to redefine

  # File transfer with the PicoRuby web terminal (see docs/PICOMODEM.md).
  # Required here, after "shell" has pulled in vfs, so that the File patch
  # below reopens the real File class instead of creating an empty one.
  require "picomodem"

  # PicoModem asks the file it is writing to preallocate a contiguous
  # block. FatFs' f_expand() fails with FR_DENIED when the card has no
  # contiguous free area that big, and picoruby-filesystem-fat turns that
  # into a RuntimeError that aborts the upload. Contiguous allocation is
  # only an optimisation, so fall back to a plain write.
  class File
    def expand(size)
      begin
        @file.expand(size) if @file.respond_to?(:expand)
      rescue => e
        # fragmented card: f_write still works
      end
      size
    end
  end

  puts "Board: #{BoardConfig::BOARD_NAME}"

  # Setup flash disk
  begin
    STDIN.echo = false
    puts "Initializing FLASH disk as the root volume... "
    Shell.setup_root_volume(:flash, label: 'storage')
    Shell.setup_system_files
    puts "Available"
  rescue => e
    puts "Not available"
    puts "#{e.message} (#{e.class})"
  end

  # Setup SD card
  $sd_available = false
  if BoardConfig::SD_MODE == "none"
    puts "SD card disabled (GPIO conflict with PSRAM)"
  else
    $sd_available = try_init_sd_card
  end

  # Always notify C side about scripts (even if SD card failed)
  notify_scripts_to_c

  # The built-in MIDI devices start here (setup_midi_devices), so the MIDI
  # Devices screen can patch between them; AMY stays lazy. The filters
  # come first so the saved chains find theirs, the layouts last.
  SDLoader.scan([FILTER_DIR, DEVICE_DIR, LAYOUT_DIR])
  load_filters
  setup_midi_devices
  setup_sequencer
  setup_amy_panel
  load_layouts

  GC.start
  SDLoader.finish

  puts "Initialization complete."
  puts "UI Mode: Waiting for script selection"
  puts ""
  puts "Available commands:"
  puts "  load /sd/app.rb  - Load and run a script"
  puts "  irb              - Start an interactive Ruby session"
  puts "  heap             - Show free heap memory"
  puts "  restart          - Restart ESP32"
  print "> "

  loop do
    # Check for a file transfer requested by the PicoRuby web terminal.
    # The console task has already answered the STX handshake and is
    # feeding raw bytes to STDIN; run one PicoModem command and return.
    if sm.modem_requested?
      begin
        PicoModem.session($stdin, ConsoleIO.new)
      rescue => e
        puts "[PicoModem] error: #{e.message}"
      end
      sm.modem_end
      GC.start
      next
    end

    # Check for console input (load command)
    console_script = sm.check_console
    if console_script
      puts "[Console] Load: #{console_script}"
      # MIDI cleanup is handled by Supervisor C code
      # Request supervisor to load this script
      sm.request_script(console_script)
      # Exit UI loop - supervisor will restart with the new script
      break
    end

    # Check for script request from UI (M5Stack touch)
    script_path = sm.get_requested
    if script_path
      puts "[UI] Load: #{script_path}"
      sm.clear_request
      # MIDI cleanup is handled by Supervisor C code
      # Request supervisor to load this script
      sm.request_script(script_path)
      # Exit UI loop - supervisor will restart with the new script
      break
    end

    # Check for SD card refresh request from UI
    if sm.sd_refresh_requested?
      puts "[UI] SD refresh requested"
      sm.clear_sd_refresh
      # Always unmount/remount so a physically swapped card is re-read.
      # FatFs otherwise serves a cached directory listing.
      unmount_sd_card
      $sd_available = try_init_sd_card
      if $sd_available
        notify_scripts_to_c
        # Filters and layouts again (a redefinition replaces the old one);
        # the devices only at the next start, their transports being made
        SDLoader.start("SD card")
        SDLoader.scan([FILTER_DIR, LAYOUT_DIR])
        load_filters
        load_layouts
        SDLoader.finish
        UI::Sequencer._send_list rescue nil
        puts "SD card re-initialized successfully"
      else
        puts "SD card not available"
        notify_scripts_to_c  # still notify so UI can clear its list
      end
    end

    # Filters, their panels (layouts) and what the MIDI Devices screen
    # changed (UI.process runs MIDI.run_filters and UI::MIDIDevices._poll);
    # MIDI.sleep_ms wakes as soon as a filter has something to do
    UI.process
    # A layout's pads and knobs (the AMY panel, /sd/layouts) run their
    # blocks here: they would lag by the 100 ms otherwise
    MIDI.sleep_ms(UI._requested_screen[0] == UI::SCREEN_LAYOUT ? 10 : 100)
  end

  puts "Exiting UI loop"
end

# main_task.rb ends - supervisor will handle next step
puts "main_task.rb completed"
