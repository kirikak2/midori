# Host tests for UI::MIDIDevices (mrblib/ui_midi_devices.rb) on top of the
# MIDI filter / route Ruby code. The C sides of both gems are stubbed: the UI
# model is a small Ruby imitation of ui_midi_devices.cpp's list / selection.
#
#   ruby test/midi_devices_test.rb          (from the picoruby-ui directory)

midi = File.expand_path("../../picoruby-midi", File.dirname(__FILE__))
load File.join(midi, "host_test/filter_stubs.rb")
load File.join(midi, "mrblib/midi_constants.rb")
load File.join(midi, "mrblib/midi_device.rb")
load File.join(midi, "mrblib/midi_filter.rb")
load File.join(midi, "mrblib/midi_route.rb")

$up = {}   # bit => connected?

module MIDI
  class << self
    def _transport_connected?(bit); $up[bit] ? true : false; end
    def _route_sources; 0x07 | 0x20; end   # + the on-screen keys
    def _route_get(i)
      r = $stub[:routes][i]
      r ? [r[0], r[1], r[2]] : nil
    end
    def _route_filtered(src)
      m = 0
      $stub[:filters].each_key { |k| m |= k[1] if k[0] == src }
      m
    end
  end
end

# The C model (ui_midi_devices.cpp), reduced to what Ruby sees
$devs = []        # [bit, flags, key, name]
$sel = [-1, -1]
$wanted = []
$serial = 0
$events = []

module UI
  class << self
    def rgb(r, g, b); 1; end
    def color_to_rgb565(c); 2; end
    def on(type, &b); $events << [type, b]; end
    def show(s); $shown = s; end

    def _mididev_register(key, name, bit, color, builtin)
      i = $devs.index { |d| builtin && d[2] == key } || $devs.index { |d| bit != 0 && d[0] == bit }
      unless i
        $devs << [0, builtin ? 1 : 0, builtin ? key : "", name]
        i = $devs.size - 1
      end
      d = $devs[i]
      if bit != 0
        d[0] = bit
        d[1] &= ~2
      elsif d[0] == 0
        d[1] |= 2
      end
      d[3] = name
      $serial += 1
      i
    end

    def _mididev_unregister(bit)
      i = $devs.index { |d| d[0] == bit }
      $devs.delete_at(i) if i
      !i.nil?
    end

    def _mididev_activate(key, bit)
      d = $devs.find { |x| x[2] == key }
      return false unless d
      d[0] = bit
      d[1] &= ~2
      true
    end

    def _mididev_count; $devs.size; end
    def _mididev_get(i); $devs[i] ? $devs[i].dup : nil; end
    def _mididev_selected(out); $sel[out ? 1 : 0]; end
    def _mididev_select(out, i, save); $sel[out ? 1 : 0] = i; $serial += 1; end
    def _mididev_restore; 0; end
    def _mididev_save; true; end
    def _mididev_take_wanted; $wanted.shift; end
    def _mididev_serial; $serial; end
    def _tempo_bit; 0x40; end
  end
end

load File.expand_path("../mrblib/ui_tempo.rb", File.dirname(__FILE__))

load File.expand_path("../mrblib/ui_midi_devices.rb", File.dirname(__FILE__))

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
rescue ArgumentError, RuntimeError => e
  if e.message.include?(pattern)
    $passed += 1
  else
    $failed += 1
    puts "FAIL #{name}: #{e.message}"
  end
end

class FakeTransport
  attr_reader :transport_id
  def initialize(bit, info); @transport_id = bit; @info = info; end
  def device_info; @info; end
  def connected?; true; end
end

M = UI::MIDIDevices
HOST = FakeTransport.new(0x01, "USB host")
UART = FakeTransport.new(0x02, "UART")
AMY  = FakeTransport.new(0x08, "AMY")
started = []

# --- built-ins -----------------------------------------------------------------

M.register_builtin(:usb_host, "USB Host", 0x01) { started << :usb_host; HOST }
M.register_builtin(:uart, "SAM2695", 0x02) { started << :uart; UART }
M.register_builtin(:amy, "AMY", 0) { started << :amy; AMY }
check "built-ins started now (not standby)", [:usb_host, :uart], started
check "amy on standby", [0, 3, "amy", "AMY"], $devs[2]
check "devices leave standby out", 2, M.devices.size
check "source info", [:usb_host, M.devices[0]], $__midi_source_info__[0x01]

# a saved patch wants AMY: boot starts it
$wanted << "amy"
M.boot
check "standby started by boot", [:usb_host, :uart, :amy], started
check "amy has its bit", 0x08, $devs[2][0]

# --- register / rename -----------------------------------------------------------

j6 = M.register(MIDI::Device.new(HOST), name: "J-6")
check "rename keeps built-in", [0x01, 1, "usb_host", "J-6"], $devs[0]
check "register returns a device", true, j6.is_a?(MIDI::Device)
extra = M.register(FakeTransport.new(0x10, "Extra"))
check "script device default name", [0x10, 0, "", "Extra"], $devs[3]
check "script device wrapped", MIDI::Device, extra.class
check_raises("unregistered transport", "not a registered") { M.register(FakeTransport.new(0, "x")) }

# --- selection ---------------------------------------------------------------------

check "nothing selected", nil, M.midi_in
M.midi_in = j6
check "midi_in index", 0, $sel[0]
check "midi_in device", j6, M.midi_in
check_raises("IN must be a source", "no MIDI IN") { M.midi_in = MIDI::Device.new(AMY) }
M.midi_out = MIDI::Device.new(AMY)
check "midi_out device bit", 0x08, MIDI.route_bit(M.midi_out)
M.midi_out = nil
check "midi_out cleared", nil, M.midi_out

