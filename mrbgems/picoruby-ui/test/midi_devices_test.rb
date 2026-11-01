# Host tests for UI::MIDIDevices (mrblib/ui_midi_devices.rb) on top of the
# MIDI filter / route Ruby code. The C sides of both gems are stubbed: the UI
# model is a small Ruby imitation of ui_midi_devices.cpp's list.
#
#   ruby test/midi_devices_test.rb          (from the picoruby-ui directory)

midi = File.expand_path("../../picoruby-midi", File.dirname(__FILE__))
load File.join(midi, "host_test/filter_stubs.rb")
load File.join(midi, "mrblib/midi_constants.rb")
load File.join(midi, "mrblib/midi_device.rb")
load File.join(midi, "mrblib/midi_filter.rb")
load File.join(midi, "mrblib/midi_route.rb")
load File.join(midi, "mrblib/midi_clock.rb")
load File.join(midi, "mrblib/midi_filter_class.rb")

$up = {}   # bit => connected?
SCRIPT = 0x100              # The Script device: picoruby-midi's virtual port
$stub[:virtual] = SCRIPT

module MIDI
  class << self
    def _transport_connected?(bit); $up[bit] ? true : false; end
    def _route_sources; 0x07 | 0x20 | SCRIPT; end   # + the on-screen keys, Script
    def _route_get(i)
      r = $stub[:routes][i]
      r ? [r[0], r[1], r[2], r[3]] : nil
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
    def _mididev_restore; 0; end
    def _mididev_save; true; end
    def _mididev_take_wanted; $wanted.shift; end
    def _mididev_serial; $serial; end
    def _tempo_bit; 0x40; end
    # Named filters (test/midi_filters_test.rb covers them)
    def _filter_define(name, label, params); true; end
    def _filter_undefine(name); true; end
    def _filter_serial; 0; end
    def _filter_chains; []; end
    def _filter_chain_set(src, dst, text, save); 1; end
  end
end

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

# --- Script: the script's IN / OUT ------------------------------------------------

$devs.unshift([SCRIPT, 1, "script", "Script"])   # Registered by C before the rest
check "nothing patched to Script", nil, M.midi_in
check "nothing patched from Script", nil, M.midi_out
M.midi_in = j6
check "midi_in= patches to Script", [[0x01, SCRIPT, MIDI::ROUTE_ALL_CHANNELS]], $stub[:routes]
check "midi_in is Script", SCRIPT, MIDI.route_bit(M.midi_in)
M.midi_in = MIDI::Device.new(FakeTransport.new(0x04, "PC"))
check "midi_in= replaces", [[0x04, SCRIPT, MIDI::ROUTE_ALL_CHANNELS]], $stub[:routes]
check_raises("IN must be a source", "source must be") { M.midi_in = MIDI::Device.new(AMY) }
M.midi_out = MIDI::Device.new(AMY)
check "midi_out is Script", SCRIPT, MIDI.route_bit(M.midi_out)
check "midi_out= patches from Script", [SCRIPT, 0x08], $stub[:routes].last[0, 2]
M.midi_out = nil
check "midi_out cleared", nil, M.midi_out
M.midi_in = nil
check "midi_in cleared", [], $stub[:routes]
$stub[:routes] = []

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
M.patch(j6, UART, channel: [0, 9, :clock])
check "patch several", [0, 9, :clock], M.patches[3][:channel]
MIDI.unroute(j6, UART)
M.patch(j6, UART, channel: 9)

M.unregister(UART)
check "unregister drops patches to it", 2, M.patches.size
check "unregister drops entry", nil, $devs.index { |d| d[0] == 0x02 }

# --- input: what is routed into Script ----------------------------------------------------

$stub[:filters] = {}
$__midi_filters__ = {}
got = []
inp = M.input
h = inp.on(:note_on) { |m| got << [m.note, m.source] }
check "listening on Script by types", 1 << 0x9, $stub[:vlisten] & (1 << 0x9)
check "no C filter for it", false, $stub[:filters].key?([SCRIPT, MIDI::DST_RUBY])
check "input device: nothing patched yet", nil, inp.device
MIDI.route(j6, SCRIPT)
check "input device: Script", SCRIPT, MIDI.route_bit(inp.device)
$stub[:queue] << [MIDI::DST_VIRTUAL, 0x01, 0x9, 0x90, 60, 100]
MIDI.run_filters
check "handler got it, with its source", [[60, :usb_host]], got
check "external bpm from what is patched", 120.0, inp.external_bpm
inp.off(h)
check "off", 0, $stub[:vlisten]
MIDI.unroute_all

# --- output: Script as a device -----------------------------------------------------------

out = M.output
check "output is kept", out, M.output
check "output is Script", SCRIPT, MIDI.route_bit(out)
$stub[:sent] = []
out.note_on(60, 100)
check "sent to Script (C sends it along its routes)", [[SCRIPT, 0x9, 0x90, 60, 100]], $stub[:sent]

# --- events -----------------------------------------------------------------------------

M.on_change { |e| e }
check "on_change registers a UI handler", :midi_devices, $events.last[0]
M.show
check "show", :midi_devices, $shown

puts "#{$passed} passed, #{$failed} failed"
exit(1) if $failed > 0
