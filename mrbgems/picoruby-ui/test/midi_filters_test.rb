# Host tests for named filters on the MIDI Devices screen
# (mrblib/ui_midi_filters.rb): the filters defined on the C side, chains
# mirrored there, the screen's changes applied, and a filter's panel made
# and taken down with its chain. Both gems' C sides are stubbed: the screen's
# model (ui_filters.cpp) is a small Ruby imitation.
#
#   ruby test/midi_filters_test.rb          (from the picoruby-ui directory)

midi = File.expand_path("../../picoruby-midi", File.dirname(__FILE__))
load File.join(midi, "host_test/filter_stubs.rb")
%w[midi_constants midi_device midi_filter midi_route midi_input midi_clock
   midi_filter_class].each { |f| load File.join(midi, "mrblib/#{f}.rb") }

module MIDI
  class << self
    def _transport_connected?(bit); true; end
    def _route_get(i)
      r = $stub[:routes][i]
      r ? [r[0], r[1], r[2], r[3]] : nil
    end
    def _route_filtered(src); 0; end
  end
end

# The screens' C models, reduced to what Ruby sees
$devs = []        # [bit, flags, key, name]
$serial = 0
$fdefs = {}       # name => [label, params text]
$fchains = {}     # [src, dst] => [rev, text]
$frev = 0
$fserial = 0
$saves = []       # [src, dst, text, save] of each _filter_chain_set
$requested = [0, -1]
$shown = []

module UI
  PAD_TYPE_TRIGGER = 0
  PAD_TYPE_MOMENTARY = 1
  PAD_TYPE_TOGGLE = 2
  KNOB_ORIGIN_MIN = 0
  KNOB_ORIGIN_CENTER = 1
  SCREEN_LAYOUT = 9
  $calls = []
  $knob_values = {}
  $labels = {}

  class << self
    def log(text); puts text; end
    def rgb(r, g, b); 1; end
    def color_to_rgb565(c); 2; end
    def type_to_constant(t); { trigger: 0, momentary: 1, toggle: 2 }[t] || 0; end
    def on(type, &b); end
    def show(s); $shown << s; true; end
    def _requested_screen; $requested; end

    def _display; [320, 240, 0, 20, 320, 180, false, 6]; end
    def _layout_begin(slot, title); $calls << [:begin, slot, title]; @next_id = 0; true; end
    def _layout_add_pad(*a); $calls << [:pad] + a; @next_id += 1; @next_id - 1; end
    def _layout_add_knob(*a); $calls << [:knob] + a; @next_id += 1; @next_id - 1; end
    def _layout_add_view(*a); @next_id += 1; @next_id - 1; end
    def _layout_commit; true; end
    def _layout_delete(slot); $calls << [:delete, slot]; end
    def _lw_set_label(slot, id, text); $labels[[slot, id]] = text; end
    def _lw_knob_value(slot, id); $knob_values[[slot, id]] || 0.0; end
    def _lw_knob_set_value(slot, id, v)
      changed = $knob_values[[slot, id]] != v
      $knob_values[[slot, id]] = v
      changed
    end

    def _mididev_register(key, name, bit, color, builtin)
      $devs << [bit, builtin ? 1 : 0, builtin ? key : "", name]
      $serial += 1
      $devs.size - 1
    end
    def _mididev_activate(key, bit); true; end
    def _mididev_count; $devs.size; end
    def _mididev_get(i); $devs[i] ? $devs[i].dup : nil; end
    def _mididev_selected(out); -1; end
    def _mididev_restore; 0; end
    def _mididev_take_wanted; nil; end
    def _mididev_serial; $serial; end
    def _tempo_bit; 0x40; end

    def _filter_define(name, label, params); $fdefs[name] = [label, params]; $fserial += 1; true; end
    def _filter_undefine(name); $fserial += 1; !$fdefs.delete(name).nil?; end
    def _filter_serial; $fserial; end
    def _filter_chains
      $fchains.map { |k, v| [k[0], k[1], v[0], v[1]] }
    end
    def _filter_chain_set(src, dst, text, save)
      $saves << [src, dst, text, save]
      $fserial += 1
      if text.empty?
        $fchains.delete([src, dst])
        $frev += 1
        return $frev
      end
      $frev += 1
      $fchains[[src, dst]] = [$frev, text]
      $frev
    end
  end