# A standby OUT starts when asked for
$devs[2][0] = 0
$devs[2][1] |= 2
$ui_mididev_devices.delete(0x08)
started.clear
$sel[1] = 2
out = M.midi_out
check "standby OUT started on access", [:amy], started
check "and returned", 0x08, MIDI.route_bit(out)

# A bit nobody made an object for this VM: a BitTransport device
$devs << [0x20, 1, "keys", "Keys"]
kd = M.send(:_device_for_bit, 0x20)
check "bit transport", 0x20, MIDI.route_bit(kd)
check "bit transport class", UI::MIDIDevices::BitTransport, kd.transport.class

# The tempo's bit gives a device on UI::Tempo itself
td = M.send(:_device_for_bit, 0x40)
check "tempo device", UI::Tempo, td.transport
check "tempo is routable by its bit", 0x40, MIDI.route_bit(UI::Tempo)

# --- patches -----------------------------------------------------------------------

$up = { 0x01 => true, 0x08 => true }
M.patch(j6, MIDI::Device.new(AMY))
MIDI.filter(j6, AMY) { |m| m }
M.patch(j6, UART, channel: 9)
M.patch(j6, kd, channel: :clock)
list = M.patches
check "patch count", 3, list.size
check "patch clock only", :clock, list[2][:channel]
check "patch from", j6, list[0][:from]
check "patch to (amy)", 0x08, MIDI.route_bit(list[0][:to])
check "patch active", true, list[0][:active]
check "patch filtered", true, list[0][:filtered]
check "patch channel", 9, list[1][:channel]
check "patch inactive (UART down)", false, list[1][:active]

M.unregister(UART)
check "unregister drops patches to it", 2, M.patches.size
check "unregister drops entry", nil, $devs.index { |d| d[0] == 0x02 }

# --- input follows the selection --------------------------------------------------------

$stub[:filters] = {}
$__midi_filters__ = {}
$sel[0] = -1
got = []
inp = M.input
h = inp.on(:note_on) { |m| got << [m.note, m.source] }
check "no source yet", false, $stub[:filters].key?([0x01, MIDI::DST_RUBY])

$sel[0] = 0
$serial += 1
MIDI.run_filters                       # the hook notices the new serial
check "listening on the IN", true, $stub[:filters].key?([0x01, MIDI::DST_RUBY])
check "input device", j6, inp.device
$stub[:queue] << [MIDI::DST_RUBY, 0x01, 0x9, 0x90, 60, 100]
MIDI.run_filters
check "handler got it", [[60, :usb_host]], got

# the user picks another IN: the handler moves
keys_index = $devs.index { |d| d[2] == "keys" }
$sel[0] = keys_index
$serial += 1
MIDI.run_filters
check "old source dropped", false, $stub[:filters].key?([0x01, MIDI::DST_RUBY])
check "new source", true, $stub[:filters].key?([0x20, MIDI::DST_RUBY])

inp.off(h)
check "off", false, $stub[:filters].key?([0x20, MIDI::DST_RUBY])

# a selection that cannot be a source (AMY) is taken as none, not an error
inp.on(:note_on) { |m| m }
$sel[0] = $devs.index { |d| d[2] == "amy" }
$serial += 1
MIDI.run_filters
check "non-source IN ignored", nil, inp.device
check "nothing listened", false, $stub[:filters].key?([0x08, MIDI::DST_RUBY])

# --- the OUT that follows the selection ---------------------------------------------------

class RecTransport < FakeTransport
  attr_reader :sent
  def initialize(bit, info); super; @sent = []; end
  def send_packet(cable, cin, b1, b2, b3); @sent << [cin, b1, b2, b3]; 0; end
end

SYN_A = RecTransport.new(0x40, "Synth A")
SYN_B = RecTransport.new(0x80, "Synth B")
M.register(SYN_A)
M.register(SYN_B)

$sel[1] = -1
$serial += 1
out = M.output
check "output is kept", out, M.output
check "no OUT: nowhere to send", 0, out.transport.transport_id
check "no OUT: send is a no-op", 0, out.note_on(60, 100)

$sel[1] = $devs.index { |d| d[0] == 0x40 }
$serial += 1
out.note_on(60, 100)
check "sent to the OUT picked", [[0x9, 0x90, 60, 100]], SYN_A.sent
check "its bit (trigger / route)", 0x40, MIDI.route_bit(out)
check "info", "Synth A", out.info

# the user picks another OUT: the kept device follows at once, and the old
# OUT gets All Notes Off on every channel
$sel[1] = $devs.index { |d| d[0] == 0x80 }
$serial += 1
out.note_off(60)
check "now sent to the new OUT", [[0x8, 0x80, 60, 0]], SYN_B.sent
check "old OUT: All Notes Off x16", 16, SYN_A.sent.count { |p| p[0] == 0xB && p[2] == 123 }
check "old OUT: one per channel", (0..15).to_a, SYN_A.sent.select { |p| p[2] == 123 }.map { |p| p[1] & 0x0F }

# a change found by the hook (no message sent in between) also releases
SYN_B.sent.clear
$sel[1] = -1
$serial += 1
MIDI.run_filters
check "deselected: old OUT released from the hook", 16, SYN_B.sent.size
check "deselected: nothing to send to", 0, out.transport.transport_id

# --- events -----------------------------------------------------------------------------

M.on_change { |e| e }
check "on_change registers a UI handler", :midi_devices, $events.last[0]
M.show
check "show", :midi_devices, $shown

puts "#{$passed} passed, #{$failed} failed"
exit(1) if $failed > 0
