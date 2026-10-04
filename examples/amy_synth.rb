# AMY subtractive synth on the built-in speaker (Tab5 / CrowPanel)
#
# Up to four oscillators through one filter and amplifier, with an LFO and
# two ADSR envelopes (AMY::Synth). Starts from the Juno-60 presets (0..127)
# or a plain initial voice, and every knob follows a preset change. Played
# from the screen and, if one is plugged in, from a USB-MIDI keyboard on the
# USB host port (routed to AMY in C with MIDI.route). Use the nav bar arrows
# to move between the three screens.
#
#   Pads    12 notes of C major, held for as long as you press
#   XYPad   X = pitch on a scale with glide, Y = filter cutoff (CC 74)
#   Knobs   bank A  Voice   Patch (Init / Juno name), Cutoff, Reso, Flt Env,
#                           Key Track, Volume, Reverb, Chorus, Pan, Glide,
#                           Velocity, Octave
#           bank B  OSC 1-3 Wave, Level, Octave, Detune for each
#           bank C  OSC 4 + envelopes   Wave, Level, Octave, Detune,
#                           Amp A/D/S/R, Filter A/D/S/R
#           bank D  LFO / FX  LFO Rate, LFO Wave, Vibrato, LFO>Filter,
#                           Tremolo, PWM, Filter type, Duty, Echo, Echo time,
#                           Echo feedback
#
# Cutoff, Reso, Pan and Glide send CCs that syn.map_cc has AMY apply itself
# (a keyboard knob on the same CC does the same); everything else is set on
# the AMY::Synth object directly.
#
# MIDI channel 1 (channel: 0 in Ruby) plays this synth.

require 'midi'
require 'ui'
require 'machine'

raise "AMY is not available on this board" unless MIDIDevices.amy

syn = AMY::Synth.new(channel: 0, voices: 6)   # the initial voice
dev = MIDI::Device.new(syn.transport)

CC_PORTA  = 5
CC_PAN    = 10
CC_RESO   = 71
CC_CUTOFF = 74

# Knob ranges (knob 0..127 <-> parameter)
CUTOFF_MIN = 100
CUTOFF_MAX = 8000
RESO_MIN   = 0.7
RESO_MAX   = 8.0
GLIDE_MAX  = 500
FENV_MAX   = 6.0      # octaves
ATTACK     = [1, 2000]
DECAY      = [10, 4000]
LFO_RATE   = [0.1, 20.0]
VIBRATO_MAX = 0.1     # octaves
LFO_FILTER_MAX = 3.0  # octaves
PWM_MAX    = 0.45
ECHO_TIME  = [20, 743]

# Pad / keyboard state, mutated in place by the knob blocks
play = { velocity: 100, octave: 0 }

# A patch load (or init) rebuilds the synth and drops its CC mappings.
map_ccs = Proc.new do
  syn.map_cc(CC_CUTOFF, :cutoff,    min: CUTOFF_MIN, max: CUTOFF_MAX)
  syn.map_cc(CC_RESO,   :resonance, min: RESO_MIN, max: RESO_MAX)
  syn.map_cc(CC_PAN,    :pan)
  syn.map_cc(CC_PORTA,  :glide,     min: 0, max: GLIDE_MAX)
end

def wave_index(list, name)
  i = list.index(name)
  i.nil? ? 0 : i
end

def set_knob(n, bank, value)
  UI.knob_set(n, value, bank: bank, notify: false)
end

def log_knob(value, range)
  AMY.unscale(value, range[0], range[1], log: true)
end

# OSC knobs: [bank, oscillator, first knob] -- Wave, Level, Octave, Detune
OSC_KNOBS = [[2, 1, 1], [2, 2, 5], [2, 3, 9], [3, 4, 1]]

