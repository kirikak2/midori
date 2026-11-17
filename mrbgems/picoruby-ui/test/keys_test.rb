# Host tests for UI::Keys (mrblib/ui.rb): one keyboard per UI::Keys.new.
#
#   ruby test/keys_test.rb
#   cat mrblib/ui.rb test/keys_test.rb | mruby    (no `load` there)
#
# The C side (main/ui/ui_keys.cpp) is a stand-in here: a slot table handing
# out ids and transport bits the way ui_keys_create / ui_keys_default do.

# Loaded first, then the stand-ins below replace what the device provides
unless Object.const_defined?(:UI) && UI.const_defined?(:Keys)
  load File.expand_path("../mrblib/ui.rb", File.dirname(__FILE__))
end

# Stand-ins for the C binding and UI::MIDIDevices
module UI
  KEYS_SLOTS = 8
  $kslots = {}            # id => { bit:, params: {}, colors: {} }
  $knext_bit = 0x100
  $kshown = 0
  $kdestroyed = []
  $logs = []
  $mididevs = []          # [name, bit] registered
  $unregistered = []

  def self._keys_open(id)
    $knext_bit <<= 1
    $kslots[id] = { bit: $knext_bit, params: { octaves: 2, low: 48 }, colors: {} }
    id
  end
  def self._keys_default
    $kslots[0] ? 0 : _keys_open(0)
  end
  def self._keys_create
    id = (1...KEYS_SLOTS).find { |i| !$kslots[i] }
    id ? _keys_open(id) : -1
  end
  def self._keys_destroy(id); $kslots.delete(id); $kdestroyed << id; end
  def self._keys_show(id); $kshown = id; end
  def self._keys_reset(id); $kslots[id][:params] = { octaves: 2, low: 48 } if $kslots[id]; end
  def self._keys_set_i(id, name, v); s = $kslots[id]; return false unless s; s[:params][name] = v; true; end
  def self._keys_get_i(id, name)
    s = $kslots[id]
    return 0 unless s
    name == :transport_bit ? s[:bit] : (s[:params][name] || 0)
  end
  def self._keys_set_key_color(id, note, c); $kslots[id][:colors][note] = c; end
  def self._keys_key_color(id, note); $kslots[id][:colors][note] || -1; end
  def self._keys_receive(id, *a); 0; end
  def self.show(screen); $shown_screen = screen; true; end
  def self.log(text); $logs << text; end
  def self.color_to_rgb565(c); 0x1234; end

  module MIDIDevices
    def self.register(dev, name: nil, color: nil); $mididevs << [name, dev.transport_id]; dev; end
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

# --- The default keyboard ------------------------------------------------------

kb0 = UI::Keys.default
check "the default keyboard is id 0", [0, true], [kb0.id, kb0.default?]
check "UI::Keys.default is one instance", true, UI::Keys.default.equal?(kb0)
check "its name", "Keys", kb0.name
check "main_task.rb registers it, not UI::Keys", [], $mididevs
UI::Keys.default(octaves: 3)
check "keywords on default configure it", 3, kb0.octaves

# --- New keyboards ------------------------------------------------------------

a = UI::Keys.new(octaves: 1, low: 60)
b = UI::Keys.new(name: "Lead")
check "each new is a keyboard of its own", [1, 2], [a.id, b.id]
check "each with its own settings", [1, 60, 2, 48], [a.octaves, a.low, b.octaves, b.low]
check "each with its own transport bit", true, a.transport_id != b.transport_id && a.transport_id != 0
check "each is a device on the MIDI Devices screen", [["Keys 2", a.transport_id], ["Lead", b.transport_id]], $mididevs
check "device_info is the name", "Lead", b.device_info
a.set_color(64, [255, 0, 0])
check "colours are per keyboard", [true, nil], [!a.color(64).nil?, b.color(64)]
check "UI::Keys.all", [0, 1, 2], UI::Keys.all.map { |k| k.id }

# --- Events reach their own keyboard -------------------------------------------

got = []
a.on_note { |e| got << [:a, e[:note]] }
b.on_note { |e| got << [:b, e[:note]] }
UI.dispatch_event({ type: :key, note: 60, velocity: 100, channel: 0, state: true, keys: b.id })
UI.dispatch_event({ type: :key, note: 61, velocity: 100, channel: 0, state: true, keys: a.id })
UI.dispatch_event({ type: :key, note: 62, velocity: 100, channel: 0, state: true, keys: 7 })
check "a key event goes to its keyboard's on_note", [[:b, 60], [:a, 61]], got

# --- show ----------------------------------------------------------------------

b.show
check "show picks the keyboard for the Keys screen", [b.id, :keys], [$kshown, $shown_screen]

# --- close ---------------------------------------------------------------------

bit = a.transport_id
a.close
check "close takes it off the screen and frees the slot", [[bit], [1]], [$unregistered, $kdestroyed]
check "closed", [true, 0], [a.closed?, a.transport_id]
check_raises("a closed keyboard refuses settings", "closed") { a.octaves = 2 }
check "its events are no longer delivered", nil, $ui_keys_instances[1]
c = UI::Keys.new
check "the slot is used again", 1, c.id
kb0.close
check "the default keyboard is only reset", [false, 2], [kb0.closed?, kb0.octaves]

# --- Running out ---------------------------------------------------------------

made = []
check_raises("at most MAX keyboards", "no free keyboard") do
  8.times { made << UI::Keys.new }
end
check "MAX open at once, the default included", UI::Keys::MAX, UI::Keys.all.size

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
raise "#{$failed} failed" if $failed > 0
