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
      # The script's own song is never autosaved, and gives way to the user's
      # (the autosave) when the script ends -- unless autosave: true
      UI._seq_set_i(SONG, SONG, :temporary, autosave ? 0 : 1)
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

    # The line armed to record (Line#arm), or nil
    def armed
      n = UI._seq_get_i(SONG, SONG, :armed)
      n < 0 ? nil : line(n)
    end

    # Disarms whatever line is armed
    def disarm
      UI._seq_set_i(SONG, SONG, :armed, -1)
      nil
    end

    # A line armed, and running: what comes in is being recorded
    def recording?
      UI._seq_get_i(SONG, SONG, :recording) != 0
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

    # --- Saving ---

    # Saves the song as `name` (in UI::Sequencer.dir, or a full path); with
    # no name, under its own name, or a new SONGnnn. Returns the name.
    def save(name = nil)
      n = name || self.name
      n = Sequencer.next_auto_name if n.nil? || n.empty?
      Sequencer.save_as(n)
    end

    # Loads a song saved by name (or a full path). tempo: true also sets the
    # Tempo to the BPM it was saved at.
    def load(name, tempo: false)
      Sequencer.load_from(name, tempo: tempo)
      self
    end

    # Changed since it was last saved by name
    def dirty?
      UI._seq_get_i(SONG, SONG, :dirty) != 0
    end

    # The song as plain Ruby data, to keep in a script and give back to restore
    def to_h
      h = { name: name, lines: [], patterns: {}, chain: chain, chain_on: chain_on? }
      lines.times do |i|
        l = line(i)
        h[:lines] << { name: l.name, mode: l.mode, channel: l.channel, mute: l.mute?,
                       velocity: l.velocity, gate: l.gate, quantize: l.quantize,
                       rec_mode: l.rec_mode }
      end
      MAX_PATTERNS.times do |p|
        pat = pattern(p)
        next unless pat.used?
        ph = { length: pat.length, lines: [] }
        lines.times do |i|
          pl = pat.line(i)
          ph[:lines] << { steps: pl.steps, rate: pl.rate, bars: pl.bars,
                          program: pl.program, bank: pl.bank,
                          grid: UI._seq_notes(p, i, 0), rt: UI._seq_notes(p, i, 1) }
        end
        h[:patterns][p] = ph
      end
      h
    end

    # Replaces the song with one from to_h
    def restore(h)
      line_hashes = h[:lines] || []
      raise ArgumentError, "restore: no lines" if line_hashes.empty?
      UI._seq_reset(line_hashes.size)
      self.name = h[:name].to_s
      line_hashes.each_with_index do |lh, i|
        l = line(i)
        l.name = lh[:name].to_s
        l.mode = lh[:mode] if lh[:mode]
        l.channel = lh[:channel] if lh[:channel]
        l.mute = lh[:mute] ? true : false
        l.velocity = lh[:velocity] if lh[:velocity]
        l.gate = lh[:gate] if lh[:gate]
        l.quantize = lh[:quantize]
        l.rec_mode = lh[:rec_mode] if lh[:rec_mode]
      end
      (h[:patterns] || {}).each do |p, ph|
        pat = pattern(p)
        pat.length = ph[:length] || :auto
        (ph[:lines] || []).each_with_index do |plh, i|
          next if i >= line_hashes.size
          pl = pat.line(i)
          pl.steps = plh[:steps] if plh[:steps]
          pl.rate = plh[:rate] if plh[:rate]
          pl.bars = plh[:bars] if plh[:bars]
          pl.program = plh[:program]
          pl.bank = plh[:bank]
          (plh[:grid] || []).each { |pos, len, n, v| UI._seq_place(p, i, pos, len, [n], [v]) }
          (plh[:rt] || []).each { |pos, len, n, v| UI._seq_add_event(p, i, pos, len, n, v) }
        end
      end
      self.chain = h[:chain] || []
      self.chain_on = h[:chain_on] ? true : false if (h[:chain] || []).size > 0
      self
    end

    # Requests from the File sheet (ui_seq_request_t)
    REQ_SAVE     = 1
    REQ_SAVE_NEW = 2
    REQ_LOAD     = 3
    REQ_NEW      = 4
    REQ_DELETE   = 5
    REQ_LIST     = 6

    EXT = ".mseq"
    AUTOSAVE = "_autosave"
    NAME_MAX = 16
    AUTOSAVE_RETRY_MS = 10000

    class << self
      # Where songs go: the SD card when there is one, else the flash
      def dir
        $sd_available ? "/sd/patterns" : "/patterns"
      end

      # The saved songs, by name (the autosave and other "_" files left out)
      def songs
        names = []
        begin
          Dir.open(dir) do |d|
            while entry = d.read
              next if entry.start_with?("_") || !entry.end_with?(EXT)
              names << entry[0, entry.size - EXT.size]
            end
          end
        rescue => e
          return []   # No directory yet
        end
        names.sort
      end

      def next_auto_name
        taken = songs
        n = 1
        n += 1 while taken.include?(format_auto(n))
        format_auto(n)
      end

      def format_auto(n)
        s = n.to_s
        s = "0" + s while s.size < 3
        "SONG" + s
      end

      # A name given by hand: letters, digits, - and _, not starting with _
      def check_name(name)
        ok = name.size >= 1 && name.size <= NAME_MAX && !name.start_with?("_")
        name.each_char do |c|
          ok = false unless (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") ||
                            (c >= "0" && c <= "9") || c == "-" || c == "_"
        end
        raise ArgumentError, "bad song name: #{name.inspect}" unless ok
        name
      end

      def path_for(name)
        return name if name.include?("/")
        "#{dir}/#{name}#{EXT}"
      end

      def basename(name)
        base = name.include?("/") ? name.split("/").last : name
        base.end_with?(EXT) ? base[0, base.size - EXT.size] : base
      end

      def ensure_dir
        Dir.mkdir(dir) unless Dir.exist?(dir)
      rescue => e
        nil
      end

      def save_as(name)
        base = basename(name)
        check_name(base) unless name.include?("/")
        ensure_dir
        UI._seq_set_name(SONG, base)
        _store_tempo
        unless write_file(path_for(name), false, true, false)
          raise IOError, "cannot save #{base}"
        end
        base
      end

      def load_from(name, tempo: false)
        base = basename(name)
        err = read_file(path_for(name), false)
        raise IOError, "#{base}: not found" if err == :missing
        raise IOError, "#{base}: #{UI._seq_load_error(err)}" if err != 0
        if tempo
          bpm = UI._seq_get_i(SONG, SONG, :bpm_x100)
          UI::Tempo.bpm = bpm / 100.0 if bpm > 0
        end
        base
      end

      def delete(name)
        path = path_for(name)
        raise IOError, "#{basename(name)}: not found" unless File.exist?(path)
        File.unlink(path)
        File.unlink(path + ".bak") if File.exist?(path + ".bak")
        true
      end

      # Writes the song to path: into path.tmp first, then swapped in (FAT
      # cannot rename over a file, so the old one goes to path.bak meanwhile;
      # a reader falls back to it). Returns whether it worked.
      def write_file(path, dirty_flag, saved, autosave)
        return false unless UI._seq_dump_begin(dirty_flag)
        tmp = path + ".tmp"
        bak = path + ".bak"
        ok = false
        begin
          File.open(tmp, "w") do |f|
            while piece = UI._seq_dump_next
              f.write(piece)
            end
          end
          File.unlink(bak) if File.exist?(bak)
          File.rename(path, bak) if File.exist?(path)
          File.rename(tmp, path)
          File.unlink(bak) if File.exist?(bak)
          ok = true
        rescue => e
          ok = false
        ensure
          UI._seq_dump_end(ok && saved, ok && autosave)
        end
        ok
      end

      # Reads path (or what was left in path.bak) into the song. Returns 0,
      # :missing, or a negative error.
      def read_file(path, autosave)
        target = File.exist?(path) ? path : (File.exist?(path + ".bak") ? path + ".bak" : nil)
        return :missing unless target
        return -8 unless UI._seq_load_begin
        err = 0
        File.open(target, "r") do |f|
          while err == 0 && (buf = f.read(1024))
            err = UI._seq_load_feed(buf)
          end
        end
        if err != 0
          UI._seq_load_abort
          return err
        end
        UI._seq_load_commit(autosave)
      rescue => e
        UI._seq_load_abort
        -7
      end

      def _store_tempo
        bpm = UI::Tempo.bpm
        UI._seq_set_i(SONG, SONG, :bpm_x100, (bpm * 100).to_i) if bpm && bpm > 0
      rescue => e
        nil
      end

      def autosave_now
        ensure_dir
        _store_tempo
        dirty = UI._seq_get_i(SONG, SONG, :dirty) != 0
        write_file(path_for(AUTOSAVE), dirty, false, true)
      end

      # The user's song back: the autosave, or an empty song when there is none
      def restore_autosave
        err = read_file(path_for(AUTOSAVE), true)
        return true if err == 0
        puts "Sequencer: autosave #{UI._seq_load_error(err)}" if err != :missing
        UI._seq_reset(DEFAULT_LINES)
        UI._seq_set_i(SONG, SONG, :temporary, 0)
        UI._seq_set_i(SONG, SONG, :mark_saved, 1)
        false
      end

      # At start-up, in every mode (main_task_base.rb): the autosave the first
      # time after power-up, and again after a script's own song; then follow
      # the File sheet and the autosave from every run of the MIDI filters.
      def _setup
        return if UI._seq_get_i(SONG, SONG, :transport_bit) == 0   # No sequencer here
        if UI._seq_get_i(SONG, SONG, :restored) == 0 || UI._seq_get_i(SONG, SONG, :temporary) != 0
          restore_autosave
          UI._seq_set_i(SONG, SONG, :restored, 1)
        end
        _send_list
        _hook
      end

      def _hook
        return if $ui_seq_hooked
        $ui_seq_hooked = true
        $__midi_filter_hooks__ << lambda { Sequencer._poll }
      end

      def _send_list
        UI._seq_set_files(songs.join("\n"))
      end

      def _now_ms
        MIDI.respond_to?(:_uptime_ms) ? MIDI._uptime_ms : 0
      end

      def _poll
        return if $ui_seq_polling   # A filter run from inside file I/O
        $ui_seq_polling = true
        begin
          req = UI._seq_take_request
          _handle(req[0], req[1]) if req
          _autosave_if_due
        ensure
          $ui_seq_polling = false
        end
      end

      # Failed autosaves (no card...) are retried every AUTOSAVE_RETRY_MS
      def _autosave_if_due
        return if UI._seq_get_i(SONG, SONG, :autosave_due) == 0
        retry_at = $ui_seq_autosave_retry_at
        return if retry_at && _now_ms < retry_at
        if autosave_now
          $ui_seq_autosave_retry_at = nil
        else
          $ui_seq_autosave_retry_at = _now_ms + AUTOSAVE_RETRY_MS
          UI._seq_status("Autosave failed")
        end
      end

      def _handle(op, name)
        case op
        when REQ_SAVE
          n = save_as(name.empty? ? next_auto_name : name)
          UI._seq_status("Saved " + n)
          _send_list
        when REQ_SAVE_NEW
          n = save_as(next_auto_name)
          UI._seq_status("Saved " + n)
          _send_list
        when REQ_LOAD
          UI._seq_status("Loaded " + load_from(name))
        when REQ_NEW
          UI._seq_reset(DEFAULT_LINES)
          UI._seq_set_i(SONG, SONG, :temporary, 0)
          UI._seq_set_i(SONG, SONG, :mark_clean, 1)
          UI._seq_status("New song")
        when REQ_DELETE
          delete(name)
          UI._seq_status("Deleted " + name)
          _send_list
        when REQ_LIST
          _send_list
        end
      rescue => e
        UI._seq_status(e.message)
      end
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
                    quantize: :keep, rec_mode: nil, steps: nil, rate: nil, bars: nil,
                    program: :keep, bank: :keep)
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
        self.program = program unless program == :keep
        self.bank = bank unless bank == :keep
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

      # --- Recording (realtime lines; one line armed at a time) ---

      # Records the notes played into the sequencer while it runs, into the
      # playing pattern. Arming another line disarms this one.
      def arm
        unless UI._seq_set_i(SONG, SONG, :armed, @index)
          raise ArgumentError, "line #{@index}: only a realtime line records"
        end
        self
      end

      # Notes still held end where they are
      def disarm
        UI._seq_set_i(SONG, SONG, :armed, -1) if armed?
        self
      end

      def armed?
        UI._seq_get_i(SONG, SONG, :armed) == @index
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

      # --- This pattern's sound ---

      # Program Change sent on the line's channel when this pattern starts
      # (Start, Continue, a switch to it -- not when it loops): 0..127, or nil
      # for none (the line keeps whatever sound it had). Changing it on the
      # playing pattern sends it at once.
      def program
        v = get(:program)
        v < 0 ? nil : v
      end

      def program=(n)
        if !n.nil? && (!n.is_a?(Integer) || n < 0 || n > 127)
          raise ArgumentError, "program must be 0..127 or nil"
        end
        put(:program, n.nil? ? -1 : n)
      end

      # Bank Select sent before the program: [msb, lsb] (each 0..127 or nil),
      # or nil when neither is set
      def bank
        msb = get(:bank_msb)
        lsb = get(:bank_lsb)
        return nil if msb < 0 && lsb < 0
        [msb < 0 ? nil : msb, lsb < 0 ? nil : lsb]
      end

      # An Integer (MSB, CC 0), [msb, lsb] (CC 0 / CC 32), or nil for none
      def bank=(value)
        msb, lsb = value.is_a?(Array) ? value : [value, nil]
        [msb, lsb].each do |v|
          if !v.nil? && (!v.is_a?(Integer) || v < 0 || v > 127)
            raise ArgumentError, "bank must be 0..127, [msb, lsb] or nil"
          end
        end
        put(:bank_msb, msb.nil? ? -1 : msb)
        put(:bank_lsb, lsb.nil? ? -1 : lsb)
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

      # Copies this line (its notes and steps / rate / bars) over line `index`
      # of `pattern` (default: this line's pattern)
      def copy_to(index, pattern: nil)
        @seq.check_line(index)
        dst = pattern.nil? ? @pattern : Sequencer.check_pattern(pattern)
        UI._seq_copy_line(@pattern, @index, dst, index)
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
