# Host tests for the AMY panel (mrblib/ui_amy_panel.rb, docs/AMY_PANEL.md).
#
#   ruby test/amy_panel_test.rb
#   cat mrblib/ui_layout.rb mrblib/ui_amy_panel.rb test/amy_panel_test.rb | mruby
#
# The pure helpers (presets, channels, tab labels, knob scaling) and the page
# as the layout DSL resolves it, with stand-in parts. Nothing here needs the
# device or AMY.

unless Object.const_defined?(:UI) && UI.const_defined?(:AMYPanel)
  load File.expand_path("../mrblib/ui_layout.rb", File.dirname(__FILE__))
  load File.expand_path("../mrblib/ui_amy_panel.rb", File.dirname(__FILE__))
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

P = UI::AMYPanel

# --- Presets ---------------------------------------------------------------

check "fm +1", 129, P.next_patch(:fm, 128, 1)
check "fm wraps up", 128, P.next_patch(:fm, 255, 1)
check "fm wraps down", 255, P.next_patch(:fm, 128, -1)
check "fm -10 wraps", 246, P.next_patch(:fm, 128, -10)
check "synth init -> 0", 0, P.next_patch(:synth, nil, 1)
check "synth 0 -> init", nil, P.next_patch(:synth, 0, -1)
check "synth 127 -> init", nil, P.next_patch(:synth, 127, 1)
check "synth init -> 127", 127, P.next_patch(:synth, nil, -1)
check "synth +10", 15, P.next_patch(:synth, 5, 10)
check "synth 120 +10 wraps past init", 1, P.next_patch(:synth, 120, 10)

# --- Channels and labels ---------------------------------------------------

class FakePart
  attr_accessor :kind, :channel, :patch

  def initialize(kind, channel, patch = nil)
    @kind = kind
    @channel = channel
    @patch = patch
  end

  def removed?
    false
  end

  def filter
    :none
  end

  def op(_n)
    FakeOp.new
  end
end

class FakeOp
  def level; 0.5; end
  def ratio; 2.0; end
end

check "free channel empty", 0, P.free_channel([])
check "free channel gap", 1, P.free_channel([FakePart.new(:fm, 0), FakePart.new(:fm, 2)])
full = []
16.times { |i| full << FakePart.new(:synth, i) }
check "free channel full", 0, P.free_channel(full)
check "next channel", 1, P.next_channel(0, 1)
check "next channel wraps up", 0, P.next_channel(15, 1)
check "next channel wraps down", 15, P.next_channel(0, -1)
check "next channel from none", 1, P.next_channel(nil, 1)

check "tab label", "SY 2 Init", P.tab_label(:synth, 1, "Init")
check "tab label no channel", "FM - X", P.tab_label(:fm, nil, "X")
check "tab label cut", "FM 1 A12 Brass Sw", P.tab_label(:fm, 0, "A12 Brass Swell")

# --- Knob scaling ----------------------------------------------------------

cutoff = P::SYNTH_KNOBS[0]
check "cutoff lo", 0.0, P.knob_from_value(cutoff, 100)
check "cutoff hi", 127.0, P.knob_from_value(cutoff, 8000)
check "cutoff round trip", 64.0, P.knob_from_value(cutoff, P.value_from_knob(cutoff, 64))
check "cutoff default", P.knob_from_value(cutoff, 4000.0), P.knob_from_value(cutoff, nil)
check "cutoff clamps", 127.0, P.knob_from_value(cutoff, 20000)

pan = P::SYNTH_KNOBS[5]
check "pan default centre", 64.0, P.knob_from_value(pan, nil)
check "pan from knob", 1.0, P.value_from_knob(pan, 127)

algo = P::FM_KNOBS[0]
check "algo is itself", 5.0, P.knob_from_value(algo, 5)
check "algo clamps", 32.0, P.knob_from_value(algo, 40)
check "algo from knob", 7, P.value_from_knob(algo, 7.0)

fcut = P::FM_KNOBS[4]
check "fm cutoff off", 127.0, P.knob_from_value(fcut, nil)
check "fm cutoff top is off", nil, P.value_from_knob(fcut, 127)
check "fm cutoff high stays on", 126.0, P.knob_from_value(fcut, 8000)
check "fm cutoff below top", true, P.value_from_knob(fcut, 126) > 7000

# --- Pages -----------------------------------------------------------------

# The panel without the device: its page against UI::Layout.resolve
class TestPanel < UI::AMYPanel
  def initialize(parts, part, choosing: false)
    @parts = parts
    @part = part
    @op = 1
    @choosing = choosing
    @keys = nil
  end
end

CONTENT = [0, 80, 1280, 620]

def page(panel)
  specs, _warnings = UI::Layout.resolve(CONTENT, rows: [1, 1, 4, 2], gap: 4) do |l|
    panel.build_page(l)
  end
  specs
end

def kinds(specs)
  counts = {}
  specs.each { |s| counts[s[:kind]] = (counts[s[:kind]] || 0) + 1 }
  counts
end

syn = FakePart.new(:synth, 0)
fm = FakePart.new(:fm, 1, 130)

empty = page(TestPanel.new([], nil))
check "empty: + and the hint", 2, empty.size
check "empty: hint label", "Tap + to add a part", empty[1][:params][:label]

choosing = page(TestPanel.new([], nil, choosing: true))
check "choosing: three choices + hint", ["+ FM", "+ Synth", "Cancel"],
      choosing[0, 3].map { |s| s[:params][:label] }

s = page(TestPanel.new([syn, fm], syn))
check "synth page kinds", { pad: 14, envelope: 2, knob: 8, keys: 1 }, kinds(s)
check "synth envelopes", [:amp, :filter],
      s.select { |x| x[:kind] == :envelope }.map { |x| x[:params][:env] }
check "selected tab coloured", :cyan, s[0][:params][:color]
check "other tab gray", :gray, s[1][:params][:color]
check "fm tab label", "FM 2 Patch 130", s[1][:params][:label]

f = page(TestPanel.new([syn, fm], fm))
check "fm page kinds", { pad: 15, envelope: 1, knob: 8, keys: 1 }, kinds(f)
check "fm algo knob range", [1, 32],
      f.select { |x| x[:kind] == :knob }.first[:params].values_at(:min, :max)

eight = []
8.times { |i| eight << FakePart.new(:synth, i) }
full_page = page(TestPanel.new(eight, eight[0]))
check "full: at most 32 widgets", true, full_page.size <= UI::Layout::MAX_WIDGETS
check "full: + becomes Full", true, full_page.any? { |x| x[:params][:label] == "Full" }
fm_eight = eight.dup
fm_eight[0] = FakePart.new(:fm, 0, 128)
check "full fm: at most 32 widgets", true,
      page(TestPanel.new(fm_eight, fm_eight[0])).size <= UI::Layout::MAX_WIDGETS

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
