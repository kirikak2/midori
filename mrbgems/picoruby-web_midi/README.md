# picoruby-web_midi

The page's Web MIDI ports as [picoruby-midi](../picoruby-midi) transports, for
PicoRuby.wasm (the MIDoRI simulator, `sim/`). Not built for the device.

```ruby
MIDIDevices.web_midis                       # every port the page knows of
daw = MIDIDevices.web_midi("IAC")           # by name: exact, else a part of it (case ignored)
MIDI::Device.new(daw).note_on(60, 100)
MIDI.route(MIDIDevices.web_midi("Keystation"), MIDIDevices.sam2695)
MIDI.on(MIDIDevices.web_midi("Keystation"), :note_on) { |m| puts m.note }
```

A script meant for the device too checks first:
`MIDIDevices.respond_to?(:web_midi)`.

Each port takes a dynamic bit of picoruby-midi's transport registry (16 bits
in all), so `MIDI.route`, `MIDI.on`, filters and the MIDI Devices screen work
as with the device's own transports. Its input is a packet source read by
picoruby-midi's input task while something routes from it.

## The page's side

The page owns the `MIDIAccess`. Web MIDI lists inputs and outputs apart; the
page pairs those with the same name into one port (slot, up to 8):

| Call (exported) | |
|---|---|
| `web_midi_port(slot, name, has_input, has_output)` | before the VM starts |
| `web_midi_connected(slot, connected)` | the port opened / closed |
| `web_midi_in(slot, b0, b1, b2, b3)` | a USB-MIDI packet received |
| `Module.webMidiOut(slot, cin, b1, b2, b3, timeUs)` | the VM sends (page side) |

`WebMIDI.register_devices` lists the ports on the MIDI Devices screen; the
simulator's start-up calls it before the saved patches are routed. A port is
keyed by a hash of its name, so patches saved to it come back.
