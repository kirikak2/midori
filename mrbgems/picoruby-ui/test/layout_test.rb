# Host tests for the layout grid (mrblib/ui_layout.rb).
#
#   ruby test/layout_test.rb
#   cat mrblib/ui_layout.rb test/layout_test.rb | mruby    (no `load` there)
#
# Only the pure-Ruby part is exercised: track sizing, spans, areas, nesting
# and validation. Nothing here needs the device.

unless Object.const_defined?(:UI) && UI.const_defined?(:Grid)
  load File.expand_path("../mrblib/ui_layout.rb", File.dirname(__FILE__))
end

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

def check_raises(name, pattern)
  yield
  $failed += 1
  puts "FAIL #{name}: nothing raised"
rescue ArgumentError => e
  if e.message.include?(pattern)
    $passed += 1
  else
    $failed += 1
    puts "FAIL #{name}: raised #{e.message.inspect}, wanted #{pattern.inspect}"
  end
end

def rects(specs)
  specs.map { |s| s[:rect] }
end

CORES3 = [0, 20, 320, 180]
TAB5 = [0, 40, 1280, 620]

# --- Tracks ------------------------------------------------------------------

starts, sizes = UI::Grid.tracks(0, 100, 4, 0)
check "equal tracks", [[0, 25, 50, 75], [25, 25, 25, 25]], [starts, sizes]

starts, sizes = UI::Grid.tracks(0, 100, 3, 0)
check "remainder goes to the first tracks", [34, 33, 33], sizes
check "tracks fill the length exactly", 100, starts[2] + sizes[2]

starts, sizes = UI::Grid.tracks(10, 100, [3, 1], 4)
check "weights with gap", [[10, 86], [72, 24]], [starts, sizes]

starts, sizes = UI::Grid.tracks(0, 200, [UI.px(60), 1, 1], 10)
check "fixed track", [[0, 70, 140], [60, 60, 60]], [starts, sizes]

check_raises("bad track", "bad track") { UI::Grid.tracks(0, 100, [1, 0], 0) }
check_raises("empty tracks", "tracks must be") { UI::Grid.tracks(0, 100, [], 0) }

# --- Spans -------------------------------------------------------------------

check "integer span", [2, 2], UI::Grid.span(2, 4, "col")
check "inclusive range", [0, 2], UI::Grid.span(0..2, 4, "col")
check "exclusive range", [0, 3], UI::Grid.span(0...4, 4, "col")
check_raises("out of range", "outside 0..3") { UI::Grid.span(4, 4, "col") }
check_raises("descending", "empty or descending") { UI::Grid.span(2..0, 4, "col") }
check_raises("not a number", "must be an Integer or a Range") { UI::Grid.span("1", 4, "col") }

# --- Placement ---------------------------------------------------------------

specs, warnings = UI::Layout.resolve(CORES3, cols: 2, rows: 1, gap: 0) do
  pad col: 0
  pad col: 1
end
check "two pads side by side", [[0, 20, 160, 180], [160, 20, 160, 180]], rects(specs)
check "big pads, no warning", [], warnings

specs, _ = UI::Layout.resolve(TAB5, cols: 4, rows: 3, gap: 20) do
  xypad col: 0..2, row: 0..1
  knob col: 3, row: 0..1
end
# 1280 - 2*20 padding - 3*20 gaps = 1180 -> 295 per column; 620 - 40 - 40 = 540 -> 180 per row
check "span with gap", [[20, 60, 925, 380], [965, 60, 295, 380]], rects(specs)
check "padding defaults to gap", 20, specs[0][:rect][0]

specs, _ = UI::Layout.resolve(CORES3, cols: 2, gap: 6, padding: 0) do
  pad col: 1
end
check "explicit padding", [163, 20, 157, 180], specs[0][:rect]

check_raises("overlap", "overlaps") do
  UI::Layout.resolve(CORES3, cols: 4, rows: 3) do
    xypad col: 0..2, row: 0..1
    pad col: 2, row: 1
  end
end

check_raises("outside the grid", "outside 0..3") do
  UI::Layout.resolve(CORES3, cols: 4) { pad col: 4 }
end

check_raises("one xypad per layout", "only one xypad") do
  UI::Layout.resolve(CORES3, cols: 2) do
    xypad col: 0
    xypad col: 1
  end
end

check_raises("one tempo per layout", "only one tempo") do
  UI::Layout.resolve(CORES3, cols: 2) do
    tempo col: 0
    tempo col: 1
  end
end

check_raises("one sequencer per layout", "only one sequencer") do
  UI::Layout.resolve(CORES3, cols: 2) do
    sequencer col: 0
    sequencer col: 1
  end
end

check_raises("one keys per layout", "only one keys") do
  UI::Layout.resolve(CORES3, cols: 2) do
    keys col: 0
    keys col: 1
  end
