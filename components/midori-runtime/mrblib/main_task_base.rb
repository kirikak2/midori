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

    def start!(bpm: 120, output: nil, sync: nil, subdivisions: 24, &user_block)
      # Seed UI.bpm with the caller-supplied initial value so the on-screen
      # tempo widget matches what the script asked for. Subsequent UI
      # tempo changes flow back into the loop via the MIDI.on_bpm_change
      # hook wired up at the bottom of this file.
      # (mrubyc has no Numeric class; check Integer/Float individually.)
      UI.set_bpm(bpm) if bpm.is_a?(Integer) || bpm.is_a?(Float)
      sm = begin
             ScriptManager.new
           rescue
             nil
           end
      _original_start!(bpm: bpm, output: output, sync: sync,
                       subdivisions: subdivisions) do |c|
        if sm && sm.stop_requested?
          break
        end
        UI.process
        user_block.call(c) if user_block
      end
    end

    # bpm_loop is the legacy entry point. The upstream gem has been
    # stripped of UI.bpm and ScriptManager references; we re-inject them
    # here so existing SD-card scripts that rely on bpm_loop's old
    # midori-flavoured behavior keep working unchanged.
    #
    # The stop check rides on the user block (`break` exits _original_
    # bpm_loop). For scripts with sparse subdivisions this may delay
    # script-switch by up to one subdivision interval - acceptable for
    # the legacy path; new code should use MIDI.start! instead.
    def bpm_loop(bpm = 120, output: nil, subdivisions: 1, send_start: true,
                 sync: false, input: nil, on_loop: nil, on_error: nil,
                 bpm_source: nil, &user_block)
      # Seed UI.bpm so the tempo widget reflects the caller-supplied value;
      # subsequent UI changes are propagated via MIDI.on_bpm_change (wired
      # up below). bpm_source: still works for scripts that supply their
      # own dynamic tempo callable.
      # (mrubyc has no Numeric class; check Integer/Float individually.)
      UI.set_bpm(bpm) if bpm.is_a?(Integer) || bpm.is_a?(Float)
      sm = begin
             ScriptManager.new
           rescue
             nil
           end
      _original_bpm_loop(bpm, output: output, subdivisions: subdivisions,
                         send_start: send_start, sync: sync, input: input,
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

# Bridge UI tempo changes into MIDI's BPM-change hook so any running
# clock loop (start! or bpm_loop) follows the on-screen widget without
# the script having to wire it up itself.
UI.on(:bpm_change) do |event|
  MIDI._notify_bpm_change(event[:bpm]) if event && event[:bpm]
end

# ============================================================================
# Function definitions (used in both modes)
# ============================================================================

# Scan SD card for Ruby scripts and notify C side
def notify_scripts_to_c
  sm = ScriptManager.new
  sm.clear

  # Only scan if SD card is mounted
  if $sd_available
    begin
      Dir.open("/sd") do |dir|
        while entry = dir.read
          next if entry == "." || entry == ".."
          next unless entry.end_with?(".rb")
          sm.add(entry)
        end
      end
      puts "Scripts notified to C side"
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
        d0_pin:   BoardConfig::SD_D0_PIN,
        slot:     BoardConfig::SD_SLOT,
        freq_khz: BoardConfig::SD_FREQ_KHZ
      )
      Shell.setup_sdcard(sdmmc)
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
      Shell.setup_sdcard(spi)
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

  # MIDI devices will be initialized on first access
  # (lazy initialization to reduce startup power consumption)

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

  # MIDI devices will be initialized on first access
  # (lazy initialization to reduce startup power consumption)

  GC.start

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
        puts "SD card re-initialized successfully"
      else
        puts "SD card not available"
        notify_scripts_to_c  # still notify so UI can clear its list
      end
    end

    sleep_ms 100
  end

  puts "Exiting UI loop"
end

# main_task.rb ends - supervisor will handle next step
puts "main_task.rb completed"