# Move every knob to the synth's current values (after a preset change).
# notify: false only moves the knobs; nothing is sent.
sync_knobs = Proc.new do
  set_knob(2, 1, AMY.unscale(syn.cutoff, CUTOFF_MIN, CUTOFF_MAX, log: true))
  set_knob(3, 1, AMY.unscale(syn.resonance, RESO_MIN, RESO_MAX))
  set_knob(4, 1, AMY.unscale(syn.filter_env, 0, FENV_MAX))
  set_knob(5, 1, AMY.unscale(syn.key_track, 0, 1))
  set_knob(6, 1, AMY.unscale(syn.volume, 0, 1))
  set_knob(7, 1, AMY.unscale(syn.reverb, 0, 1))
  set_knob(8, 1, AMY.unscale(syn.chorus, 0, 1))
  set_knob(9, 1, AMY.unscale(syn.pan, 0, 1))
  set_knob(10, 1, AMY.unscale(syn.glide, 0, GLIDE_MAX))

  OSC_KNOBS.each do |bank, n, first|
    o = syn.osc(n)
    set_knob(first, bank, wave_index(AMY::Synth::WAVE_LIST, o.wave))
    UI.knob_label(first, "Osc#{n} #{o.wave}", bank: bank)
    set_knob(first + 1, bank, AMY.unscale(o.level, 0, 1))
    set_knob(first + 2, bank, o.octave)
    set_knob(first + 3, bank, o.detune)
  end

  set_knob(5, 3, log_knob(syn.amp_attack, ATTACK))
  set_knob(6, 3, log_knob(syn.amp_decay, DECAY))
  set_knob(7, 3, AMY.unscale(syn.amp_sustain, 0, 1))
  set_knob(8, 3, log_knob(syn.amp_release, DECAY))
  set_knob(9, 3, log_knob(syn.filter_attack, ATTACK))
  set_knob(10, 3, log_knob(syn.filter_decay, DECAY))
  set_knob(11, 3, AMY.unscale(syn.filter_sustain, 0, 1))
  set_knob(12, 3, log_knob(syn.filter_release, DECAY))

  set_knob(1, 4, log_knob(syn.lfo.rate, LFO_RATE))
  set_knob(2, 4, wave_index(AMY::Synth::LFO_WAVE_LIST, syn.lfo.wave))
  UI.knob_label(2, "LFO #{syn.lfo.wave}", bank: 4)
  set_knob(3, 4, AMY.unscale(syn.lfo.vibrato, 0, VIBRATO_MAX))
  set_knob(4, 4, AMY.unscale(syn.lfo.filter, 0, LFO_FILTER_MAX))
  set_knob(5, 4, AMY.unscale(syn.lfo.tremolo, 0, 1))
  set_knob(6, 4, AMY.unscale(syn.lfo.pwm, 0, PWM_MAX))
  set_knob(7, 4, wave_index(AMY::Synth::FILTER_LIST, syn.filter))
  UI.knob_label(7, "Filter #{syn.filter}", bank: 4)
  set_knob(8, 4, AMY.unscale(syn.osc(1).duty, 0.05, 0.95))
  set_knob(9, 4, AMY.unscale(syn.echo_level, 0, 1))
  set_knob(10, 4, log_knob(syn.echo_delay, ECHO_TIME))
  set_knob(11, 4, AMY.unscale(syn.echo_feedback, 0, 1))
end

# ---- Knobs, bank A: voice ---------------------------------------------------
# Patch: -1 is the initial voice, 0..127 the Juno-60 presets.
UI.knob(1, bank: 1, label: "Init", color: :yellow, min: -1, max: 127, value: -1) do |v|
  n = v.to_i
  current = syn.patch.nil? ? -1 : syn.patch
  if n != current
    if n < 0
      syn.init_voice
      syn.refresh
    else
      syn.patch = n            # also reads the preset's values back from AMY
    end
    map_ccs.call
    UI.knob_label(1, n < 0 ? "Init" : AMY.patch_name(n), bank: 1)
    sync_knobs.call
  end
end
UI.knob(2, bank: 1, label: "Cutoff", color: :cyan, value: 100) do |v|
  dev.control_change(CC_CUTOFF, v.to_i)
end
UI.knob(3, bank: 1, label: "Reso", color: :cyan, value: 0) do |v|
  dev.control_change(CC_RESO, v.to_i)
end
UI.knob(4, bank: 1, label: "Flt Env", color: :cyan, value: 0) do |v|
  syn.filter_env = v / 127.0 * FENV_MAX
end
UI.knob(5, bank: 1, label: "Key Track", color: :cyan, value: 0) do |v|
  syn.key_track = v / 127.0
end
UI.knob(6, bank: 1, label: "Volume", color: :green, value: 127) do |v|
  syn.volume = v / 127.0
end
UI.knob(7, bank: 1, label: "Reverb", color: :blue, value: 0) do |v|
  syn.reverb = v / 127.0
end
UI.knob(8, bank: 1, label: "Chorus", color: :blue, value: 0) do |v|
  syn.chorus = v / 127.0
