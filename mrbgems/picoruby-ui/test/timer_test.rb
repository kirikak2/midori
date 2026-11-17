# Host tests for UI::Timer (mrblib/ui_timer.rb) and the layout's `timer`.
#
#   ruby test/timer_test.rb
#   cat mrblib/ui_layout.rb mrblib/ui_timer.rb test/timer_test.rb | mruby
#
# The countdown itself is C (main/ui/ui_timer.cpp); here the C binding is a
# stand-in, and what is checked is how events reach the blocks, arming, and
# the layout widget.

# Stand-ins for what ui.rb and the C binding provide on the device
module UI
  $timer = { seconds: 60, remaining: 60, state: 0, armed: false, resets: 0 }
  $calls = []
  $logs = []
  $tempo = []

  def self.log(text); $logs << text; end
  def self.color_to_rgb565(c); 0x8410; end
  def self.type_to_constant(t); 0; end
  def self._display; [320, 240, 0, 20, 320, 180, false, 6]; end
  def self._layout_begin(slot, title); $calls << [:begin, slot, title]; @next_id = 0; true; end
  def self._layout_add_pad(*a); $calls << [:pad] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_knob(*a); $calls << [:knob] + a; @next_id += 1; @next_id - 1; end
  def self._layout_add_view(*a); $calls << [:view] + a; @next_id += 1; @next_id - 1; end
  def self._layout_commit; $calls << [:commit]; true; end
  def self._layout_delete(slot); $calls << [:delete, slot]; end
  def self.show(screen); $calls << [:show, screen]; true; end

  def self._timer_seconds; $timer[:seconds]; end
  def self._timer_set_seconds(s)
    s = 1 if s < 1
    s = 5999 if s > 5999
    $timer[:seconds] = s
    $timer[:remaining] = s if $timer[:state] != 1
  end
  def self._timer_remaining; $timer[:remaining]; end
  def self._timer_state; $timer[:state]; end
  def self._timer_reset; $timer[:resets] += 1; end
  def self._timer_set_armed(on); $timer[:armed] = on; end
  def self._timer_armed; $timer[:armed]; end

  class Tempo
    def self.start; $tempo << :start; nil; end
    def self.stop; $tempo << :stop; nil; end
  end
end

unless Object.const_defined?(:UI) && UI.const_defined?(:Grid)
  load File.expand_path("../mrblib/ui_layout.rb", File.dirname(__FILE__))
end
unless UI.const_defined?(:Timer)
  load File.expand_path("../mrblib/ui_timer.rb", File.dirname(__FILE__))
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

def timer_event(kind, remaining)
  { type: :timer, kind: kind, remaining: remaining }
end

# --- Settings ----------------------------------------------------------------

UI::Timer.seconds = 90
check "seconds= sets the time", 90, UI::Timer.seconds
check "remaining follows while idle", 90, UI::Timer.remaining
UI::Timer.seconds = 100000
check "clamped to 99:59 (by C)", 5999, UI::Timer.seconds
UI::Timer.set(minutes: 1, seconds: 30)
check "set(minutes:, seconds:)", 90, UI::Timer.seconds
UI::Timer.seconds = 45.7
check "a Float is truncated", 45, UI::Timer.seconds
check "idle state", :idle, UI::Timer.state
$timer[:state] = 1
check "running", [:running, true], [UI::Timer.state, UI::Timer.running?]
$timer[:state] = 3
check "finished", [:finished, true], [UI::Timer.state, UI::Timer.finished?]
$timer[:state] = 0

UI::Timer.reset
check "reset goes to C", 1, $timer[:resets]

# --- Arming --------------------------------------------------------------------

check "not armed by default", false, UI::Timer.armed?
ticks = []
UI::Timer.on_tick { |sec| ticks << sec }
check "on_tick arms", true, UI::Timer.armed?
UI::Timer.disarm
check "disarm", false, UI::Timer.armed?
UI::Timer.armed = 1
check "armed= takes any truthy value", true, UI::Timer.armed?
UI::Timer.armed = false

# --- Events --------------------------------------------------------------------

events = []
UI::Timer.on_start { |sec| events << [:start, sec] }
UI::Timer.on_finish { events << [:finish] }
UI::Timer.on_stop { |sec| events << [:stop, sec] }

UI::Timer._dispatch(timer_event(:start, 3))
UI::Timer._dispatch(timer_event(:tick, 2))
UI::Timer._dispatch(timer_event(:tick, 1))
UI::Timer._dispatch(timer_event(:tick, 0))
UI::Timer._dispatch(timer_event(:finish, 0))
check "on_tick gets the time set, then each second down to 0", [3, 2, 1, 0], ticks
check "on_start before the ticks, on_finish at the end", [[:start, 3], [:finish]], events

events.clear
UI::Timer._dispatch(timer_event(:stop, 42))
check "on_stop gets the seconds left", [[:stop, 42]], events

ticks2 = []
UI::Timer.on_tick { |sec| ticks2 << sec }
UI::Timer._dispatch(timer_event(:tick, 5))
check "on_tick again replaces the block", [[3, 2, 1, 0], [5]], [ticks, ticks2]

UI::Timer.on_tick
UI::Timer._dispatch(timer_event(:tick, 4))
check "on_tick without a block clears it", [5], ticks2

UI::Timer.on_tick { |sec| raise "boom" }
UI::Timer._dispatch(timer_event(:tick, 3))
check "an error in a block is logged, not raised", true, $logs.last.include?("boom")

zero_arity = 0
UI::Timer.on_tick { zero_arity += 1 }
UI::Timer._dispatch(timer_event(:tick, 2))
check "a block without a parameter is called too", 1, zero_arity

UI::Timer.start
UI::Timer.stop
check "start / stop go through the tempo", [:start, :stop], $tempo

UI::Timer.show
check "show", [:show, :timer], $calls.last

# --- Layout --------------------------------------------------------------------

$ui_layouts = {}
$ui_widget_callbacks = {}
$calls.clear
UI::Timer.on_tick
UI::Timer.armed = false
laid = []
layout = UI.layout("Countdown", cols: 2, gap: 0) do
  timer col: 0, id: :t, seconds: 30 do |sec| laid << sec end
  pad col: 1, label: "Go"
end
check "timer goes over as a view of kind 8", [:view, 8, 0, 20, 160, 180], $calls[1]
check "the timer widget is UI::Timer", UI::Timer, layout[:t]
check "seconds: sets the time", 30, UI::Timer.seconds
check "a block is on_tick (and arms)", true, UI::Timer.armed?
UI::Timer._dispatch(timer_event(:tick, 29))
check "...and gets the ticks", [29], laid

raised = begin
  UI.layout("Two") do
    timer col: 0
    timer col: 1
  end
  false
rescue ArgumentError => e
  e.message.include?("only one timer")
end
check "one timer per layout", true, raised

puts "#{$passed} passed, #{$failed} failed"
# A bare mruby has no exit; the failure count above is the result there
exit($failed == 0 ? 0 : 1) if respond_to?(:exit, true)
raise "#{$failed} failed" if $failed > 0
