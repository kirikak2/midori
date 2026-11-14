# UI::MIDIDevices: the MIDI Devices screen from Ruby (docs/UI_MIDI_DEVICES.md)
#
#   M = UI::MIDIDevices
#   M.register(MIDI::Device.new(MIDIDevices.usb_midi_host), name: "J-6")
#   M.patch(j6, amy)                  # MIDI.route, shown on the screen
#   M.midi_in  = j6                   # j6 -> Script (the script's IN)
#   M.midi_out = amy                  # Script -> amy (the script's OUT)
#   M.input.on(:note_on) { |m| M.output.note_on(m.note + 12, m.velocity) }
#
# The script is itself a device on the screen, "Script" (picoruby-midi's
# virtual port, one transport bit): what it sends to it (midi_out / output)
# goes out along Script's routes, and what other devices' routes bring into
# it reaches midi_in / input. The user patches Script like any other device,
# so M.output and M.input keep reaching whatever is patched later.
#
# Devices are picoruby-midi transports. The firmware's own (USB host, UART,
# USB device, AMY, the on-screen keys) are registered at every start
# (register_builtin, from main_task.rb); a script may register more, which
# go when it ends. Patches between built-ins made on the screen are saved
# (NVS) and routed again at every start.

# bit => MIDI::Device for that transport
$ui_mididev_devices = {}
# built-in key => Proc giving its transport (or MIDI::Device)
$ui_mididev_factories = {}
$ui_mididev_input = nil
$ui_mididev_hooked = false
$ui_mididev_serial = -1

