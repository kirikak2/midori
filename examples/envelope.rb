# AMY envelopes edited by touch (Tab5 / CrowPanel)
#
# UI::Envelope on a layout (docs/ENVELOPE.md): the amp and filter envelopes
# of an AMY::Synth drawn as their ADSR shapes. Drag a breakpoint to change
# it -- A (attack peak) sideways, D (end of decay) sideways and up / down,
# S (end of sustain) up / down, R (end of release) sideways. Put a finger
# anywhere in the box: it takes the nearest point.
#
#   +-------------+-------------+--------+
#   | Amp env     | Filter env  | Cutoff |
#   |             |             | Reso   |
#   +-------------+-------------+--------+
#   | keys (2 octaves)                   |
#   +------------------------------------+
#
# The < > pads step through the Juno-60 presets; the envelopes and knobs
# follow (env.sync) because a patch brings its own.

require 'midi'
require 'ui'

raise "AMY is not available on this board" unless MIDIDevices.amy

syn = AMY.add_synth(channel: 0, voices: 6)
syn.filter_env = 2.5   # octaves, so the filter envelope can be heard

CUTOFF_MIN = 100
CUTOFF_MAX = 8000
RESO_MIN   = 0.7
RESO_MAX   = 8.0

patch = -1   # -1: the initial voice
layout = nil

# -1 is the initial voice, then the presets 0..127, wrapping around
step_patch = Proc.new do |dir|
  patch += dir
  patch = 127 if patch < -1
  patch = -1 if patch > 127
  patch < 0 ? syn.init_voice : (syn.patch = patch)
  syn.filter_env = 2.5 if patch < 0
  layout[:amp].sync
  layout[:filter].sync
  layout[:cutoff].set_value(AMY.unscale(syn.cutoff, CUTOFF_MIN, CUTOFF_MAX, log: true),
                            notify: false)
  layout[:reso].set_value(AMY.unscale(syn.resonance, RESO_MIN, RESO_MAX), notify: false)
  puts "Patch: #{syn.patch_name || 'Init'}"
end

layout = UI.layout("Envelopes", cols: [2, 2, 1], rows: [3, 2]) do
  # to: + env: send each change with syn.amp_envelope / syn.filter_envelope,
  # and start from the synth's current values
  envelope col: 0, row: 0, id: :amp, label: "Amp", color: :green,
           to: syn, env: :amp, max_time: 3000
  envelope col: 1, row: 0, id: :filter, label: "Filter", color: :cyan,
           to: syn, env: :filter, max_time: 3000

  grid col: 2, row: 0, rows: 3 do
    knob row: 0, id: :cutoff, label: "Cutoff", color: :orange,
         value: AMY.unscale(syn.cutoff, CUTOFF_MIN, CUTOFF_MAX, log: true) do |v|
      syn.cutoff = AMY.scale(v, CUTOFF_MIN, CUTOFF_MAX, log: true)
    end
    knob row: 1, id: :reso, label: "Reso", color: :orange,
         value: AMY.unscale(syn.resonance, RESO_MIN, RESO_MAX) do |v|
      syn.resonance = AMY.scale(v, RESO_MIN, RESO_MAX)
    end
    # Juno-60 presets; the envelopes follow (a patch brings its own)
    grid row: 2, cols: 2 do
      pad col: 0, label: "<" do step_patch.call(-1) end
      pad col: 1, label: ">" do step_patch.call(1) end
    end
  end

  keys col: 0..2, row: 1, octaves: 2, low: 48, device: syn.transport
end

# Nothing is sent by defining the widgets; push the starting values once
layout.send_all
layout.show

puts "Envelope editor ready."

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
