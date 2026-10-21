# UI::MIDIDevices: the MIDI Devices screen from Ruby (docs/UI_MIDI_DEVICES.md)
#
#   M = UI::MIDIDevices
#   M.register(MIDI::Device.new(MIDIDevices.usb_midi_host), name: "J-6")
#   M.patch(j6, amy)                  # MIDI.route, shown on the screen
#   M.midi_in  = j6                   # the program's IN / OUT; the user may
#   M.midi_out = amy                  # change them on the screen
#   M.input.on(:note_on) { |m| M.midi_out&.note_on(m.note + 12, m.velocity) }
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
$ui_mididev_serial = -1

module UI
  module MIDIDevices
    FLAG_BUILTIN = 1
    FLAG_STANDBY = 2
    ROUTE_SLOTS = 8   # MIDI_ROUTE_MAX

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

    # The program's IN, following the selection: what arrives on whichever
    # device is selected as the IN, now and after the user picks another.
    #
    #   inp = UI::MIDIDevices.input
    #   inp.on(:note_on) { |m| ... }
    class Input
      def initialize
        @handlers = []   # [type, channel, block]
        @src = 0
        @src_device = nil
      end

      # Like MIDI.on, on the selected IN. Returns the block (for #off).
      def on(type = :any, channel: nil, &block)
        raise ArgumentError, "UI::MIDIDevices.input.on: a block is required" unless block
        @handlers << [type, channel, block]
        MIDI.on(@src, type, channel: channel, &block) if @src != 0
        block
      end

      # Remove one handler, or all of them.
      def off(handler = nil)
        @handlers.each do |h|
          next unless handler.nil? || h[2] == handler
          MIDI.off(@src, nil, h[2]) if @src != 0
        end
        @handlers.delete_if { |h| handler.nil? || h[2] == handler }
        self
      end

      # The device heard now (nil when none is selected)
      def device
        @src_device
      end

      def external_bpm
        @src == 0 ? 0.0 : MIDI.external_bpm(@src)
      end

      def reset_external_clock
        MIDI.reset_external_clock(@src) if @src != 0
      end

      # Internal: move the handlers when the selection changed.
      def _sync
        dev = MIDIDevices.midi_in
        bit = dev ? MIDI.route_bit(dev) : 0
        # Only a route source can be listened to (a stale selection is none)
        if bit != 0 && (bit & MIDI._route_sources) == 0
          bit = 0
          dev = nil
        end
        return if bit == @src
        if @src != 0
          @handlers.each { |h| MIDI.off(@src, nil, h[2]) }
        end
        @src = bit
        @src_device = dev
        if bit != 0
          @handlers.each { |h| MIDI.on(bit, h[0], channel: h[1], &h[2]) }
          MIDI.reset_external_clock(bit)
        end
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
        while b < 0x100
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
      # something needs the device.
      def register_builtin(key, name, bit = 0, color: nil, &factory)
        k = key.to_s
        $ui_mididev_factories[k] = factory if factory
        UI._mididev_register(k, name.to_s, bit, _color(color), true)
        _start(k) if bit != 0 && factory
        nil
      end

      # Route the saved patches and pick the saved IN / OUT (main_task.rb,
      # after register_builtin).
      def boot
        UI._mididev_restore
        service
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

      # = MIDI.route(from, to, channel:), shown on the screen
      def patch(from, to, channel: nil)
        MIDI.route(from, to, channel: channel)
      end

      # = MIDI.unroute(from, to)
      def unpatch(from, to = nil)
        MIDI.unroute(from, to)
      end

      # Every route between devices, as the screen shows it.
      # @return [Array<Hash>] from:, to:, channel: (nil = all), active:, filtered:
      def patches
        list = []
        i = 0
        while i < ROUTE_SLOTS
          r = MIDI._route_get(i)
          i += 1
          next unless r
          filtered = MIDI._route_filtered(r[0])
          b = 1
          while b < 0x100
            if (r[1] & b) != 0
              list << { from: _device_for_bit(r[0]), to: _device_for_bit(b),
                        channel: r[2] < 0 ? nil : r[2],
                        active: MIDI._transport_connected?(r[0]) && MIDI._transport_connected?(b),
                        filtered: (filtered & b) != 0 }
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

      # The program's IN / OUT (nil for none). The user may change them on
      # the screen at any time; ask again rather than keeping the result.
      def midi_in
        _device_at(UI._mididev_selected(false))
      end

      def midi_out
        _device_at(UI._mididev_selected(true))
      end

      def midi_in=(device)
        _select(false, device)
      end

      def midi_out=(device)
        _select(true, device)
      end

      # What arrives on the IN, following the selection (see Input).
      def input
        unless $ui_mididev_input
          $ui_mididev_input = Input.new
          $__midi_filter_hooks__ << lambda { MIDIDevices._poll }
          $ui_mididev_input._sync
        end
        $ui_mididev_input
      end

      # Selection, patch and connection changes. The block gets a Hash:
      # kind: (:select_in / :select_out / :patch / :unpatch / :connect /
      # :disconnect), device: / from: / to: (MIDI::Device), channel:.
      def on_change(&block)
        UI.on(:midi_devices, &block)
      end

      def show
        UI.show(:midi_devices)
      end

      # --- internals ---

      # Once per run of the MIDI filters: catch up when the model changed
      def _poll
        serial = UI._mididev_serial
        return if serial == $ui_mididev_serial
        $ui_mididev_serial = serial
        service
        $ui_mididev_input._sync if $ui_mididev_input
      end

      def _device_for_bit(bit)
        return nil if bit.nil? || bit == 0
        dev = $ui_mididev_devices[bit]
        return dev if dev
        entry = _entry(_index_of_bit(bit))
        if entry && $ui_mididev_factories[entry[2]]
          dev = _start(entry[2])
          return dev if dev
        end
        dev = MIDI::Device.new(BitTransport.new(bit, entry ? entry[3] : nil))
        $ui_mididev_devices[bit] = dev
        dev
      end

      def _device_at(index)
        entry = _entry(index)
        return nil unless entry
        return _start(entry[2]) if entry[0] == 0   # standby: start it now
        _device_for_bit(entry[0])
      end

      def _select(out, device)
        if device.nil?
          UI._mididev_select(out, -1, false)
        else
          bit = MIDI.route_bit(device)
          raise ArgumentError, "UI::MIDIDevices: not a registered MIDI transport" if bit == 0
          if !out && (bit & MIDI._route_sources) == 0
            raise ArgumentError, "UI::MIDIDevices.midi_in: the device has no MIDI IN"
          end
          register(device) unless _index_of_bit(bit)
          $ui_mididev_devices[bit] ||= _wrap(device)
          UI._mididev_select(out, _index_of_bit(bit), false)
        end
        $ui_mididev_input._sync if $ui_mididev_input && !out
        device
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