end

specs, warnings = UI::Layout.resolve(TAB5, cols: 4, rows: 3, gap: 20) do
  xypad col: 0..3, row: 0
  keys col: 0..3, row: 1..2, octaves: 3
end
check "keys under an xypad", [[20, 60, 1240, 180], [20, 260, 1240, 380]], rects(specs)
check "keys keeps its keywords for UI::Keys.new", { octaves: 3 }, specs[1][:params][:opts]
check "a wide keyboard does not warn", [], warnings

_, warnings = UI::Layout.resolve(CORES3, cols: 4, gap: 0) { keys col: 0 }
check "a narrow keyboard warns", true, warnings[0].include?("keys 0 is 80x180px")

check_raises("widget limit", "too many widgets") do
  UI::Layout.resolve(TAB5, cols: 33, gap: 0) do
    33.times { |i| pad col: i }
  end
end

# --- Areas -------------------------------------------------------------------

specs, _ = UI::Layout.resolve(CORES3, gap: 0) do
  areas <<~GRID
    xy xy k1
    xy xy k2
    p  p  p
  GRID
  xypad area: :xy
  knob area: :k1
  knob area: :k2
  pad area: :p
end
check "areas", [[0, 20, 214, 120], [214, 20, 106, 60], [214, 80, 106, 60], [0, 140, 320, 60]],
      rects(specs)

check_raises("area not a rectangle", "not a rectangle") do
  UI::Layout.resolve(CORES3) do
    areas "a a\na b"
  end
end

check_raises("ragged areas", "row 1 has") do
  UI::Layout.resolve(CORES3) { areas "a a\na" }
end

check_raises("areas vs cols", "columns but cols") do
  UI::Layout.resolve(CORES3, cols: 3) { areas "a b" }
end

check_raises("unknown area", "no area named") do
  UI::Layout.resolve(CORES3) do
    areas "a b"
    pad area: :c
  end
end

check_raises("area and col", "not both") do
  UI::Layout.resolve(CORES3) do
    areas "a b"
    pad area: :a, col: 0
  end
end

specs, _ = UI::Layout.resolve(CORES3, gap: 0) do
  areas ". a"
  pad area: :a
end
check "empty cell", [[160, 20, 160, 180]], rects(specs)

# --- Nesting -----------------------------------------------------------------

specs, _ = UI::Layout.resolve(CORES3, cols: 2, gap: 0) do
  grid col: 1, rows: 2 do
    knob row: 0
    knob row: 1
  end
  pad col: 0
end
check "nested grid", [[160, 20, 160, 90], [160, 110, 160, 90], [0, 20, 160, 180]], rects(specs)

check_raises("grid cell taken", "overlaps") do
  UI::Layout.resolve(CORES3, cols: 2) do
    grid(col: 0, rows: 2) { pad row: 0 }
    pad col: 0
  end
end

specs, _ = UI::Layout.resolve(CORES3, rows: 2, gap: 0) do |l|
  l.grid row: 1, cols: 4 do |g|
    4.times { |i| g.pad col: i }
  end
end
check "block parameter form", [[0, 110, 80, 90], [80, 110, 80, 90], [160, 110, 80, 90],
                               [240, 110, 80, 90]], rects(specs)

# --- Minimum sizes -----------------------------------------------------------

specs, warnings = UI::Layout.resolve(CORES3, cols: 8, rows: 6, gap: 0) do
  pad col: 0, row: 0
  knob col: 1, row: 0..1   # 40x60: too narrow
  knob col: 2..3, row: 2..3 # 80x60: fine
end
check "small widgets warn", 2, warnings.size
check "warning names the widget", true, warnings[0].include?("pad 0 is 40x30px")

# --- Live layouts (device calls stubbed) ------------------------------------