end
UI.knob(9, bank: 1, label: "Pan", color: :purple, origin: :center, value: 64) do |v|
  dev.control_change(CC_PAN, v.to_i)
end
UI.knob(10, bank: 1, label: "Glide", color: :purple, value: 0) do |v|
  dev.control_change(CC_PORTA, v.to_i)
end
UI.knob(11, bank: 1, label: "Velocity", color: :red, min: 1, max: 127, value: 100) do |v|
  play[:velocity] = v.to_i
end
# Knob 12 (Octave) is defined after the pads and the XYPad, which it shifts.

# ---- Knobs, banks B / C: oscillators ----------------------------------------
waves = AMY::Synth::WAVE_LIST
OSC_KNOBS.each do |bank, n, first|
  UI.knob(first, bank: bank, label: "Osc#{n} Wave", color: :yellow,
          min: 0, max: waves.size - 1, value: 0) do |v|
    w = waves[v.to_i]
    if w != syn.osc(n).wave
      syn.osc(n).wave = w
      UI.knob_label(first, "Osc#{n} #{w}", bank: bank)
    end
  end
  UI.knob(first + 1, bank: bank, label: "Osc#{n} Level", color: :green, value: 0) do |v|
    syn.osc(n).level = v / 127.0
  end
  UI.knob(first + 2, bank: bank, label: "Osc#{n} Oct", color: :orange,
          min: -2, max: 2, origin: :center, value: 0) do |v|
    oct = v.to_i
    syn.osc(n).octave = oct if oct != syn.osc(n).octave
  end
  UI.knob(first + 3, bank: bank, label: "Osc#{n} Detune", color: :orange,
          min: -50, max: 50, origin: :center, value: 0) do |v|
    syn.osc(n).detune = v.to_i
  end
end

# ---- Knobs, bank C: envelopes ------------------------------------------------
UI.knob(5, bank: 3, label: "Amp A", color: :red, value: 10) do |v|
  syn.amp_attack = AMY.scale(v, ATTACK[0], ATTACK[1], log: true)
end
UI.knob(6, bank: 3, label: "Amp D", color: :red, value: 60) do |v|
  syn.amp_decay = AMY.scale(v, DECAY[0], DECAY[1], log: true)
end
UI.knob(7, bank: 3, label: "Amp S", color: :red, value: 100) do |v|
  syn.amp_sustain = v / 127.0
end
UI.knob(8, bank: 3, label: "Amp R", color: :red, value: 60) do |v|
  syn.amp_release = AMY.scale(v, DECAY[0], DECAY[1], log: true)
end
UI.knob(9, bank: 3, label: "Flt A", color: :cyan, value: 10) do |v|
  syn.filter_attack = AMY.scale(v, ATTACK[0], ATTACK[1], log: true)
end
UI.knob(10, bank: 3, label: "Flt D", color: :cyan, value: 70) do |v|
  syn.filter_decay = AMY.scale(v, DECAY[0], DECAY[1], log: true)
end
UI.knob(11, bank: 3, label: "Flt S", color: :cyan, value: 40) do |v|
  syn.filter_sustain = v / 127.0
end
UI.knob(12, bank: 3, label: "Flt R", color: :cyan, value: 60) do |v|
  syn.filter_release = AMY.scale(v, DECAY[0], DECAY[1], log: true)
end

# ---- Knobs, bank D: LFO, filter type, duty, echo ----------------------------
lfo_waves = AMY::Synth::LFO_WAVE_LIST
filters = AMY::Synth::FILTER_LIST
UI.knob(1, bank: 4, label: "LFO Rate", color: :magenta, value: 64) do |v|
  syn.lfo.rate = AMY.scale(v, LFO_RATE[0], LFO_RATE[1], log: true)
end
UI.knob(2, bank: 4, label: "LFO Wave", color: :magenta,
        min: 0, max: lfo_waves.size - 1, value: 1) do |v|
  w = lfo_waves[v.to_i]
  if w != syn.lfo.wave
    syn.lfo.wave = w
    UI.knob_label(2, "LFO #{w}", bank: 4)
  end
end
UI.knob(3, bank: 4, label: "Vibrato", color: :magenta, value: 0) do |v|
  syn.lfo.vibrato = v / 127.0 * VIBRATO_MAX
end
UI.knob(4, bank: 4, label: "LFO>Filter", color: :magenta, value: 0) do |v|
  syn.lfo.filter = v / 127.0 * LFO_FILTER_MAX
