# AMY render benchmark (Tab5 / CrowPanel)
#
# Holds chords of growing size on an FM synth (DX7 preset) and on a
# subtractive synth (Juno preset), and prints AMY.perf for each step: how
# long the audio task took to render a 5.8 ms block (avg / min / max),
# blocks over budget, audible oscs and a load histogram. Built with
# CONFIG_USB_MIDI_AMY_PROFILE, each report also breaks the render down by
# AMY stage.
#
# Run it with nothing else playing and the screen left alone: the timing is
# wall time on the audio task's core, so the UI drawing counts towards it.
# Takes about a minute; the console Stop ends it between steps.

require 'midi'

raise "AMY is not available on this board" unless MIDIDevices.amy

MEASURE_MS = 3000
SETTLE_MS  = 300    # let the note-ons land before the window opens
RELEASE_MS = 1500   # let the release tails die out between steps
CHORD = [48, 55, 60, 64, 67, 72]
VOICE_STEPS = [1, 2, 4, 6]

$sm = ScriptManager.new

def stopped?
  $sm.stop_requested?
end

def measure(label)
  sleep_ms SETTLE_MS
  AMY.perf_reset
  sleep_ms MEASURE_MS
  puts "== #{label}"
  puts AMY.perf
end

def hold_chords(label, dev, channel)
  VOICE_STEPS.each do |n|
    return if stopped?
    notes = CHORD[0, n]
    notes.each { |note| dev.note_on(note, 100, channel: channel) }
    measure("#{label}, #{n} notes")
    notes.each { |note| dev.note_off(note, channel: channel) }
    sleep_ms RELEASE_MS
  end
end

AMY.start
measure("idle (no synth)")

fm = AMY.add_fm(channel: 0, voices: 6, patch: 128)
fm_dev = MIDI::Device.new(fm.transport)
hold_chords("FM patch 128", fm_dev, 0)

unless stopped?
  fm.reverb = 0.5
  CHORD.each { |note| fm_dev.note_on(note, 100, channel: 0) }
  measure("FM patch 128, 6 notes + reverb")
  CHORD.each { |note| fm_dev.note_off(note, channel: 0) }
  fm.reverb = 0.0
  sleep_ms RELEASE_MS
end

unless stopped?
  AMY.reset
  syn = AMY.add_synth(channel: 1, voices: 6, patch: 0)
  hold_chords("Juno patch 0", MIDI::Device.new(syn.transport), 1)
end

puts "AMY benchmark done"
