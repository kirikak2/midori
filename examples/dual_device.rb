# Dual Device Example - Using both SAM2695 and USB-MIDI
# Demonstrates using multiple MIDI devices simultaneously

require 'midi'
require 'ui'

# Get pre-initialized devices
sam = MIDIDevices.sam2695
usb = MIDIDevices.usb_midi_host

# Check which devices are available
sam_available = !sam.nil?
usb_available = !usb.nil?

puts "Board: #{BoardConfig::BOARD_NAME}"
puts "SAM2695: #{sam_available ? 'available' : 'not available'}"
puts "USB-MIDI Host: #{usb_available ? 'available' : 'not available'}"

# Exit if no devices available
unless sam_available || usb_available
  puts "No MIDI devices available on this board"
  exit
end

# Initialize SAM2695 device
if sam_available
  sam_device = MIDI::Device.new(sam)
  sam_device.program_change(0, channel: 0)  # Acoustic Grand Piano
  puts "SAM2695 device initialized"
end

# Initialize USB-MIDI device
if usb_available
  usb_device = MIDI::Device.new(usb)
  usb_device.program_change(1, channel: 0)  # Bright Acoustic Piano
  puts "USB-MIDI device initialized"
end

# Pad 1: Play on SAM2695 (if available)
if sam_available
  UI.pad(1, label: "SAM C4", color: :red, type: :trigger) do
    sam_device.trigger(60, 127, channel: 0, duration: 200)
  end

  UI.pad(2, label: "SAM E4", color: :yellow, type: :trigger) do
    sam_device.trigger(64, 127, channel: 0, duration: 200)
  end

  UI.pad(3, label: "SAM G4", color: :cyan, type: :trigger) do
    sam_device.trigger(67, 127, channel: 0, duration: 200)
  end
end

# Pad 4: Play on USB-MIDI (if available)
if usb_available
  UI.pad(4, label: "USB C5", color: :blue, type: :trigger) do
    usb_device.trigger(72, 127, channel: 0, duration: 200)
  end

  UI.pad(5, label: "USB E5", color: :orange, type: :trigger) do
    usb_device.trigger(76, 127, channel: 0, duration: 200)
  end

  UI.pad(6, label: "USB G5", color: :purple, type: :trigger) do
    usb_device.trigger(79, 127, channel: 0, duration: 200)
  end
end

# Main loop - process UI events
# Use the first available device as the clock output
output_device = sam_device || usb_device
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, output: output_device, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # Empty - just keep processing UI events
end