# Stand-ins for what ui.rb and the C binding provide on the device
module UI
  PAD_TYPE_TRIGGER = 0
  PAD_TYPE_MOMENTARY = 1
  PAD_TYPE_TOGGLE = 2
  KNOB_ORIGIN_MIN = 0
  KNOB_ORIGIN_CENTER = 1
  $calls = []
  $knob_values = {}
  $env_values = {}
  $logs = []

  def self.log(text); $logs << text; end
  def self.color_to_rgb565(c); c == :red ? 0xF800 : 0x8410; end
  def self.type_to_constant(t); { trigger: 0, momentary: 1, toggle: 2 }[t] || 0; end
  def self._display; [320, 240, 0, 20, 320, 180, false, 6]; end
  def self._layout_begin(slot, title)
    $calls << [:begin, slot, title]
    @slot_building = slot
    @next_id = 0
    true
  end
  def self._layout_add_pad(*a); $calls << [:pad] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_knob(*a); $calls << [:knob] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_view(*a); $calls << [:view] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_envelope(*a)
    $calls << [:envelope] + a
    $env_values[[@slot_building, @next_id]] = a[6, 4].map { |v| v.to_f }
    @next_id += 1
    @next_id - 1
  end
  def self._layout_commit; $calls << [:commit]; true; end
  class Tempo; end
  def self._layout_delete(slot); $calls << [:delete, slot]; end
  def self._lw_knob_value(slot, id); $knob_values[[slot, id]] || 0.0; end
  # Floats, as the C side keeps them
  def self._lw_env_values(slot, id)
    v = $env_values[[slot, id]]
    v && v.map { |x| (x * 1.0000001) }
  end
  def self._lw_env_set(slot, id, *given)
    v = $env_values[[slot, id]]
    before = v.dup
    given.each_with_index { |g, i| v[i] = g.to_f unless g.nil? }
    v != before
  end
  def self._lw_knob_set_value(slot, id, v)
    changed = $knob_values[[slot, id]] != v
    $knob_values[[slot, id]] = v
    changed
  end
end

def widget_event(layout, id, phase, state: false, value: 0.0)
  { type: :widget, layout: layout.slot, id: id, phase: phase, state: state, value: value }
end

$ui_layouts = {}
$ui_widget_callbacks = {}
hits = []

perform = UI.layout("Perform", cols: 2, gap: 0) do
  pad col: 0, label: "Kick", color: :red, id: :kick do hits << :kick end
  knob col: 1, label: "Cutoff", id: :cutoff do |v| hits << v end
end
check "first layout gets slot 0", 0, perform.slot
check "begin, two widgets, commit", [:begin, :pad, :knob, :commit], $calls.map { |c| c[0] }
check "pad goes over with its rectangle and colour", [:pad, 0, 20, 160, 180, "Kick", 0xF800, 0], $calls[1]
check "knob notifies when it has a block", true, $calls[2].last
check "showable", 0, perform.layout_slot
check "lookup by id", perform.widgets[0], perform[:kick]

UI.dispatch_widget_event(widget_event(perform, 0, :press, state: true))
UI.dispatch_widget_event(widget_event(perform, 0, :release))
UI.dispatch_widget_event(widget_event(perform, 1, :change, value: 42.0))
check "trigger pad fires on press only; knob gets the value", [:kick, 42.0], hits

hits.clear
perform[:cutoff].value = 64
check "setting a knob calls its block", [64.0], hits.map { |v| v.to_f }
hits.clear
check "an unchanged value does not", false, perform[:cutoff].set_value(64)
check "...and does not call it", [], hits

$calls.clear
given = Object.new
played = UI.layout("Play", rows: 2, gap: 0) do
  pad row: 0, label: "Oct+"
  keys row: 1, keys: given
end
check "keys goes over as a view of kind 4", [:view, 4, 0, 110, 320, 90], $calls[2]
check "a given keyboard is used as is", true, played.widgets[1].equal?(given)
played.delete

$calls.clear
clocked = UI.layout("Clock", cols: 2, gap: 0) do
  tempo col: 0
  knob col: 1, label: "BPM", min: 40, max: 240 do |v| v end
end
check "tempo goes over as a view of kind 5", [:view, 5, 0, 20, 160, 180], $calls[1]
check "the tempo widget is UI::Tempo", UI::Tempo, clocked.widgets[0]
clocked.delete

mix = UI.layout("Mix") { |l| l.knob label: "Vol" }
check "a new layout gets the next id", 3, mix.slot

$calls.clear
again = UI.layout("Perform", cols: 1, gap: 0) { pad label: "Snare" }
check "same title keeps its slot", [0, true], [again.slot, again.equal?(perform)]
check "redefinition replaces the widgets", 1, again.widgets.size
UI.dispatch_widget_event(widget_event(again, 1, :change, value: 1.0))
check "old widgets' callbacks are gone", [], hits

many = (0...10).map { |i| UI.layout("Many #{i}") {} }
check "no fixed number of layouts", 10, many.size
check "ids go up in definition order", (4..13).to_a, many.map { |l| l.slot }
many.each { |l| l.delete }

mix.delete
check "delete removes that id", [:delete, 3], $calls.last
check "deleted layout cannot be shown", nil, mix.layout_slot
check "a deleted id is not handed out again at once", 14, UI.layout("E") {}.slot

$ui_layout_next_id = UI::Layout::MAX_ID
check "ids wrap past MAX_ID", UI::Layout::MAX_ID, UI.layout("F") {}.slot
check "...skipping the ids in use", 1, UI.layout("G") {}.slot

check_raises("errors leave the old layout alone", "outside") do
  UI.layout("Perform", cols: 1) { pad col: 1 }
end
check "...which is still there", 1, $ui_layouts["Perform"].widgets.size