end

load File.expand_path("../mrblib/ui_layout.rb", File.dirname(__FILE__))
load File.expand_path("../mrblib/ui_tempo.rb", File.dirname(__FILE__))
load File.expand_path("../mrblib/ui_midi_devices.rb", File.dirname(__FILE__))
load File.expand_path("../mrblib/ui_midi_filters.rb", File.dirname(__FILE__))

$passed = 0
$failed = 0

def check(name, expected, actual)
  if expected == actual
    $passed += 1
  else
    $failed += 1
    puts "FAIL #{name}: expected #{expected.inspect}, got #{actual.inspect}"
  end
end

class FakeTransport
  attr_reader :transport_id
  def initialize(bit, info); @transport_id = bit; @info = info; end
  def device_info; @info; end
  def connected?; true; end
end

M = UI::MIDIDevices
F = UI::MIDIFilters
HOST = FakeTransport.new(0x01, "USB host")
AMY  = FakeTransport.new(0x08, "AMY")

# Screen side: the user changed a chain (as ui_filter_chain_set would)
def screen_sets(src, dst, text)
  $frev += 1
  if text.empty?
    $fchains.delete([src, dst])
  else
    $fchains[[src, dst]] = [$frev, text]
  end
  $fserial += 1
  M._poll
end

class Octave < MIDI::Filter
  label "Octave"
  types :note_on, :note_off
  param :by, :int, range: -2..2, default: 1, label: "By"
  param :mode, :enum, choices: [:up, :down]
  param :on, :bool, default: true

  def call(m)
    m.note += 12 * params[:by]
    m
  end

  def layout(ui)
    ui.layout(cols: 3, rows: 1) do
      param_knob :by, col: 0
      param_pad :mode, col: 1
      param_pad :on, col: 2
    end
  end
end

class Plain < MIDI::Filter
  types :note_on
end

# --- Text --------------------------------------------------------------------

check "parse", [[:oct, { by: 1, mode: 0 }], [:plain, {}]], F.parse("oct(by:1,mode:0),plain")
check "parse negative", [[:oct, { by: -2 }]], F.parse("oct(by:-2)")
check "parse empty", [], F.parse("")

# --- Defined before install (from /sd/plugins), then installed by boot ------

MIDI.register_filter(:oct, Octave.new(by: 2))
M.register_builtin(:usb_host, "J-6", 0x01) { HOST }
M.register_builtin(:amy, "AMY", 0x08) { AMY }
check "nothing defined before install", {}, $fdefs
M.boot
check "install defines what is registered", ["Octave", "by|By|0|-2|2|2|1|\nmode|mode|1|0|1|0|1|up;down\non|on|2|0|1|1|1|"],
      $fdefs["oct"]
MIDI.register_filter(:plain, Plain.new)
check "registering later defines too", true, $fdefs.key?("plain")
MIDI.unregister_filter(:plain)
check "unregister undefines", false, $fdefs.key?("plain")
MIDI.register_filter(:plain, Plain.new)

# --- A script's chain is mirrored --------------------------------------------

$saves.clear
M.patch(HOST, AMY, filters: [:oct])
check "patch(filters:) chains", [Octave], MIDI.chain_of(HOST, AMY).map { |f| f.class }
check "...mirrored as text", [0x01, 0x08, "oct(by:2,mode:0,on:1)", false], $saves.last
check "patches list the names", [:oct], M.patches[0][:filters]

# --- Its panel ----------------------------------------------------------------

octave = MIDI.chain_of(HOST, AMY)[0]
panel = $ui_filter_panels[octave.object_id]
check "an active filter has a panel", "Octave J-6>AMY", panel && panel.title
check "...a knob for :int", [:knob, "By", -2.0, 2.0, 2.0], [$calls[1][0], $calls[1][5], $calls[1][7], $calls[1][8], $calls[1][10]]
check "...a pad showing the enum", "mode up", $calls[2][5]

