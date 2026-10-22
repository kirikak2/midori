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
  $logs = []

  def self.log(text); $logs << text; end
  def self.color_to_rgb565(c); c == :red ? 0xF800 : 0x8410; end
  def self.type_to_constant(t); { trigger: 0, momentary: 1, toggle: 2 }[t] || 0; end
  def self._display; [320, 240, 0, 20, 320, 180, false, 6]; end
  def self._layout_begin(slot, title); $calls << [:begin, slot, title]; @next_id = 0; true; end
  def self._layout_add_pad(*a); $calls << [:pad] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_knob(*a); $calls << [:knob] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_view(*a); $calls << [:view] + a; @next_id += 1; @next_id - 1; end
  def self._layout_commit; $calls << [:commit]; true; end
  class Tempo; end
  def self._layout_delete(slot); $calls << [:delete, slot]; end
  def self._lw_knob_value(slot, id); $knob_values[[slot, id]] || 0.0; end
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
check "second layout gets slot 1", 1, mix.slot

$calls.clear
again = UI.layout("Perform", cols: 1, gap: 0) { pad label: "Snare" }
check "same title keeps its slot", [0, true], [again.slot, again.equal?(perform)]
check "redefinition replaces the widgets", 1, again.widgets.size
UI.dispatch_widget_event(widget_event(again, 1, :change, value: 1.0))
check "old widgets' callbacks are gone", [], hits

check_raises("layout limit", "too many layouts") do
  UI.layout("C") {}
  UI.layout("D") {}
  UI.layout("E") {}
end

mix.delete
check "delete frees the slot", [:delete, 1], $calls.last
check "deleted layout cannot be shown", nil, mix.layout_slot
check "freed slot is reused", 1, UI.layout("E") {}.slot

check_raises("errors leave the old layout alone", "outside") do
  UI.layout("Perform", cols: 1) { pad col: 1 }
end
check "...which is still there", 1, $ui_layouts["Perform"].widgets.size

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
raise "#{$failed} failed" if $failed > 0