$calls.clear
given_seq = Object.new
seqd = UI.layout("Seq", cols: 1, gap: 0) do
  sequencer col: 0, sequencer: given_seq
end
check "sequencer goes over as a view of kind 6", [:view, 6, 0, 20, 320, 180], $calls[1]
check "a given sequencer is used as is", true, seqd.widgets[0].equal?(given_seq)
seqd.delete

# --- Envelopes ----------------------------------------------------------------

check "times round to whole ms within max_time",
      [0.0, 13.0, 2000.0], [-5, 12.6, 9000].map { |v| UI::Envelope.quantize(0, v, 2000.0) }
check "sustain rounds to 0.01 within 0..1",
      [0.0, 0.7, 1.0], [-1, 0.6999, 3].map { |v| UI::Envelope.quantize(2, v, 2000.0) }

# A stand-in for AMY::Synth's envelope API
class FakeSynth
  attr_reader :sent
  def initialize; @sent = []; end
  def amp_attack; 5; end
  def amp_decay; 250; end
  def amp_sustain; 0.5; end
  def amp_release; 800; end
  def amp_envelope(attack: nil, decay: nil, sustain: nil, release: nil)
    @sent << [:amp, attack, decay, sustain, release]
  end
end

# And an FM operator's (no env: prefix)
class FakeOp
  attr_reader :sent
  def initialize; @sent = []; end
  def attack; 1; end
  def decay; 2; end
  def sustain; 0.25; end
  def release; 4; end
  def envelope(attack: nil, decay: nil, sustain: nil, release: nil)
    @sent << [attack, decay, sustain, release]
  end
end

check "values given win over the target's",
      [100.0, 250.0, 0.5, 800.0],
      UI::Envelope.initial_values([100, nil, nil, nil], FakeSynth.new, :amp, 2000.0)
check "no target: the defaults", UI::Envelope::DEFAULT,
      UI::Envelope.initial_values([nil, nil, nil, nil], nil, nil, 2000.0)
check "no env: reads attack / decay / ...", [1.0, 2.0, 0.25, 4.0],
      UI::Envelope.initial_values([nil, nil, nil, nil], FakeOp.new, nil, 2000.0)

specs, warnings = UI::Layout.resolve(CORES3, cols: 2) do
  envelope col: 0, label: "Amp"
  envelope col: 1, max_time: 500, release: 900
end
check "envelopes resolve like any widget", [[0, 20, 160, 180], [160, 20, 160, 180]], rects(specs)
check "release clamps to max_time", 500.0, specs[1][:params][:values][3]
check "envelope of that size does not warn", [], warnings
check_raises("max_time must be positive", "max_time") do
  UI::Layout.resolve(CORES3) { envelope max_time: 0 }
end

$calls.clear
syn = FakeSynth.new
op = FakeOp.new
seen = []
shaped = UI.layout("Shape", cols: 3, gap: 0) do
  envelope col: 0, label: "Amp", id: :amp, to: syn, env: :amp
  envelope col: 1, label: "Op1", to: op, color: :red
  envelope col: 2, label: "Free", attack: 20 do |h| seen << h end
end
check "envelope goes over with its values and notify",
      [:envelope, 0, 20, 107, 180, "Amp", 0x8410, 5.0, 250.0, 0.5, 800.0, 2000.0, true],
      $calls[1]
check "...colour converted", 0xF800, $calls[2][6]
amp = shaped[:amp]
check "the handle is a UI::Envelope", UI::Envelope, amp.class
check "values come back rounded", { attack: 5.0, decay: 250.0, sustain: 0.5, release: 800.0 },
      amp.values

UI.dispatch_widget_event({ type: :widget, layout: shaped.slot, id: 0, kind: :envelope,
                           phase: :change, final: false })
check "a drag sends to the target's amp envelope", [[:amp, 5.0, 250.0, 0.5, 800.0]], syn.sent

UI.dispatch_widget_event({ type: :widget, layout: shaped.slot, id: 2, kind: :envelope,
                           phase: :change, final: true })
check "the block gets a Hash", [{ attack: 20.0, decay: 300.0, sustain: 0.7, release: 300.0 }], seen

syn.sent.clear
check "setting a value sends it", true, (amp.decay = 400; syn.sent.size == 1)
check "...with the new decay", 400.0, syn.sent[0][2]
check "an unchanged value does not", false, amp.set(decay: 400)
check "notify: false only updates", [true, 1], [amp.set(sustain: 0.2, notify: false), syn.sent.size]
check "reset goes back to the definition", [true, 250.0, 0.5], [amp.reset, amp.decay, amp.sustain]

op.sent.clear
shaped.send_all
check "send_all sends every envelope", [[1.0, 2.0, 0.25, 4.0]], op.sent
shaped.delete

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
raise "#{$failed} failed" if $failed > 0
