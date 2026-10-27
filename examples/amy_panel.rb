# AMY parts built by touch (Tab5 / CrowPanel)
#
# UI::AMYPanel (docs/AMY_PANEL.md): one tab per AMY part. "+" adds an FM
# (DX7) or a Synth (Juno-60) part on the lowest free MIDI channel; each tab
# has the preset (<< < > >>), the channel (Ch- / Ch+), the envelopes, eight
# knobs and a one-octave keyboard that plays the tab's channel.
#
# UI mode has the same panel on the Home screen ("AMY"); this script starts
# with two parts already on it, and plays whatever comes in on the MIDI IN
# chosen on the MIDI Devices screen.

require 'midi'
require 'ui'

raise "AMY is not available on this board" unless MIDIDevices.amy

AMY.add_synth(channel: 0, voices: 4, patch: 48)   # A61 Piano II
AMY.add_fm(channel: 1, voices: 6, patch: 135)

panel = UI::AMYPanel.new(engine: Proc.new { MIDIDevices.amy })
panel.show

# An external keyboard plays the parts on its channels
input = UI::MIDIDevices.midi_in
MIDI.route(input, MIDIDevices.amy) if input

puts "AMY panel ready."

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
