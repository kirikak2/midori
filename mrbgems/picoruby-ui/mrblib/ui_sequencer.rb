# Sequencer (docs/SEQUENCER.md)
#
# A step / realtime sequencer of up to 16 lines and 16 patterns, and a
# picoruby-midi transport: it plays on the MIDI clock routed to it, takes the
# notes routed to it as the chord to place on the grid, and sends what it
# plays to `device:` and wherever MIDI.route sends what comes from it.
#
#   seq = UI::Sequencer.new(device: MIDIDevices.amy)   # clock: UI::Tempo
#   seq.line(0, name: "Kick", channel: 9).pattern("x...x...x...x.x.", notes: [36])
#   seq.line(1, name: "Keys")[0] = [60, 64, 67]
#   UI::Tempo.start
#
# The song lives in C and outlasts the script (UI::Sequencer.current picks it
# up without clearing it). Lines are the same in every pattern; what a line
# plays (its notes and length) is per pattern.
module UI
  class Sequencer
    MAX_LINES     = 16
    DEFAULT_LINES = 8
    MAX_PATTERNS  = 16
    MAX_STEPS     = 64
    MAX_CHORD     = 8
    MAX_CHAIN     = 64
    MAX_REPEAT    = 16
    PPQN          = 96
    TICKS_PER_BAR = 384
    STEPS = [4, 8, 16, 32, 64]
    BARS  = [1, 2, 4, 8]
    VIEWS = [8, 16]
    MODES     = { grid: 0, realtime: 1 }
    RATES     = { 4 => 0, 8 => 1, 16 => 2, 32 => 3, :"8t" => 4, :"16t" => 5 }
    SWITCHES  = { pattern: 0, bar: 1 }
    REC_MODES = { overdub: 0, replace: 1 }
    EDIT = -2    # The pattern being edited (follows the playing one)
    SONG = -1

    # The sequencer as it is, without clearing anything
    def self.current
      $ui_sequencer_instance || new(reset: false)
    end

    # Internal: a symbol / value looked up in one of the tables above
    def self.code(table, value, what)
      code = table[value]
      raise ArgumentError, "#{what} must be one of #{table.keys.inspect}" if code.nil?
      code
    end

    def self.key_of(table, code)
      table.each { |k, v| return k if v == code }
      nil
    end

    # Internal: what MIDI::Clock is in this build -- UI::Tempo's clock, when
    # there is one
    def self.default_clock
      UI::Tempo.transport_id != 0 ? UI::Tempo : nil
    end

    # @param lines [Integer] 1..16 (default 8)
    # @param clock [Object, nil] where the clock comes from (default UI::Tempo);
    #   an external device, or nil to route none
    # @param input [Object, Array, nil] where notes come from (the chord to
    #   place, what realtime lines record); one or several
    # @param device [Object, Array, nil] where it plays besides MIDI.route
    # @param view [Integer] steps on screen: 8 or 16
    # @param autosave [Boolean] keep this song as the user's (saved
    #   automatically); otherwise it is the script's own
    # @param reset [Boolean] false: take the song as it is (UI::Sequencer.current)
    def initialize(lines: DEFAULT_LINES, clock: :tempo, input: nil, device: nil, view: nil,
                   autosave: false, reset: true)
      $ui_sequencer_instance = self
      @devices = []
      @inputs = []
      @clock = nil
      @routes = []
      @view = 16
      @autosave = autosave
      return unless reset

      if !lines.is_a?(Integer) || lines < 1 || lines > MAX_LINES
        raise ArgumentError, "lines must be 1..#{MAX_LINES}"
      end
      UI._seq_reset(lines)
      self.device = device
      self.view = view if view
      @clock = clock == :tempo ? Sequencer.default_clock : clock
      @inputs = Sequencer.list(input)
      _reroute
    end

    # Internal: nil / one / an Array of several, as an Array
    def self.list(value)
      return [] if value.nil?
      value.is_a?(Array) ? value : [value]
    end

    # --- Transport (picoruby-midi) ---

    # Its bit in picoruby-midi's transport registry (0 on a board without a
    # display, where there is no sequencer)
    def transport_id
      UI._seq_get_i(SONG, SONG, :transport_bit)
    end

    # A packet sent to the sequencer: the clock drives it, notes are its input
    def send_packet(cable, cin, midi1, midi2, midi3)
      UI._seq_receive(cin, midi1, midi2, midi3)
    end

    def bytes_available
      0
    end

    def read_available
      nil
    end

    def connected?
      transport_id != 0
    end

    def device_info
      "Sequencer"
    end

    # --- Where it plays, and what drives it ---

    def device
      @devices.size == 1 ? @devices[0] : @devices
    end

    # One MIDI::Device / transport, an Array of them, or nil for none
    def device=(dev)
      @devices = Sequencer.list(dev)
      mask = 0
      @devices.each { |d| mask |= UI::Tombola.transport_mask_for(d) }
      UI._seq_set_i(SONG, SONG, :out_mask, mask)
    end

    attr_reader :clock

    # Where the clock comes from (nil: none routed by the sequencer)
    def clock=(source)
      @clock = source
      _reroute
    end

    def input
      @inputs.size == 1 ? @inputs[0] : @inputs
    end

    # Where notes come from: one, an Array of several, or nil
    def input=(source)
      @inputs = Sequencer.list(source)
      _reroute
    end

    # Internal: route the clock and the inputs to the sequencer. The clock
    # must come from one source only (two would play it twice as fast), so
    # inputs get their channels only, and a source that is both gets one
    # route carrying both.
    def _reroute
      @routes.each { |r| r.unroute }
      @routes = []
      return if transport_id == 0

      sources = []   # [source, notes?, clock?]
      add = lambda do |src, notes, clk|
        bit = MIDI.route_bit(src)
        entry = sources.find { |e| MIDI.route_bit(e[0]) == bit }
        if entry
          entry[1] ||= notes
          entry[2] ||= clk
        else
          sources << [src, notes, clk]
        end
      end
      add.call(@clock, false, true) if @clock
      @inputs.each { |src| add.call(src, true, false) }

      sources.each do |src, notes, clk|
        channels = notes ? (0..15).to_a : []
        channels << :clock if clk
        route = MIDI.route(src, self, channel: channels)
        @routes << route if route
      end
    end

    # --- Song ---

    def name
      UI._seq_name(SONG)
    end

    def name=(s)
      UI._seq_set_name(SONG, s.to_s)
    end

    # Number of lines (1..16)
    def lines
      UI._seq_get_i(SONG, SONG, :line_count)
    end

    # A line of the pattern being edited, configured with `opts` (see Line#configure)
    def line(index, **opts)
      check_line(index)
      l = Line.new(self, EDIT, index)
      l.configure(**opts) unless opts.empty?
      l
    end

    # Adds a line at the end. Returns it, or false with 16 lines.
    def add_line(**opts)
      index = UI._seq_add_line
      return false if index < 0
      line(index, **opts)
    end

    def remove_line(index)
      check_line(index)
      UI._seq_remove_line(index)
    end

    # Notes in the whole song (at most 8192)
    def song_notes
      UI._seq_get_i(SONG, SONG, :song_notes)
    end

    # --- Patterns ---

    def pattern(index)
      Pattern.new(self, Sequencer.check_pattern(index))
    end

    # The pattern playing (or to play on Start)
    def playing_pattern
      UI._seq_get_i(SONG, SONG, :play_pattern)
    end

    # Plays `index` now when stopped; while running, when the playing pattern
    # ends (or at the next bar: switch = :bar). Turns the chain off.
    def play_pattern=(index)
      UI._seq_set_i(SONG, SONG, :play_pattern, Sequencer.check_pattern(index))
    end

    # The pattern queued to play next, or nil
    def next_pattern
      n = UI._seq_get_i(SONG, SONG, :next_pattern)
      n < 0 ? nil : n
    end

    def switch
      Sequencer.key_of(SWITCHES, UI._seq_get_i(SONG, SONG, :switch))
    end

    # :pattern (at the end of the playing pattern) or :bar (at the next bar)
    def switch=(mode)
      UI._seq_set_i(SONG, SONG, :switch, Sequencer.code(SWITCHES, mode, "switch"))
    end

    # The pattern Sequencer#line edits: a fixed one, or nil to follow the playing one
    def edit_pattern
      n = UI._seq_get_i(SONG, SONG, :edit_pattern)
      n < 0 ? nil : n
    end

    def edit_pattern=(index)
      UI._seq_set_i(SONG, SONG, :edit_pattern, index.nil? ? -1 : Sequencer.check_pattern(index))
    end

    # --- Chain ---

    # [[pattern, repeat], ...]
    def chain
      UI._seq_chain
    end

    # [[pattern, repeat], ...]; a bare pattern number plays once. At most 64.
    def chain=(entries)
      pats = []
      reps = []
      Sequencer.list(entries).each do |e|
        p, r = e.is_a?(Array) ? e : [e, 1]
        Sequencer.check_pattern(p)
        if !r.is_a?(Integer) || r < 1 || r > MAX_REPEAT
          raise ArgumentError, "repeat must be 1..#{MAX_REPEAT}"
        end
        pats << p
        reps << r
      end
      raise ArgumentError, "at most #{MAX_CHAIN} chain entries" if pats.size > MAX_CHAIN
      UI._seq_set_chain(pats, reps)
    end

    def chain_on?
      UI._seq_get_i(SONG, SONG, :chain_on) != 0
    end

    # true plays the chain (from its top on Start); needs entries
    def chain_on=(on)
      UI._seq_set_i(SONG, SONG, :chain_on, on ? 1 : 0)
    end

    # --- Chord ---

    # The notes placed on the grid: the last chord played into the sequencer
    def chord
      UI._seq_chord.map { |n, _v| n }
    end

    # [[note, velocity], ...]
    def chord_with_velocities
      UI._seq_chord
    end

    # A note, an Array of notes, or of [note, velocity] pairs (at most 8)
    def chord=(value)
      notes = []
      vels = []
      Sequencer.list(value).each do |v|
        n, vel = v.is_a?(Array) ? v : [v, 100]
        notes << n.to_i
        vels << vel.to_i
      end
      raise ArgumentError, "at most #{MAX_CHORD} notes in a chord" if notes.size > MAX_CHORD
      UI._seq_set_chord(notes, vels)
    end

    # --- State ---

    # Between a Start / Continue and a Stop on the clock it gets
    def running?
      UI._seq_get_i(SONG, SONG, :running) != 0
    end

    # { pattern:, tick:, bar:, beat: } in the playing pattern
    def position
      tick = UI._seq_get_i(SONG, SONG, :pattern_tick)
      { pattern: playing_pattern, tick: tick, bar: tick / TICKS_PER_BAR,
        beat: (tick % TICKS_PER_BAR) / PPQN }
    end

    attr_reader :view

    # Steps on screen: 8 or 16
    def view=(n)
      raise ArgumentError, "view must be 8 or 16" unless VIEWS.include?(n)
      @view = n
    end

    def autosave?
      @autosave
    end

    # Bring the Sequencer screen to the front
    def show
      UI.show(:sequencer)
    end

    # Internal
    def check_line(index)
      if !index.is_a?(Integer) || index < 0 || index >= lines
        raise ArgumentError, "line must be 0..#{lines - 1}"
      end
      index
    end

    def self.check_pattern(index)
      if !index.is_a?(Integer) || index < 0 || index >= MAX_PATTERNS
        raise ArgumentError, "pattern must be 0..#{MAX_PATTERNS - 1}"
      end
      index
    end

    # --- A pattern ---------------------------------------------------------

    class Pattern
      attr_reader :index

      def initialize(seq, index)
        @seq = seq
        @index = index
      end

      # Whether anything was ever put in it
      def used?
        UI._seq_get_i(@index, SONG, :used) != 0
      end

      # :auto (the longest line's) or 1..8 bars
      def length
        n = UI._seq_get_i(@index, SONG, :length)
        n == 0 ? :auto : n
      end

      def length=(bars)
        n = bars == :auto ? 0 : bars
        if !n.is_a?(Integer) || n < 0 || n > 8
          raise ArgumentError, "length must be :auto or 1..8 (bars)"
        end
        UI._seq_set_i(@index, SONG, :length, n)
      end

      # Its length in ticks (96 PPQN)
      def ticks
        UI._seq_get_i(@index, SONG, :ticks)
      end

      def line(n, **opts)
        @seq.check_line(n)
        l = Line.new(@seq, @index, n)
        l.configure(**opts) unless opts.empty?
        l
      end

      def clear
        UI._seq_clear_pattern(@index)
      end

      def copy_to(dst)
        UI._seq_copy_pattern(@index, Sequencer.check_pattern(dst))
      end

      def play
        @seq.play_pattern = @index
      end
    end

    # --- A line of a pattern -----------------------------------------------

    # Settings (name, channel, mode, mute, velocity, gate, quantize,
    # rec_mode) are the line's in every pattern; steps / rate / bars and the
    # notes are this pattern's.
    class Line
      attr_reader :index

      def initialize(seq, pattern, index)
        @seq = seq
        @pattern = pattern
        @index = index
      end

      # The pattern this is a line of (an index; EDIT: the one being edited)
      def pattern_index
        @pattern
      end

      def configure(name: nil, mode: nil, channel: nil, mute: nil, velocity: nil, gate: nil,
                    quantize: :keep, rec_mode: nil, steps: nil, rate: nil, bars: nil)
        self.name = name unless name.nil?
        self.mode = mode unless mode.nil?
        self.channel = channel unless channel.nil?
        self.mute = mute unless mute.nil?
        self.velocity = velocity unless velocity.nil?
        self.gate = gate unless gate.nil?
        self.quantize = quantize unless quantize == :keep
        self.rec_mode = rec_mode unless rec_mode.nil?
        self.steps = steps unless steps.nil?
        self.rate = rate unless rate.nil?
        self.bars = bars unless bars.nil?
        self
      end

      # --- The line's settings ---

      def name
        UI._seq_name(@index)
      end

      def name=(s)
        UI._seq_set_name(@index, s.to_s)
      end

      def mode
        Sequencer.key_of(MODES, get(:mode))
      end

      # :grid or :realtime (the other's notes are kept)
      def mode=(m)
        put(:mode, Sequencer.code(MODES, m, "mode"))
      end

      def channel
        get(:channel)
      end

      def channel=(ch)
        if !ch.is_a?(Integer) || ch < 0 || ch > 15
          raise ArgumentError, "channel must be 0..15"
        end
        put(:channel, ch)
      end

      def mute?
        get(:mute) != 0
      end

      def mute=(on)
        put(:mute, on ? 1 : 0)
      end

      # Grid: :input (the chord's velocities when placed) or 1..127
      def velocity
        v = get(:velocity)
        v == 0 ? :input : v
      end

      def velocity=(v)
        n = v == :input ? 0 : v
        if !n.is_a?(Integer) || n < 0 || n > 127
          raise ArgumentError, "velocity must be :input or 1..127"
        end
        put(:velocity, n)
      end

      # Grid: how much of a note's length sounds, 1..100 %
      def gate
        get(:gate)
      end

      def gate=(percent)
        if !percent.is_a?(Integer) || percent < 1 || percent > 100
          raise ArgumentError, "gate must be 1..100"
        end
        put(:gate, percent)
      end

      # Realtime recording: nil, or a rate notes start on (4, 8, 16, 32, :"8t", :"16t")
      def quantize
        q = get(:quantize)
        q == 0 ? nil : Sequencer.key_of(RATES, q - 1)
      end

      def quantize=(rate)
        put(:quantize, rate.nil? ? 0 : Sequencer.code(RATES, rate, "quantize") + 1)
      end

      def rec_mode
        Sequencer.key_of(REC_MODES, get(:rec_mode))
      end

      def rec_mode=(m)
        put(:rec_mode, Sequencer.code(REC_MODES, m, "rec_mode"))
      end

      # --- This pattern's length ---

      # Grid: 4 / 8 / 16 / 32 / 64
      def steps
        get(:steps)
      end

      def steps=(n)
        raise ArgumentError, "steps must be one of #{STEPS.inspect}" unless STEPS.include?(n)
        put(:steps, n)
      end

      # Grid: one step's length: 4, 8, 16, 32, :"8t" or :"16t"
      def rate
        Sequencer.key_of(RATES, get(:rate))
      end

      def rate=(r)
        put(:rate, Sequencer.code(RATES, r, "rate"))
      end

      # Realtime: the loop's length, 1 / 2 / 4 / 8 bars
      def bars
        get(:bars)
      end

      def bars=(n)
        raise ArgumentError, "bars must be one of #{BARS.inspect}" unless BARS.include?(n)
        put(:bars, n)
      end

      # Its length in ticks (96 PPQN)
      def ticks
        get(:ticks)
      end

      # --- Grid notes (steps) ---

      # The notes starting at `step`: [{ note:, length:, velocity: }, ...]
      def [](step)
        check_step(step)
        result = []
        UI._seq_notes(@pattern, @index, 0).each do |pos, len, note, vel|
          result << { note: note, length: len, velocity: vel } if pos == step
        end
        result
      end

      # Puts a note / chord at `step` (one step long), replacing what started
      # there. nil or [] clears it.
      def []=(step, notes)
        if notes.nil? || notes == []
          clear(step)
        else
          set(step, notes: notes)
        end
      end

      # Replaces what starts at `step` with `notes`, `length` steps long
      def set(step, notes:, length: 1, velocity: nil)
        check_step(step)
        UI._seq_remove(@pattern, @index, step, -1)
        place(step, Sequencer.list(notes), length, velocity)
      end

      # Adds one note at `step` (others starting there stay)
      def add(step, note:, length: 1, velocity: nil)
        check_step(step)
        place(step, [note], length, velocity)
      end

      # Removes `note` at `step`, or everything starting there
      def remove(step, note: nil)
        check_step(step)
        UI._seq_remove(@pattern, @index, step, note.nil? ? -1 : note)
      end

      # Makes the notes starting at `step` `length` steps long
      def set_length(step, length)
        check_step(step)
        UI._seq_set_length(@pattern, @index, step, length)
      end

      # Removes the notes starting at `step`, or (no step) every note of the
      # line's current mode
      def clear(step = nil)
        if step.nil?
          UI._seq_clear(@pattern, @index, MODES[mode])
        else
          remove(step)
        end
        nil
      end

      # A whole grid from a string, one character a step: x (a note), . (none),
      # - (the previous note goes on). | is ignored, for readability.
      #   l.pattern("x...x...|x...x.x-", notes: [36])
      def pattern(str, notes: nil, velocity: nil)
        notes = Sequencer.list(notes || @seq.chord)
        UI._seq_clear(@pattern, @index, 0)
        starts = []   # [step, length]
        step = 0
        str.each_char do |c|
          next if c == "|"
          break if step >= MAX_STEPS
          case c
          when "x", "X"
            starts << [step, 1]
          when "-"
            starts.last[1] += 1 if starts.last && starts.last[0] + starts.last[1] == step
          end
          step += 1
        end
        starts.each { |s, len| place(s, notes, len, velocity) }
        self
      end

      # --- Notes as they are stored ---

      # The notes of the line's current mode: [{ pos:, len:, note:, velocity: }, ...]
      # (grid: steps, realtime: ticks)
      def notes
        UI._seq_notes(@pattern, @index, MODES[mode]).map do |pos, len, note, vel|
          { pos: pos, len: len, note: note, velocity: vel }
        end
      end

      # Replaces the notes of the line's current mode
      def notes=(list)
        m = MODES[mode]
        UI._seq_clear(@pattern, @index, m)
        list.each do |n|
          pos = n[:pos].to_i
          len = (n[:len] || 1).to_i
          note = n[:note].to_i
          vel = (n[:velocity] || 100).to_i
          if m == 0
            UI._seq_place(@pattern, @index, pos, len, [note], [vel])
          else
            UI._seq_add_event(@pattern, @index, pos, len, note, vel)
          end
        end
      end

      # Realtime: adds a note at `pos` ticks, `len` ticks long
      def add_event(pos:, len:, note:, velocity: 100)
        UI._seq_add_event(@pattern, @index, pos, len, note, velocity)
      end

      private

      def get(name)
        UI._seq_get_i(@pattern, @index, name)
      end

      def put(name, value)
        unless UI._seq_set_i(@pattern, @index, name, value)
          raise ArgumentError, "#{name}: cannot set #{value.inspect}"
        end
        value
      end

      def check_step(step)
        if !step.is_a?(Integer) || step < 0 || step >= MAX_STEPS
          raise ArgumentError, "step must be 0..#{MAX_STEPS - 1}"
        end
      end

      # Velocity: the given one, else the line's fixed one, else 100
      def place(step, notes, length, velocity)
        v = velocity
        if v.nil?
          fixed = get(:velocity)
          v = fixed == 0 ? 100 : fixed
        end
        placed = UI._seq_place(@pattern, @index, step, length.to_i, notes.map(&:to_i),
                               notes.map { v })
        raise ArgumentError, "cannot place notes at step #{step}" if placed < 0
        placed
      end
    end
  end
end
