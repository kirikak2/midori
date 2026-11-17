# UI::Timer: a countdown that MIDI Start starts and that ends with MIDI Stop
# (docs/TIMER.md)
#
#   UI::Timer.seconds = 90
#   UI::Timer.on_tick do |sec|            # 90, 89, ... 0 (arms the timer)
#     dev.trigger(sec <= 10 ? 84 : 72, 100, duration: 50)
#   end
#   UI::Timer.on_finish { UI.log("Time up") }
#   UI::Timer.show
#   # Play on the Tempo or Timer screen (or UI::Tempo.start) starts it; at 0
#   # it stops the clock, which sends MIDI Stop wherever the tempo is routed.
#
# "Play" is MIDI::Clock's Start -- the Tempo screen's Play, UI::Tempo.start,
# MIDI::Clock.start -- and "Stop" is the clock's Stop, sent along the routes
# from UI::Tempo. The countdown runs in C (an esp_timer on whole seconds), so
# what the script does in between does not move it; the blocks run from
# UI.process (the UI-mode loop, MIDI.sleep_ms, the clock loops). One timer per
# process, like the tempo. In a layout: `timer area: :t`.
#
# Armed or not: a timer that is not armed ignores Start, so a minute set long
# ago never stops the music by surprise. on_tick / on_start / on_finish /
# on_stop and `armed = true` arm it for this script (the script's end takes it
# back); the screen's Arm button, and its Play, arm it until turned off.
module UI
  class Timer
    STATES = [:idle, :running, :stopped, :finished]

    @on_tick = nil
    @on_start = nil
    @on_finish = nil
    @on_stop = nil

    class << self
      # @return [Integer] the time set, in seconds
      def seconds
        UI._timer_seconds
      end

      # The time set, 1..5999 seconds (99:59). While counting, the next run
      # uses it; otherwise the display shows it at once.
      def seconds=(value)
        UI._timer_set_seconds(value.to_i)
      end

      # The time set from minutes and seconds: set(minutes: 1, seconds: 30)
      def set(minutes: 0, seconds: 0)
        self.seconds = minutes.to_i * 60 + seconds.to_i
      end

      # @return [Integer] seconds left (the time set while waiting)
      def remaining
        UI._timer_remaining
      end

      # @return [Symbol] :idle / :running / :stopped (by a Stop before 0) / :finished
      def state
        STATES[UI._timer_state] || :idle
      end

      def running?
        UI._timer_state == 1
      end

      def finished?
        UI._timer_state == 3
      end

      # Back to the time set. While counting, starts over (the clock keeps
      # running and on_start / on_tick get the time set again).
      def reset
        UI._timer_reset
        nil
      end

      def armed?
        UI._timer_armed
      end

      # true: Start starts the countdown (until the script ends). false:
      # disarmed, the screen's Arm too.
      def armed=(on)
        UI._timer_set_armed(on ? true : false)
      end

      def arm
        self.armed = true
        nil
      end

      def disarm
        self.armed = false
        nil
      end

      # MIDI Start / Stop through the tempo (as the screens' Play / Stop)
      def start
        UI::Tempo.start
      end

      def stop
        UI::Tempo.stop
      end

      # The block gets the seconds left on Start (the time set) and after
      # every second down, the last time with 0. One block: calling it again
      # replaces it, without a block clears it. Arms the timer.
      def on_tick(&block)
        @on_tick = block
        arm if block
        block
      end

      # Start: the block gets the time set. Arms the timer.
      def on_start(&block)
        @on_start = block
        arm if block
        block
      end

      # Time up: called after on_tick(0), with Stop already sent. Arms the timer.
      def on_finish(&block)
        @on_finish = block
        arm if block
        block
      end

      # A Stop before 0 halted it: the block gets the seconds left. Arms the timer.
      def on_stop(&block)
        @on_stop = block
        arm if block
        block
      end

      def show
        UI.show(:timer)
      end

      # Internal: UI.dispatch_event, for type :timer
      def _dispatch(event)
        sec = event[:remaining]
        case event[:kind]
        when :start
          _call(@on_start, sec)
          _call(@on_tick, sec)
        when :tick
          _call(@on_tick, sec)
        when :finish
          _call(@on_finish, nil)
        when :stop
          _call(@on_stop, sec)
        end
      end

      def _call(block, arg)
        return unless block
        begin
          if block.arity == 0
            block.call
          else
            block.call(arg)
          end
        rescue => e
          UI.log("Error in UI::Timer handler: " + e.message)
        end
      end
    end
  end
end
