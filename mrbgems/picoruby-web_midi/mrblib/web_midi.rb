# The page's Web MIDI ports as MIDI transports (PicoRuby.wasm / the MIDoRI
# simulator only):
#
#   dev = MIDI::Device.new(MIDIDevices.web_midi("IAC"))
#   MIDI.route(MIDIDevices.web_midi("Keystation"), MIDIDevices.sam2695)
#
# A port is found by name: the exact name first, then a part of it (case
# ignored). The device has no such ports, so a script meant for it too
# checks first: MIDIDevices.respond_to?(:web_midi).
module WebMIDI
  class Port
    attr_reader :slot

    def initialize(slot)
      @slot = slot
    end

    def name;         WebMIDI._name(@slot);       end
    def input?;       WebMIDI._has_input(@slot);  end
    def output?;      WebMIDI._has_output(@slot); end
    def connected?;   WebMIDI._connected(@slot);  end

    # Its bit in picoruby-midi's transport registry (0 when it is full)
    def transport_id
      WebMIDI._bit(@slot)
    end

    def send_packet(cable, cin, midi1, midi2, midi3)
      WebMIDI._send_packet(@slot, cable, cin, midi1, midi2, midi3)
    end

    def bytes_available
      WebMIDI._bytes_available(@slot)
    end

    # The 4-byte USB-MIDI packets received, or nil. Not while a route,
    # filter or MIDI.on reads the port (the input task takes them then).
    def read_available
      WebMIDI._read_available(@slot)
    end

    def device_info
      { name: name, input: input?, output: output?, transport: "Web MIDI" }
    end

    def open_device
      connected? ? self : nil
    end

    def inspect
      "#<WebMIDI::Port #{name.inspect}#{connected? ? '' : ' (closed)'}>"
    end
  end

  @ports = []

  # Every port the page knows of
  def self.ports
    n = _count
    i = @ports.size
    while i < n
      @ports << Port.new(i)
      i += 1
    end
    @ports.select { |p| p.name }
  end

  # The port by name (exact, else a part of it, case ignored), or nil
  def self.find(name)
    all = ports
    s = name.to_s
    hit = all.find { |p| p.name == s }
    return hit if hit
    d = s.downcase
    all.find { |p| p.name.downcase.include?(d) }
  end

  # List the ports on the MIDI Devices screen (the simulator's start-up,
  # before the saved patches are routed). Keyed by a hash of the name, so
  # patches saved to a port come back on the next run.
  def self.register_devices
    return unless defined?(UI::MIDIDevices)
    ports.each do |p|
      bit = p.transport_id
      next if bit == 0
      key = WebMIDI._key(p.slot)
      # The screen's column headings fit 10 characters: keep the end, which
      # tells "IAC Driver Bus 1" from "... 2" ("IAC Dri..1")
      n = p.name
      label = n.size > 10 ? "#{n[0, 7]}..#{n[-1, 1]}" : n
      UI::MIDIDevices.register_builtin(key, label, bit, color: :green, output: p.output?) { WebMIDI.find(p.name) }
    end
  end
end

module MIDIDevices
  # A Web MIDI port by name (simulator only), or nil
  def self.web_midi(name)
    WebMIDI.find(name)
  end

  # Every Web MIDI port
  def self.web_midis
    WebMIDI.ports
  end
end