module UI
  module MIDIDevices
    FLAG_BUILTIN = 1
    FLAG_STANDBY = 2
    ROUTE_SLOTS = 32   # MIDI_ROUTE_MAX

    # A registered transport known only by its bit (one whose Ruby object
    # this VM never made, e.g. after a restart): enough for MIDI::Device.
    class BitTransport
      attr_reader :transport_id

      def initialize(bit, name = nil)
        @transport_id = bit
        @name = name
      end

      def send_packet(cable, cin, midi1, midi2, midi3)
        MIDI._transport_send(@transport_id, cin, midi1, midi2, midi3)
        0
      end

      def connected?
        MIDI._transport_connected?(@transport_id)
      end

      def bytes_available
        0
      end

      def read_available
        nil
      end

      def device_info
        @name || "MIDI transport #{@transport_id}"
      end
    end

    # The script's IN: what the routes into Script bring (from every device
    # patched to it), now and after the user patches another.
    #
    #   inp = UI::MIDIDevices.input
    #   inp.on(:note_on) { |m| m.source }   # m.source: the device it came from
    class Input
      def initialize
        @handlers = []   # [type, channel, block]
      end

      # Like MIDI.on, on Script. Returns the block (for #off).
      def on(type = :any, channel: nil, &block)
        raise ArgumentError, "UI::MIDIDevices.input.on: a block is required" unless block
        @handlers << [type, channel, block]
        bit = MIDIDevices.script_bit
        MIDI.on(bit, type, channel: channel, &block) if bit != 0
        block
      end

      # Remove one handler, or all of them.
      def off(handler = nil)
        bit = MIDIDevices.script_bit
        @handlers.each do |h|
          next unless handler.nil? || h[2] == handler
          MIDI.off(bit, nil, h[2]) if bit != 0
        end
        @handlers.delete_if { |h| handler.nil? || h[2] == handler }
        self
      end

      # Script, while something is patched to it (nil otherwise)
      def device
        MIDIDevices.midi_in
      end

      # The external clock of the first device patched to Script that has one
      def external_bpm
        MIDIDevices._script_sources.each do |src|
          bpm = MIDI.external_bpm(src)
          return bpm if bpm > 0.0
        end
        0.0
      end

      def reset_external_clock
        MIDIDevices._script_sources.each { |src| MIDI.reset_external_clock(src) }
      end
    end

    class << self
      # Add a device to the list (or rename one already there).
      # @param device [MIDI::Device, Object] a MIDI::Device or its transport
      # @param name [String] shown on the screen
      # @param color [Symbol, Integer, nil] colour of its patches
      # @return [MIDI::Device]
      def register(device, name: nil, color: nil)
        dev = _wrap(device)
        bit = MIDI.route_bit(dev)
        raise ArgumentError, "UI::MIDIDevices.register: not a registered MIDI transport" if bit == 0
        entry = _entry(_index_of_bit(bit))
        key = entry ? entry[2] : ""
        builtin = entry ? (entry[1] & FLAG_BUILTIN) != 0 : false
        label = (name || (entry ? entry[3] : _default_name(dev))).to_s
        if UI._mididev_register(key, label, bit, _color(color), builtin) < 0
          raise "UI::MIDIDevices.register: the device list is full"
        end
        $ui_mididev_devices[bit] = dev
        MIDI._set_source_info(bit, key == "" ? nil : key.to_sym, dev)
        dev
      end

      # Take a device off the list, with every patch from or to it.
      def unregister(device)
        bit = MIDI.route_bit(device)
        return false if bit == 0
        b = 1
        while b <= MIDI::ALL_TRANSPORTS
          if b != bit
            MIDI.unfilter(b, bit)
            MIDI._route_remove(b, bit)
          end
          b = b << 1
        end
        MIDI.unroute(bit)
        $ui_mididev_devices.delete(bit)
        UI._mididev_unregister(bit)
      end

      # The firmware's devices (main_task.rb, at every start). The block gives
      # the transport; it runs now, or for bit 0 (standby: AMY) the first time
      # something needs the device. output: false for one that only sends
      # (no MIDI OUT column).
      def register_builtin(key, name, bit = 0, color: nil, output: true, &factory)
        k = key.to_s
        $ui_mididev_factories[k] = factory if factory
        UI._mididev_register(k, name.to_s, bit, _color(color), true)
        _start(k) if bit != 0 && factory
        UI._mididev_no_out(k) unless output   # after _start, which registers again
        nil
      end

      # Route the saved patches (main_task.rb, after register_builtin).
      def boot
        bit = script_bit
        MIDI._set_source_info(bit, :script, _script_device) if bit != 0
        MIDIFilters.install
        UI._mididev_restore
        service
        MIDIFilters._poll
      end

      # Start the standby devices something asked for (a saved patch, the
      # screen). Called from UI.process and the MIDI filter loop.
      def service
        while (key = UI._mididev_take_wanted)
          _start(key)
        end
        nil
      end

      # @return [Array<MIDI::Device>] the devices that are started
      def devices
        list = []
        n = UI._mididev_count
        i = 0
        while i < n
          e = UI._mididev_get(i)
          list << _device_for_bit(e[0]) if e && e[0] != 0
          i += 1
        end
        list
      end

      # Whether the device is up (USB enumerated, UART installed, ...)
      def active?(device)
        bit = MIDI.route_bit(device)
        bit != 0 && MIDI._transport_connected?(bit)
      end

      # = MIDI.route(from, to, channel:), shown on the screen. filters: puts
      # named filters on it (MIDI.chain): [:chord, [:arp, { rate: 2 }]]
      def patch(from, to, channel: nil, filters: nil)
        r = MIDI.route(from, to, channel: channel)
        MIDI.chain(from, to, *filters) if r && filters && !filters.empty?
        r
      end

      # = MIDI.unroute(from, to)
      def unpatch(from, to = nil)
        MIDI.unroute(from, to)
      end

      # Every route between devices, as the screen shows it.
      # @return [Array<Hash>] from:, to:, channel: (nil = all, 0..15, :clock =
      #   clock only, or an Array of those for several), active:, filtered:
      def patches
        list = []
        i = 0
        while i < ROUTE_SLOTS
          r = MIDI._route_get(i)
          i += 1
          next unless r
          filtered = MIDI._route_filtered(r[0])
          b = 1
          while b <= MIDI::ALL_TRANSPORTS
            if (r[1] & b) != 0
              list << { from: _device_for_bit(r[0]), to: _device_for_bit(b),
                        channel: MIDI._route_channel(r[2], r[3]),
                        active: MIDI._transport_connected?(r[0]) && MIDI._transport_connected?(b),
                        filtered: (filtered & b) != 0,
                        filters: _chain_names(r[0], b) }
            end
            b = b << 1
          end
        end
        list
      end

      # Make the routes between built-ins what a script starts with.
      def save
        UI._mididev_save
      end

      # The Script device's bit (0 on a board without the MIDI Devices screen)
      def script_bit
        MIDI._route_virtual
      end

      # The script's IN / OUT: the Script device, while something is patched
      # to it / from it (nil otherwise, so `md.midi_out ||= dev` patches dev).
      # The user may patch more or other devices on the screen; Script stays
      # the same device, so it can be kept.
      def midi_in
        _script_sources.empty? ? nil : _script_device
      end

      def midi_out
        bit = script_bit
        bit != 0 && MIDI._route_targets_of(bit) != 0 ? _script_device : nil
      end

      # Patch `device` (all channels) to Script in place of what was, for the
      # script's run (the patches made on the screen come back after it).
      # nil takes every patch to Script off.
      def midi_in=(device)
        bit = script_bit
        return device if bit == 0
        _script_sources.each do |src|
          MIDI.unfilter(src, bit)
          MIDI._route_remove(src, bit)
        end
        if device
          register(device) unless _index_of_bit(MIDI.route_bit(device))
          MIDI.route(device, bit)
        end
        device
      end

      # Patch Script to `device` (all channels) in place of what was, for the
      # script's run. nil takes every patch from Script off.
      def midi_out=(device)
        bit = script_bit
        return device if bit == 0
        MIDI.unroute(bit)
        if device
          register(device) unless _index_of_bit(MIDI.route_bit(device))
          MIDI.route(bit, device)
        end
        device
      end

      # What arrives on Script (see Input).
      def input
        unless $ui_mididev_input
          $ui_mididev_input = Input.new
          _hook
        end
        $ui_mididev_input
      end

      # Script as a MIDI::Device: what is sent to it goes out along its
      # routes. Unlike midi_out, never nil (it sends nowhere while unpatched).
      #
      #   out = UI::MIDIDevices.output
      #   player = MIDI::MML::CombinedPlayer.new(out)
      #   UI.pad(1) { out.trigger(36) }
      #
      # Objects that send from C (UI::Keys, UI::Tombola device:, the note
      # scheduler) take its bit and so follow the patches too.
      def output
        _script_device
      end

      # Patch and connection changes. The block gets a Hash:
      # kind: (:patch / :unpatch / :connect / :disconnect), device: / from: / to: (MIDI::Device), channel: (patches:
      # 0..15, nil for all, :clock for the clock alone, an Array of those for
      # several).
      def on_change(&block)
        UI.on(:midi_devices, &block)
      end

      def show
        UI.show(:midi_devices)
      end

      # --- internals ---

      # Once per run of the MIDI filters: catch up when the model changed
      def _poll
        MIDIFilters._poll
        serial = UI._mididev_serial
        return if serial == $ui_mididev_serial
        $ui_mididev_serial = serial
        service
        _note_sources
      end

      # Built-ins the firmware lists by itself (the devices on the USB host
      # port, as they are plugged in) have no factory: give their bits a key
      # for m.source like the others
      def _note_sources
        n = UI._mididev_count
        i = 0
        while i < n
          e = UI._mididev_get(i)
          i += 1
          next unless e && e[0] != 0 && (e[1] & FLAG_BUILTIN) != 0 && e[2] != ""
          info = $__midi_source_info__[e[0]]
          next if info && info[0] == e[2].to_sym
          MIDI._set_source_info(e[0], e[2].to_sym, $ui_mididev_devices[e[0]])
        end
      end

      # Follow the model from every run of the MIDI filters (once)
      def _hook
        return if $ui_mididev_hooked
        $ui_mididev_hooked = true
        $__midi_filter_hooks__ << lambda { MIDIDevices._poll }
      end

      def _device_for_bit(bit)
        return nil if bit.nil? || bit == 0
        dev = $ui_mididev_devices[bit]
        return dev if dev
        # The tempo is a transport object of its own (UI::Tempo)
        if bit == UI::Tempo.transport_id
          dev = MIDI::Device.new(UI::Tempo)
          $ui_mididev_devices[bit] = dev
          return dev
        end
        entry = _entry(_index_of_bit(bit))
        if entry && $ui_mididev_factories[entry[2]]
          dev = _start(entry[2])
          return dev if dev
        end
        dev = MIDI::Device.new(BitTransport.new(bit, entry ? entry[3] : nil))
        $ui_mididev_devices[bit] = dev
        dev
      end

      def _chain_names(src, dst)
        c = $__midi_chains__ ? $__midi_chains__[MIDI._pair_key(src, dst)] : nil
        c ? c.names : []
      end

      # The name the screen shows for a bit ("?" when it is not listed)
      def _name_of_bit(bit)
        e = _entry(_index_of_bit(bit))
        e ? e[3] : "?"
      end

      # Script as a MIDI::Device (sends to nothing on a board without it)
      def _script_device
        bit = script_bit
        return _device_for_bit(bit) if bit != 0
        $ui_mididev_devices[0] ||= MIDI::Device.new(BitTransport.new(0, "Script"))
      end

      # The bits of the devices patched to Script
      def _script_sources
        bit = script_bit
        list = []
        return list if bit == 0
        i = 0
        while i < ROUTE_SLOTS
          r = MIDI._route_get(i)
          i += 1
          list << r[0] if r && (r[1] & bit) != 0 && !list.include?(r[0])
        end
        list
      end

      # Run a built-in's factory: its device, now with a bit
      def _start(key)
        factory = $ui_mididev_factories[key]
        return nil unless factory
        t = factory.call
        return nil unless t
        dev = _wrap(t)
        bit = MIDI.route_bit(dev)
        return nil if bit == 0
        $ui_mididev_devices[bit] = dev
        UI._mididev_activate(key, bit)
        MIDI._set_source_info(bit, key.to_sym, dev)
        dev
      end

      def _wrap(device)
        device.is_a?(MIDI::Device) ? device : MIDI::Device.new(device)
      end

      def _entry(index)
        return nil if index.nil? || index < 0
        UI._mididev_get(index)
      end

      def _index_of_bit(bit)
        n = UI._mididev_count
        i = 0
        while i < n
          e = UI._mididev_get(i)
          return i if e && e[0] == bit
          i += 1
        end
        nil
      end

      def _default_name(dev)
        info = dev.transport.respond_to?(:device_info) ? dev.transport.device_info : nil
        info.is_a?(String) && info.size > 0 ? info : dev.transport.class.to_s
      end

      def _color(color)
        return 0 if color.nil?
        return color if color.is_a?(Integer)
        return UI.rgb(color[0], color[1], color[2]) if color.is_a?(Array)
        UI.color_to_rgb565(color)
      end
    end
  end
end
