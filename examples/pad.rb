# pad.rb
#
# 画面のパッドでドラムを叩く。音は MIDI Devices 画面で選んだ OUT
# （UI::MIDIDevices.midi_out）へ送るので、どの音源で鳴らすかは画面の
# Prog(output) 行でいつでも変えられる。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# OUT が未選択なら SAM2695（なければ PC）にする。画面で選び直せる
md.midi_out ||= MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

DRUM_CH = 9   # General MIDI のドラムは ch 10
md.midi_out.program_change(1, channel: DRUM_CH)

# General MIDI Drum Notes
KICK = 36
SNARE = 38
CLOSED_HH = 42
OPEN_HH = 46
CYMBAL = 49
TOM = 45

# OUT は画面で変わるので、叩くたびに md.midi_out を聞き直す
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  md.midi_out&.trigger(KICK, 127, channel: DRUM_CH, duration: 100)
end

UI.pad(2, label: "Snare", color: :yellow, type: :trigger) do
  md.midi_out&.trigger(SNARE, 127, channel: DRUM_CH, duration: 100)
end

UI.pad(3, label: "CH", color: :cyan, type: :trigger) do
  md.midi_out&.trigger(CLOSED_HH, 100, channel: DRUM_CH, duration: 100)
end

UI.pad(4, label: "OH", color: :blue, type: :trigger) do
  md.midi_out&.trigger(OPEN_HH, 100, channel: DRUM_CH, duration: 150)
end

UI.pad(5, label: "Cymbal", color: :orange, type: :trigger) do
  md.midi_out&.trigger(CYMBAL, 127, channel: DRUM_CH, duration: 200)
end

UI.pad(6, label: "Tom", color: :purple, type: :trigger) do
  md.midi_out&.trigger(TOM, 110, channel: DRUM_CH, duration: 200)
end

UI.pads

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
