require 'midi'
require 'ui'

# Use pre-initialized SAM2695 device from MIDIDevices module
# This works on any board without needing to specify pin numbers
sam = MIDIDevices.sam2695
unless sam
  puts "SAM2695 not available on this board"
  exit
end

device = MIDI::Device.new(sam)

device.program_change(1, channel: 9)

# General MIDI Drum Notes
KICK = 36
SNARE = 38
CLOSED_HH = 42
OPEN_HH = 46
CYMBAL = 49
TOM = 45

# Pad 1: Kick
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  device.trigger(KICK, 127, channel: 9, duration: 100)
end

# Pad 2: Snare
UI.pad(2, label: "Snare", color: :yellow, type: :trigger) do
  device.trigger(SNARE, 127, channel: 9, duration: 100)
end

# Pad 3: Closed Hi-Hat
UI.pad(3, label: "CH", color: :cyan, type: :trigger) do
  device.trigger(CLOSED_HH, 100, channel: 9, duration: 100)
end

# Pad 4: Open Hi-Hat
UI.pad(4, label: "OH", color: :blue, type: :trigger) do
  device.trigger(OPEN_HH, 100, channel: 9, duration: 150)
end

# Pad 5: Cymbal
UI.pad(5, label: "Cymbal", color: :orange, type: :trigger) do
  device.trigger(CYMBAL, 127, channel: 9, duration: 200)
end

# Pad 6: Tom
UI.pad(6, label: "Tom", color: :purple, type: :trigger) do
  device.trigger(TOM, 110, channel: 9, duration: 200)
end

# Main loop - process UI events
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, output: device, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # Empty - just keep processing UI events
end