end
UI.knob(5, bank: 4, label: "Tremolo", color: :magenta, value: 0) do |v|
  syn.lfo.tremolo = v / 127.0
end
UI.knob(6, bank: 4, label: "PWM", color: :magenta, value: 0) do |v|
  syn.lfo.pwm = v / 127.0 * PWM_MAX
end
UI.knob(7, bank: 4, label: "Filter", color: :cyan,
        min: 0, max: filters.size - 1, value: 2) do |v|
  f = filters[v.to_i]
  if f != syn.filter
    syn.filter = f
    UI.knob_label(7, "Filter #{f}", bank: 4)
  end
end
UI.knob(8, bank: 4, label: "Duty", color: :yellow, value: 64) do |v|
  d = AMY.scale(v, 0.05, 0.95)
  syn.oscs.each { |o| o.duty = d }
end
UI.knob(9, bank: 4, label: "Echo", color: :blue, value: 0) do |v|
  syn.echo_level = v / 127.0
end
UI.knob(10, bank: 4, label: "Echo Time", color: :blue, value: 90) do |v|
  syn.echo_delay = AMY.scale(v, ECHO_TIME[0], ECHO_TIME[1], log: true).to_i
end
UI.knob(11, bank: 4, label: "Echo FB", color: :blue, value: 50) do |v|
  syn.echo_feedback = v / 127.0 * 0.9
end

map_ccs.call
syn.refresh
sync_knobs.call   # start on the initial voice's values

# ---- Pads: hold to play -----------------------------------------------------
SCALE = [60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79]   # C4 .. G5
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
COLORS = [:red, :orange, :yellow, :green, :cyan, :blue, :purple, :magenta]
held = {}

def note_label(note)
  "#{NOTE_NAMES[note % 12]}#{note / 12 - 1}"
end

SCALE.each_with_index do |base, i|
  UI.pad(i + 1, label: note_label(base), color: COLORS[i % COLORS.size], type: :momentary) do |on|
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

# ---- XYPad --------------------------------------------------------------------
# AMY's pitch bend range is fixed at +-2 semitones, so glide_range is 2.
XY_SCALE = [48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72]   # C pentatonic, C3..C6
xy = UI::XYPad.new(
  scale: XY_SCALE,
  glide_range: 2,
  y_cc: CC_CUTOFF,
  y_range: 0..127,
  velocity: 100,
  device: dev
)

# ---- Octave (knob 12, bank A) -------------------------------------------------
UI.knob(12, bank: 1, label: "Octave", color: :red, min: -2, max: 2,
        origin: :center, value: 0) do |v|
  oct = v.to_i
  if oct != play[:octave]
    play[:octave] = oct
    SCALE.each_with_index { |base, i| UI.pad_label(i + 1, note_label(base + 12 * oct)) }
    shifted = XY_SCALE.map { |n| n + 12 * oct }
    s = 1
    while s <= UI::XYPad::MAX_TOUCHES
      xy.slot(s, scale: shifted)
      s += 1
    end
  end
end

# ---- USB-MIDI keyboard (optional) -------------------------------------------
# Routed to AMY in C; mapped CCs (74 cutoff, 71 reso, 10 pan, 5 glide) work
# from the keyboard's knobs too. CC 73 / 72 (attack / release on many
# keyboards) set the amp envelope from Ruby.
usb = MIDIDevices.usb_midi_host
if usb
  MIDI.route(usb, syn.transport)
  input = MIDI::Input.new(MIDI::Device.new(usb))
  input.on(:control_change) do |e|
    case e[:cc]
    when 73 then syn.amp_attack  = AMY.scale(e[:value], ATTACK[0], ATTACK[1], log: true)
    when 72 then syn.amp_release = AMY.scale(e[:value], DECAY[0], DECAY[1], log: true)
    end
  end
end

UI.knobs
puts "AMY Synth ready. Render load is printed every 5 s."

# Main loop: MIDI.bpm_loop, not `loop do` (a bare loop never lets the VM
# switch tasks, so UI and MIDI events would not be handled).
last_report = 0
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, output: nil, subdivisions: 48, send_start: false, on_loop: on_loop) do |c|
  now = Machine.uptime_us / 1000
  if now - last_report >= 5000
    last_report = now
    puts "AMY render load #{(AMY.render_load * 100).to_i}%, overloads #{AMY.overloads}"
  end
end
