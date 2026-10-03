# AMY FM synth on the built-in speaker (Tab5 / CrowPanel)
#
# A DX7-style FM synth played from the screen and, if one is plugged in,
# from a USB-MIDI keyboard on the USB host port. Use the nav bar arrows to
# move between the three screens.
#
#   Pads    12 notes of C major, held for as long as you press (note_on on
#           press, note_off on release)
#   XYPad   X = pitch on a scale with glide, Y = filter cutoff (CC 74)
#   Knobs   bank A  Voice      Patch (shows the preset name), Algo, Feedback,
#                              Cutoff, Reso, Volume, Reverb, Chorus, Pan,
#                              Glide, Velocity, Octave
#           bank B  Op 1 / 2   Level, Ratio, Attack, Decay, Sustain, Release
#           bank C  Op 3 / 4   same
#
# Two ways a knob reaches AMY, depending on the parameter:
#
#   * CC-mappable (cutoff, resonance, feedback, pan, glide): fm.map_cc tells
#     AMY to apply the CC itself, and the knob just sends a CC -- the same
#     code would drive any other synth, and a keyboard's knob sending that CC
#     works the same way. The XYPad's Y axis rides on this too (CC 74).
#   * Not CC-mappable (patch, algorithm, operator envelopes, ...): the knob
#     sets the parameter on the AMY::FM object directly.
#
# Operator knobs are only sent once touched. Touching an envelope stage
# replaces that operator's preset (DX7) envelope with a plain ADSR.
#
# MIDI channel 1 (channel: 0 in Ruby) plays this synth.

require 'midi'
require 'ui'
require 'machine'

raise "AMY is not available on this board" unless MIDIDevices.amy

FIRST_PATCH = 128   # DX7 presets: 128..255
fm  = AMY::FM.new(channel: 0, voices: 6, patch: FIRST_PATCH)
dev = MIDI::Device.new(fm.synth)

CC_PORTA    = 5
CC_PAN      = 10
CC_FEEDBACK = 20
CC_RESO     = 71
CC_CUTOFF   = 74

# Knob numbers on bank A that are re-sent after a patch change
K_CUTOFF = 4
K_RESO   = 5
K_VOLUME = 6
K_PAN    = 9
K_GLIDE  = 10

# Pad / keyboard state, mutated in place by the knob blocks
play = { velocity: 100, octave: 0 }

# ---- CC mappings ----------------------------------------------------------
# A patch load rebuilds the synth, so these (and the filter they switch on)
# are set again after every patch change.
map_ccs = Proc.new do
  fm.map_cc(CC_CUTOFF,   :filter_freq, min: 200, max: 8000)
  fm.map_cc(CC_RESO,     :resonance,   min: 0.7, max: 6.0)
  fm.map_cc(CC_FEEDBACK, :feedback)
  fm.map_cc(CC_PAN,      :pan)
  fm.map_cc(CC_PORTA,    :portamento,  min: 0, max: 500)
end

# Put the voice-level knobs (filter, volume, pan, glide) back on the new patch.
# Operator and algorithm knobs are left alone: the preset defines those.
restore_voice = Proc.new do
  dev.control_change(CC_CUTOFF, UI.knob_value(K_CUTOFF, bank: 1).to_i)
  dev.control_change(CC_RESO,   UI.knob_value(K_RESO,   bank: 1).to_i)
  dev.control_change(CC_PAN,    UI.knob_value(K_PAN,    bank: 1).to_i)
  dev.control_change(CC_PORTA,  UI.knob_value(K_GLIDE,  bank: 1).to_i)
  fm.volume = UI.knob_value(K_VOLUME, bank: 1) / 127.0
end

# ---- Knobs, bank A: voice ----------------------------------------------------
UI.knob(1, bank: 1, label: AMY.patch_name(FIRST_PATCH), color: :yellow,
        min: FIRST_PATCH, max: 255, value: FIRST_PATCH) do |v|
  n = v.to_i
  if n != fm.patch
    fm.patch = n
    map_ccs.call
    restore_voice.call
    UI.knob_label(1, AMY.patch_name(n), bank: 1)
  end
end
UI.knob(2, bank: 1, label: "Algo", color: :yellow, min: 1, max: 32, value: 1) do |v|
  fm.algorithm = v.to_i
end
UI.knob(3, bank: 1, label: "Feedback", color: :orange, value: 0) do |v|
  dev.control_change(CC_FEEDBACK, v.to_i)
end
UI.knob(K_CUTOFF, bank: 1, label: "Cutoff", color: :cyan, value: 127) do |v|
  dev.control_change(CC_CUTOFF, v.to_i)
end
UI.knob(K_RESO, bank: 1, label: "Reso", color: :cyan, value: 0) do |v|
  dev.control_change(CC_RESO, v.to_i)
end
UI.knob(K_VOLUME, bank: 1, label: "Volume", color: :green, value: 100) do |v|
  fm.volume = v / 127.0
end
UI.knob(7, bank: 1, label: "Reverb", color: :blue, value: 25) do |v|
  fm.reverb = v / 127.0
end
UI.knob(8, bank: 1, label: "Chorus", color: :blue, value: 0) do |v|
  fm.chorus = v / 127.0
end
UI.knob(K_PAN, bank: 1, label: "Pan", color: :purple, origin: :center, value: 64) do |v|
  dev.control_change(CC_PAN, v.to_i)
