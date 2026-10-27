# AMY panel (docs/AMY_PANEL.md)
#
# A layout for building AMY parts by touch: one tab per part (AMY.parts),
# "+" adds an AMY.add_fm or AMY.add_synth, and each tab has the part's
# preset, MIDI channel, envelopes and main parameters, with a one-octave
# keyboard to try it out.
#
#   panel = UI::AMYPanel.new
#   panel.show
#
# Built on the layout DSL in Ruby only: a change of structure (another tab,
# a part added or removed, another FM operator) defines the layout again,
# a change of values (preset, channel) rewrites the widgets in place.

$ui_amy_panel = nil

module UI
  class AMYPanel
    MAX_TABS = 8

    KIND_COLORS = { fm: :magenta, synth: :cyan }
    KIND_TAGS   = { fm: "FM", synth: "SY" }
    DIM = 0x3186   # UI.rgb(48, 48, 48): pads that only show a value

    FM_FIRST_PATCH = 128
    FM_LAST_PATCH  = 255
    JUNO_LAST_PATCH = 127
    INIT_PATCH = -1   # AMY::Synth's initial voice (patch nil)

    LOW_MIN = 24   # C1
    LOW_MAX = 96   # C7

    NAME_LENGTH = 12

    # [key, label, lo, hi, curve, default]
    #   curve :lin / :log map 0..127 onto lo..hi, :int is lo..hi itself,
    #   :log_off is :log with the top of the knob meaning "no filter" (nil)
    SYNTH_KNOBS = [
      [:cutoff,     "Cutoff", 100.0, 8000.0, :log, 4000.0],
      [:resonance,  "Reso",   0.7,   8.0,    :lin, 0.7],
      [:filter_env, "FltEnv", 0.0,   6.0,    :lin, 0.0],
      [:key_track,  "KeyTrk", 0.0,   1.0,    :lin, 0.0],
      [:glide,      "Glide",  0.0,   500.0,  :lin, 0.0],
      [:pan,        "Pan",    0.0,   1.0,    :lin, 0.5],
      [:volume,     "Volume", 0.0,   1.0,    :lin, 1.0],
      [:reverb,     "Reverb", 0.0,   1.0,    :lin, 0.0]
    ]

    FM_KNOBS = [
      [:algorithm,   "Algo",   1,     32,     :int,     1],
      [:feedback,    "Feedbk", 0.0,   1.0,    :lin,     0.0],
      [:op_level,    "OpLvl",  0.0,   1.0,    :lin,     1.0],
      [:op_ratio,    "Ratio",  0.5,   16.0,   :log,     1.0],
      [:filter_freq, "Cutoff", 100.0, 8000.0, :log_off, nil],
      [:resonance,   "Reso",   0.7,   8.0,    :lin,     0.7],
      [:volume,      "Volume", 0.0,   1.0,    :lin,     1.0],
      [:reverb,      "Reverb", 0.0,   1.0,    :lin,     0.0]
    ]

    # --- Pure helpers (host-tested) ---------------------------------------

    # The patch `step` away from `patch`, wrapping within the kind's range.
    # A Synth's initial voice is nil, before preset 0.
    def self.next_patch(kind, patch, step)
      if kind == :fm
        lo = FM_FIRST_PATCH
        hi = FM_LAST_PATCH
        p = patch.nil? ? lo : patch
      else
        lo = INIT_PATCH
        hi = JUNO_LAST_PATCH
        p = patch.nil? ? INIT_PATCH : patch
      end
      size = hi - lo + 1
      n = (p - lo + step) % size
      n += size if n < 0
      n += lo
      (kind != :fm && n == INIT_PATCH) ? nil : n
    end

    # Display name of a part's preset
    def self.patch_label(kind, patch)
      return "Init" if patch.nil?
      Object.const_defined?(:AMY) ? AMY.patch_name(patch) : "Patch #{patch}"
    end

    # "FM 1 E.PIANO 1": kind, channel (1-based, "-" for none), preset
    def self.tab_label(kind, channel, name)
      ch = channel.nil? ? "-" : (channel + 1).to_s
      name = name.to_s
      name = name[0, NAME_LENGTH] if name.length > NAME_LENGTH
      "#{KIND_TAGS[kind]} #{ch} #{name}"
    end

    # The lowest channel none of `parts` listens on (0 when all are taken)
    def self.free_channel(parts)
      used = parts.map { |p| p.channel }
      ch = 0
      while ch < 16
        return ch unless used.include?(ch)
        ch += 1
      end
      0
    end

    # MIDI channel `step` away, 0..15 wrapping
    def self.next_channel(channel, step)
      n = ((channel || 0) + step) % 16
      n += 16 if n < 0
      n
    end

    def self.knobs_for(kind)
      kind == :fm ? FM_KNOBS : SYNTH_KNOBS
    end

    # Parameter value -> knob position (nil: the spec's default)
    def self.knob_from_value(spec, value)
      lo = spec[2]
      hi = spec[3]
      curve = spec[4]
      return 127.0 if curve == :log_off && value.nil?
      value = spec[5] if value.nil?
      return (curve == :int ? lo : 0).to_f if value.nil?
      case curve
      when :int
        v = value.to_i
        v = lo if v < lo
        v = hi if v > hi
        v.to_f
      when :log, :log_off
        v = value.to_f
        v = lo if v < lo
        v = hi if v > hi
        k = Math.log(v / lo) / Math.log(hi / lo) * 127.0
        k = 126.0 if curve == :log_off && k > 126.0
        k.round.to_f
      else
        t = (value.to_f - lo) / (hi - lo)
        t = 0.0 if t < 0.0
        t = 1.0 if t > 1.0
        (t * 127.0).round.to_f
      end
    end

    # Knob position -> parameter value (nil: no filter, for :log_off)
    def self.value_from_knob(spec, k)
      lo = spec[2]
      hi = spec[3]
      curve = spec[4]
      case curve
      when :int
        k.round
      when :log, :log_off
        return nil if curve == :log_off && k >= 127
        lo * ((hi / lo) ** (k.to_f / 127.0))
      else
        lo + (hi - lo) * k.to_f / 127.0
      end
    end

    # What a part reports for a knob's parameter (op: the FM operator)
    def self.read_param(part, key, op)
      case key
      when :op_level then part.op(op).level
      when :op_ratio then part.op(op).ratio
      when :filter_freq then part.filter == :none ? nil : part.filter_freq
      else part.respond_to?(key) ? part.send(key) : nil
      end
    end

    def self.write_param(part, key, op, value)
      case key
      when :op_level then part.op(op).level = value
      when :op_ratio then part.op(op).ratio = value
      when :filter_freq
        value.nil? ? (part.filter = :none) : (part.filter_freq = value)
      when :glide then part.glide = value.round
      else part.send("#{key}=".to_sym, value)
      end
    end

    # --- Instance ----------------------------------------------------------

    def self.current
      $ui_amy_panel
    end

    # Internal: from UI.process, after the events (see #request)
    def self._poll
      $ui_amy_panel._poll unless $ui_amy_panel.nil?
    end

    # True while the panel is on screen (or about to be)
    def self.showing?
      p = $ui_amy_panel
      return false if p.nil? || p.layout.nil? || p.layout.layout_slot.nil?
      s = UI._requested_screen
      s[0] == UI::SCREEN_LAYOUT && s[1] == p.layout.layout_slot
    end

    attr_reader :layout, :part

    # title:  the layout's title (Home tile)
    # engine: a Proc returning the AMY transport, called before the first
    #         part is added (it starts AMY); default AMY::Transport.instance
    # low:    the keyboard's lowest note
    def initialize(title: "AMY", engine: nil, low: 60)
      @title = title.to_s
      @engine = engine
      @low = low
      @parts = []
      @part = nil
      @op = 1
      @choosing = false
      @confirm_delete = false
      @pending = nil
      @keys = nil
      @layout = nil
      $ui_amy_panel = self
      rebuild
    end

    def show
      @layout.show
    end

    # The parts as AMY has them now, onto the tabs
    def refresh
      rebuild
      self
    end

    def select(part)
      @part = part
      @op = 1
      rebuild
      self
    end

    # Internal: a pad's work, done after UI.process has dispatched the rest
    # of its events (a redefinition renumbers the widgets, so an event still
    # queued for an old one would land on a new one)
    def request(action, arg = nil)
      @pending = [action, arg]
    end

    def _poll
      unless @pending.nil?
        action, arg = @pending
        @pending = nil
        perform(action, arg)
        return
      end
      rebuild if current_parts != @parts
    end

    # The page against a layout builder (also what the host test resolves)
    def build_page(l)
      tabs_row(l)
      if @part.nil?
        l.pad(row: 1..3, label: "Tap + to add a part", color: DIM)
        return
      end
      header_row(l)
      body_row(l)
      keys_row(l)
    end

    # --- Internal ---

    private

    def current_parts
      Object.const_defined?(:AMY) ? AMY.parts : []
    end

    def engine
      @engine.nil? ? AMY::Transport.instance : @engine.call
    end

    def rebuild
      @parts = current_parts
      @part = nil if @part && (@part.removed? || !@parts.include?(@part))
      @part = @parts[0] if @part.nil? && !@parts.empty?
      @confirm_delete = false
      if @part && @keys.nil?
        @keys = UI::Keys.new(octaves: 1, low: @low, channel: @part.channel || 0,
                             device: engine)
      end
      @keys.channel = @part.channel || 0 if @part && @keys
      @layout = UI.layout(@title, rows: [1, 1, 4, 2]) { |l| build_page(l) }
    end

    def perform(action, arg)
      cancel_delete unless action == :delete
      case action
      when :select
        @choosing = false
        select(arg)
      when :choose
        @choosing = arg
        rebuild
      when :add
        @choosing = false
        add_part(arg)
      when :patch
        step_patch(arg)
      when :channel
        step_channel(arg)
      when :op
        @op = @op % 6 + 1
        rebuild
      when :octave
        n = @keys.low + 12 * arg
        @keys.low = n if n >= LOW_MIN && n <= LOW_MAX
      when :delete
        delete_part
      end
    rescue => e
      UI.log("AMY panel: " + e.message)
    end

    def add_part(kind)
      return if @parts.size >= MAX_TABS
      engine
      ch = AMYPanel.free_channel(@parts)
      @part = kind == :fm ? AMY.add_fm(channel: ch) : AMY.add_synth(channel: ch)
      @op = 1
      rebuild
    end

    def step_patch(step)
      kind = @part.kind
      n = AMYPanel.next_patch(kind, @part.patch, step)
      if n.nil?
        @part.init_voice
      else
        @part.patch = n
      end
      sync_values
    end

    def step_channel(step)
      ch = AMYPanel.next_channel(@part.channel, step)
      @part.channel = ch
      @keys.channel = ch if @keys
      sync_labels
    end

    def delete_part
      unless @confirm_delete
        @confirm_delete = true
        w = @layout[:del]
        if w
          w.label = "Sure?"
          w.color = :red
        end
        return
      end
      @part.remove
      @part = nil
      rebuild
    end

    def cancel_delete
      return unless @confirm_delete
      @confirm_delete = false
      w = @layout[:del]
      if w
        w.label = "Del"
        w.color = :gray
      end
    end

    # After a preset change: the widgets show what the part has now
    def sync_values
      [:env_amp, :env_filter, :env_op].each do |id|
        w = @layout[id]
        w.sync if w
      end
      AMYPanel.knobs_for(@part.kind).each_with_index do |spec, i|
        w = @layout["k#{i}".to_sym]
        next unless w
        v = AMYPanel.read_param(@part, spec[0], @op)
        w.set_value(AMYPanel.knob_from_value(spec, v), notify: false)
      end
      sync_labels
    end

    def sync_labels
      w = @layout[:name]
      w.label = AMYPanel.patch_label(@part.kind, @part.patch) if w
      w = @layout[:ch]
      w.label = channel_label(@part.channel) if w
      i = @parts.index(@part)
      w = i.nil? ? nil : @layout["tab#{i}".to_sym]
      w.label = label_for(@part) if w
    end

    def label_for(part)
      AMYPanel.tab_label(part.kind, part.channel, AMYPanel.patch_label(part.kind, part.patch))
    end

    def channel_label(ch)
      ch.nil? ? "Ch -" : "Ch #{ch + 1}"
    end

    def tabs_row(l)
      l.grid(row: 0, cols: MAX_TABS + 1) do |g|
        if @choosing
          g.pad(col: 0..1, label: "+ FM", color: KIND_COLORS[:fm]) { request(:add, :fm) }
          g.pad(col: 2..3, label: "+ Synth", color: KIND_COLORS[:synth]) { request(:add, :synth) }
          g.pad(col: 4..5, label: "Cancel") { request(:choose, false) }
        else
          @parts.each_with_index do |part, i|
            break if i >= MAX_TABS
            color = part.equal?(@part) ? KIND_COLORS[part.kind] : :gray
            g.pad(col: i, id: "tab#{i}".to_sym, label: label_for(part), color: color) do
              request(:select, part)
            end
          end
          if @parts.size >= MAX_TABS
            g.pad(col: MAX_TABS, label: "Full", color: DIM)
          else
            g.pad(col: MAX_TABS, label: "+", color: :green) { request(:choose, true) }
          end
        end
      end
    end

    def header_row(l)
      part = @part
      l.grid(row: 1, cols: [2, 2, 8, 2, 2, 1, 2, 4, 2, 3, 3]) do |g|
        g.pad(col: 0, label: "<<") { request(:patch, -10) }
        g.pad(col: 1, label: "<") { request(:patch, -1) }
        g.pad(col: 2, id: :name, label: AMYPanel.patch_label(part.kind, part.patch),
              color: DIM)
        g.pad(col: 3, label: ">") { request(:patch, 1) }
        g.pad(col: 4, label: ">>") { request(:patch, 10) }
        g.pad(col: 6, label: "Ch-") { request(:channel, -1) }
        g.pad(col: 7, id: :ch, label: channel_label(part.channel), color: DIM)
        g.pad(col: 8, label: "Ch+") { request(:channel, 1) }
        g.pad(col: 10, id: :del, label: "Del") { request(:delete) }
      end
    end

    def body_row(l)
      part = @part
      if part.kind == :fm
        l.grid(row: 2, cols: [1, 5, 6]) do |g|
          g.pad(col: 0, label: "Op #{@op}", color: KIND_COLORS[:fm]) { request(:op) }
          g.envelope(col: 1, id: :env_op, label: "Op #{@op}", color: KIND_COLORS[:fm],
                     to: part.op(@op), max_time: 3000)
          knob_grid(g, 2, part)
        end
      else
        l.grid(row: 2, cols: [3, 3, 4]) do |g|
          g.envelope(col: 0, id: :env_amp, label: "Amp", color: :green,
                     to: part, env: :amp, max_time: 3000)
          g.envelope(col: 1, id: :env_filter, label: "Filter", color: :cyan,
                     to: part, env: :filter, max_time: 3000)
          knob_grid(g, 2, part)
        end
      end
    end

    def knob_grid(g, col, part)
      op = @op
      color = KIND_COLORS[part.kind]
      g.grid(col: col, cols: 4, rows: 2) do |k|
        AMYPanel.knobs_for(part.kind).each_with_index do |spec, i|
          int = spec[4] == :int
          value = AMYPanel.knob_from_value(spec, AMYPanel.read_param(part, spec[0], op))
          k.knob(col: i % 4, row: i / 4, id: "k#{i}".to_sym, label: spec[1], color: color,
                 value: value, min: int ? spec[2] : 0, max: int ? spec[3] : 127,
                 origin: spec[0] == :pan ? :center : :min) do |v|
            AMYPanel.write_param(part, spec[0], op, AMYPanel.value_from_knob(spec, v))
          end
        end
      end
    end

    def keys_row(l)
      l.grid(row: 3, cols: [1, 8, 1]) do |g|
        g.pad(col: 0, label: "Oct-") { request(:octave, -1) }
        g.keys(col: 1, keys: @keys)
        g.pad(col: 2, label: "Oct+") { request(:octave, 1) }
      end
    end
  end
end
