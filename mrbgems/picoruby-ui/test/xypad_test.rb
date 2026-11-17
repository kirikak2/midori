# Host tests for UI::XYPad (mrblib/ui.rb): one pad per UI::XYPad.new.
#
#   ruby test/xypad_test.rb
#   cat mrblib/ui.rb test/xypad_test.rb | mruby    (no `load` there)
#
# The C side (main/ui/ui_xypad.cpp) is a stand-in here: a pad table handing
# out ids and transport bits the way ui_xypad_create / ui_xypad_default do.

# Loaded first, then the stand-ins below replace what the device provides
unless Object.const_defined?(:UI) && UI.const_defined?(:XYPad)
  load File.expand_path("../mrblib/ui.rb", File.dirname(__FILE__))
end

module UI
  PADS = 4
  $xpads = {}            # id => { bit:, slots: [{...}] * 5, max: }
  $xnext_bit = 0x100
  $xshown = 0
  $xdestroyed = []
  $xreset = []
  $xdispatched = []      # [pad, route, cin, b1, b2, b3]
  $logs = []
  $mididevs = []         # [name, bit, output]
  $unregistered = []

  def self._xypad_open(id)
    $xnext_bit <<= 1
    $xpads[id] = { bit: $xnext_bit, slots: Array.new(5) { {} }, max: 5 }
    id
  end
  def self._xypad_default; $xpads[0] ? 0 : _xypad_open(0); end
  def self._xypad_create
    id = (1...PADS).find { |i| !$xpads[i] }
    id ? _xypad_open(id) : -1
  end
  def self._xypad_destroy(id); $xpads.delete(id); $xdestroyed << id; end
  def self._xypad_show(id); $xshown = id; end
  def self._xypad_reset(id); $xreset << id; $xpads[id][:slots] = Array.new(5) { {} } if $xpads[id]; end
  def self._xypad_set_max_touches(id, n); $xpads[id][:max] = n; end
  def self._xypad_set_i(id, i, name, v); p = $xpads[id]; return false unless p; p[:slots][i][name] = v; true; end
  def self._xypad_set_f(id, i, name, v); _xypad_set_i(id, i, name, v); end
  def self._xypad_get_i(id, i, name); p = $xpads[id]; p ? (p[:slots][i][name] || 0) : 0; end
  def self._xypad_get_f(id, i, name)
    return 2.0 if name == :touch_glide_range
    _xypad_get_i(id, i, name).to_f
  end
  def self._xypad_set_scale(id, i, notes); $xpads[id][:slots][i][:scale] = notes; end
  def self._xypad_get_scale(id, i); $xpads[id][:slots][i][:scale] || []; end
  def self._xypad_transport_bit(id); p = $xpads[id]; p ? p[:bit] : 0; end
  def self._xypad_targets(id, cin, b1); 0x40; end   # every pad is routed to bit 0x40
  def self._xypad_dispatch(id, route, cin, b1, b2, b3); $xdispatched << [id, route, cin, b1, b2, b3]; end
  def self._request_screen(index, arg); $shown_screen = SCREEN_NAMES[index]; true; end
  def self._pop_event; nil; end
  def self.log(text); $logs << text; end

  module MIDIDevices
    def self.register(dev, name: nil, color: nil, output: true)
      $mididevs << [name, dev.transport_id, output]
      dev
    end
    def self.unregister(dev); $unregistered << dev.transport_id; end
  end
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
rescue => e
  if e.message.include?(pattern)
    $passed += 1
  else
    $failed += 1
    puts "FAIL #{name}: raised #{e.message.inspect}, wanted #{pattern.inspect}"
  end
end

def touch(pad, slot, phase, note: 60, channel: 0, x: 0.0, y: 64.0, bend: 0.0)
  { type: :xypad_touch, pad: pad.id, slot: slot, phase: phase, channel: channel,
    note: note, bend_semitones: bend, x: x, y: y }
end

# --- The default pad -------------------------------------------------------------

xy0 = UI::XYPad.default
check "the default pad is id 0", [0, true], [xy0.id, xy0.default?]
check "UI::XYPad.default is one instance", true, UI::XYPad.default.equal?(xy0)
check "it starts from the defaults", [0], $xreset
check "main_task.rb registers it, not UI::XYPad", [], $mididevs
UI::XYPad.default(y_cc: 71)
check "keywords on default configure every slot", [71, 71], [xy0.slot(1)[:y_cc], xy0.slot(5)[:y_cc]]

# --- New pads -------------------------------------------------------------------

a = UI::XYPad.new(scale: [60, 62, 64])
b = UI::XYPad.new(name: "FX", y_cc: 74)
check "each new is a pad of its own", [1, 2], [a.id, b.id]
check "each with its own slots", [[60, 62, 64], []], [a.slot(1)[:scale], b.slot(1)[:scale]]
check "each with its own transport bit", true, a.transport_id != b.transport_id && a.transport_id != 0
check "each is a MIDI IN only device on the MIDI Devices screen",
      [["XYPad 2", a.transport_id, false], ["FX", b.transport_id, false]], $mididevs
check "device_info is the name", "FX", b.device_info
check "channels per slot by default", [0, 4], [a.slot(1)[:channel], a.slot(5)[:channel]]
check "UI::XYPad.all", [0, 1, 2], UI::XYPad.all.map { |p| p.id }

# --- Touches reach their own pad --------------------------------------------------

seen = []
a.on_touch { |e| seen << [:a, e[:phase]] }
b.on_touch { |e| seen << [:b, e[:phase]] }
UI.dispatch_event(touch(b, 1, :down))
UI.dispatch_event(touch(a, 2, :down))
UI.dispatch_event(touch(b, 1, :up))
check "a touch goes to its pad's handler", [[:b, :down], [:a, :down], [:b, :up]], seen
check "the built-in MIDI goes along each pad's own routes", [a.id, b.id], $xdispatched.map { |d| d[0] }.uniq.sort
check "...first message: b's note on", [b.id, 0x40, 0x9, 0x90, 60, 100], $xdispatched[0]

# --- show -----------------------------------------------------------------------

b.show
check "show picks the pad for the XYPad screen", [b.id, :xypad], [$xshown, $shown_screen]

# --- close ----------------------------------------------------------------------

bit = a.transport_id
a.close
check "close takes it off the screen and frees the id", [[bit], [1]], [$unregistered, $xdestroyed]
check "closed", [true, 0], [a.closed?, a.transport_id]
check_raises("a closed pad refuses settings", "closed") { a.slot(1, y_cc: 1) }
check "its touches are no longer delivered", nil, $ui_xypad_instances[1]
c = UI::XYPad.new
check "the id is used again", 1, c.id
xy0.close
check "the default pad is only reset", [false, true], [xy0.closed?, $xreset.size > 1]

# --- Running out ----------------------------------------------------------------

check_raises("at most MAX pads", "no free pad") do
  4.times { UI::XYPad.new }
end
check "MAX open at once, the default included", UI::XYPad::MAX, UI::XYPad.all.size

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
raise "#{$failed} failed" if $failed > 0