# Turning the knob sets the parameter; the model gets it as a panel's value
$saves.clear
panel[:param_by].dispatch({ value: -1.0 })
check "knob sets the param", -1, octave.params[:by]
check "...mirrored, saved later", [0x01, 0x08, "oct(by:-1,mode:0,on:1)", true], $saves.last

# The enum pad steps and relabels
panel[:param_mode].dispatch({ phase: :press, state: true })
check "enum pad steps", :down, octave.params[:mode]
check "...relabels", "mode down", $labels[[panel.slot, 1]]

# --- The screen changes the chain ---------------------------------------------

key = [0x01, 0x08]
screen_sets(0x01, 0x08, "oct(by:1,mode:0,on:0)")
check "a screen value reaches the same instance", [true, 1, false],
      [MIDI.chain_of(HOST, AMY)[0].equal?(octave), octave.params[:by], octave.params[:on]]
check "...the knob shows it", 1.0, panel[:param_by].value
rev = $fchains[key][0]
$saves.clear
M._poll
check "nothing again for the same revision", [], $saves

screen_sets(0x01, 0x08, "plain,oct(by:1)")
check "the screen adds a filter in front", [Plain, Octave], MIDI.chain_of(HOST, AMY).map { |f| f.class }
check "...the instance that stays is kept", true, MIDI.chain_of(HOST, AMY)[1].equal?(octave)
check "...the panel too", panel, $ui_filter_panels[octave.object_id]

# Values out of range come back kept in range
screen_sets(0x01, 0x08, "oct(by:9)")
check "applied in range", 2, octave.params[:by]
check "...and mirrored back so", "oct(by:2,mode:0,on:0)", $fchains[key][1]

# --- Taken off ---------------------------------------------------------------

$calls.clear
$requested = [UI::SCREEN_LAYOUT, panel.slot]
screen_sets(0x01, 0x08, "")
check "the screen takes the chain off", [], MIDI.chain_of(HOST, AMY)
check "...the panel goes", nil, $ui_filter_panels[octave.object_id]
check "...its layout deleted", [:delete, panel.slot], $calls.last
check "...from the MIDI Devices screen when it showed", :midi_devices, $shown.last
check "...the layout is gone", nil, $ui_layouts["Octave J-6>AMY"]

# A route taken down by a script takes the panel and the model's chain
$shown.clear
M.patch(HOST, AMY, filters: [:oct])
o2 = MIDI.chain_of(HOST, AMY)[0]
check "a new instance, a new panel", true, !$ui_filter_panels[o2.object_id].nil?
MIDI.unroute(HOST, AMY)
check "unroute takes the panel down", nil, $ui_filter_panels[o2.object_id]
check "...and the model's chain", false, $fchains.key?(key)
check "...without leaving the screen it was not on", [], $shown

# A route the screen removed (the model drops the chain) goes the same way
M.patch(HOST, AMY, filters: [:oct])
o3 = MIDI.chain_of(HOST, AMY)[0]
$fchains.delete(key)
$fserial += 1
M._poll
check "a chain gone from the model is taken down", [nil, []], [$ui_filter_panels[o3.object_id], MIDI.chain_of(HOST, AMY)]

# Unregistering takes the panel down (the chain keeps the name)
M.patch(HOST, AMY, filters: [:oct])
o4 = MIDI.chain_of(HOST, AMY)[0]
MIDI.unregister_filter(:oct)
check "unregister closes the panel", nil, $ui_filter_panels[o4.object_id]
check "...the name stays in the chain", [:oct], MIDI.chains[[0x01, 0x08]].names
MIDI.register_filter(:oct, Octave.new)
check "registering again opens a panel for the new instance", true,
      !$ui_filter_panels[MIDI.chain_of(HOST, AMY)[0].object_id].nil?

# Two patches with the same names get distinct titles
MIDI.unroute_all
$ui_layouts.clear
MIDI.chain(HOST, AMY, :oct)
first = $ui_layouts.keys
check "one panel", ["Octave J-6>AMY"], first

puts "#{$passed} passed, #{$failed} failed"
exit(1) if $failed > 0