end
UI.knob(K_GLIDE, bank: 1, label: "Glide", color: :purple, value: 0) do |v|
  dev.control_change(CC_PORTA, v.to_i)
end
UI.knob(11, bank: 1, label: "Velocity", color: :red, min: 1, max: 127, value: 100) do |v|
  play[:velocity] = v.to_i
end
UI.knob(12, bank: 1, label: "Octave", color: :red, min: -2, max: 2,
        origin: :center, value: 0) do |v|
  play[:octave] = v.to_i
end

# ---- Knobs, banks B / C: operators 1-2 and 3-4 (DX7 numbering) -----------
[[2, 1, 1], [2, 2, 7], [3, 3, 1], [3, 4, 7]].each do |bank, n, first|
  UI.knob(first, bank: bank, label: "Op#{n} Level", color: :green, value: 100) do |v|
    fm.op(n).level = v / 127.0
  end
  UI.knob(first + 1, bank: bank, label: "Op#{n} Ratio", color: :yellow, value: 32) do |v|
    fm.op(n).ratio = AMY.scale(v, 0.5, 16, log: true)
  end
  UI.knob(first + 2, bank: bank, label: "Op#{n} Attack", color: :cyan, value: 10) do |v|
    fm.op(n).attack = AMY.scale(v, 1, 2000, log: true)
  end
  UI.knob(first + 3, bank: bank, label: "Op#{n} Decay", color: :cyan, value: 60) do |v|
    fm.op(n).decay = AMY.scale(v, 10, 4000, log: true)
  end
  UI.knob(first + 4, bank: bank, label: "Op#{n} Sustain", color: :cyan, value: 90) do |v|
    fm.op(n).sustain = v / 127.0
  end
  UI.knob(first + 5, bank: bank, label: "Op#{n} Release", color: :cyan, value: 60) do |v|
    fm.op(n).release = AMY.scale(v, 10, 4000, log: true)
  end
end

map_ccs.call
restore_voice.call
fm.reverb = UI.knob_value(7, bank: 1) / 127.0

# ---- Pads: hold to play ---------------------------------------------------
# The note sounding on each pad is remembered, so releasing it ends the right
# note even if the Octave knob moved while it was held.
SCALE = [60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79]   # C4 .. G5
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
COLORS = [:red, :orange, :yellow, :green, :cyan, :blue, :purple, :magenta]
held = {}

SCALE.each_with_index do |base, i|
  label = "#{NOTE_NAMES[base % 12]}#{base / 12 - 1}"
  UI.pad(i + 1, label: label, color: COLORS[i % COLORS.size], type: :momentary) do |on|
    if on
      note = base + 12 * play[:octave]
      held[i] = note
      dev.note_on(note, play[:velocity])
    elsif held[i]
      dev.note_off(held[i])
      held.delete(i)
    end
  end
end

# ---- XYPad ------------------------------------------------------------------
# X snaps to the scale and glides by pitch bend; AMY's bend range is fixed at
# +-2 semitones, so glide_range must be 2 for the glide to land in tune. Pitch
# bend is per channel (and global inside AMY), so several fingers gliding at
# once bend each other. Y sends CC 74, which the mapping above turns into
# the filter cutoff.
xy = UI::XYPad.new(
  scale: [48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72],   # C pentatonic, C3..C6
  glide_range: 2,
  y_cc: CC_CUTOFF,
  y_range: 0..127,
  velocity: 100,
  device: dev
)

# ---- USB-MIDI keyboard (optional) ---------------------------------------
# Notes and CCs are passed on to AMY from Ruby here; CC 73 / 72 (attack /
# release on many keyboards) set operator 1's envelope directly.
# The input registers itself with MIDI, and MIDI.bpm_loop below processes
# it on every pass.
usb = MIDIDevices.usb_midi_host
if usb
  input = MIDI::Input.new(MIDI::Device.new(usb))
  input.on(:note_on)  { |e| dev.note_on(e[:note], e[:velocity]) }
  input.on(:note_off) { |e| dev.note_off(e[:note]) }
  input.on(:pitch_bend) { |e| dev.pitch_bend(e[:value]) if e[:value] }
  input.on(:control_change) do |e|
    case e[:cc]
    when 73 then fm.op(1).attack  = AMY.scale(e[:value], 1, 2000, log: true)
    when 72 then fm.op(1).release = AMY.scale(e[:value], 10, 4000, log: true)
    else dev.control_change(e[:cc], e[:value])   # mapped CCs land in AMY
    end
  end
end

UI.knobs
puts "AMY FM ready (#{AMY.patch_name(fm.patch)}). Render load is printed every 5 s."

# Main loop. MIDI.bpm_loop, not `loop do`: a bare loop never lets the VM
# switch tasks, so UI and MIDI events would not be handled. output: nil --
# there is no clock to send (AMY is not synced to MIDI clock), so the block
# just runs every subdivision: 120 BPM x 48 = about every 10 ms.
# Midori's bpm_loop also stops on the Scripts screen's [Stop].
last_report = 0
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, output: nil, subdivisions: 48, send_start: false, on_loop: on_loop) do |c|
  now = Machine.uptime_us / 1000
  if now - last_report >= 5000
    last_report = now
    puts "AMY render load #{(AMY.render_load * 100).to_i}%, overloads #{AMY.overloads}"
  end
end
