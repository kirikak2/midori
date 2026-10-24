# Layout DSL (docs/UI_LAYOUT_DSL.md)
#
# Places pads, knobs, the XYPad, the Tombola and the Keys on one screen by grid column /
# row. Everything grid-related -- tracks, areas, nesting, validation -- is
# resolved here, once, when the layout is defined; the C++ side only ever
# receives a list of pixel rectangles.
#
#   UI.layout("Perform", cols: 4, rows: 3) do
#     areas <<~GRID
#       xy xy xy k1
#       xy xy xy k2
#       p  p  p  p
#     GRID
#     xypad area: :xy, scale: [48, 50, 52, 55], device: dev
#     knob area: :k1, label: "Cutoff" do |v| dev.control_change(74, v.to_i) end
#     grid area: :p, cols: 4 do
#       4.times { |i| pad col: i, label: "P#{i}" do dev.trigger(36 + i) end }
#     end
#   end.show

$ui_layouts = {}            # title => UI::Layout
$ui_widget_callbacks = {}   # layout id * UI::Layout::KEY_STRIDE + widget id => handle
$ui_layout_next_id = 0

module UI
  # A fixed-size grid track, as opposed to a weight: cols: [UI.px(60), 1, 1]
  class Px
    attr_reader :pixels

    def initialize(pixels)
      @pixels = pixels.to_i
    end
  end

  def self.px(pixels)
    Px.new(pixels)
  end

  # Screen facts a layout is sized against.
  # @return [Hash] { width:, height:, content: [x, y, w, h], large:, gap: }
  def self.display
    d = _display
    { width: d[0], height: d[1], content: [d[2], d[3], d[4], d[5]],
      large: d[6], gap: d[7] }
  end

  # Grid math only: a rectangle split into column / row tracks. 0-based cells,
  # inclusive Range spans. Pure Ruby with no device calls, so it runs (and is
  # tested) on a host interpreter.
  class Grid
    attr_reader :cols, :rows, :x, :y, :w, :h

    # @param rect [Array] [x, y, w, h] in pixels
    # @param cols [Integer, Array] count, or one entry per track (weight or UI.px)
    # @param rows [Integer, Array]
    def initialize(rect, cols: 1, rows: 1, gap: 0, padding: 0)
      @x, @y, @w, @h = rect
      @gap = gap
      @padding = padding
      @col_starts, @col_sizes = Grid.tracks(@x + padding, @w - padding * 2, cols, gap)
      @row_starts, @row_sizes = Grid.tracks(@y + padding, @h - padding * 2, rows, gap)
      @cols = @col_sizes.size
      @rows = @row_sizes.size
    end

    # Splits `length` pixels starting at `origin` into tracks.
    # @return [Array] [starts, sizes]
    def self.tracks(origin, length, spec, gap)
      spec = Array.new(spec, 1) if spec.is_a?(Integer)
      unless spec.is_a?(Array) && spec.size > 0
        raise ArgumentError, "tracks must be a positive Integer or a non-empty Array"
      end

      fixed = 0
      weight = 0
      spec.each do |t|
        if t.is_a?(Px)
          fixed += t.pixels
        elsif t.is_a?(Numeric) && t > 0
          weight += t
        else
          raise ArgumentError, "bad track: #{t.inspect} (use a positive number or UI.px(n))"
        end
      end

      free = length - gap * (spec.size - 1) - fixed
      free = 0 if free < 0

      # Whole pixels per weighted track, then the remainder one pixel at a
      # time from the first one, so the tracks fill the length exactly.
      sizes = spec.map do |t|
        t.is_a?(Px) ? t.pixels : (weight > 0 ? (free * t / weight).to_i : 0)
      end
      used = 0
      sizes.each_with_index { |sz, i| used += sz unless spec[i].is_a?(Px) }
      left = (weight > 0) ? free - used : 0
      i = 0
      while left > 0 && i < spec.size
        unless spec[i].is_a?(Px)
          sizes[i] += 1
          left -= 1
        end
        i += 1
        i = 0 if i == spec.size && left > 0
      end

      starts = []
      pos = origin
      sizes.each do |sz|
        starts << pos
        pos += sz + gap
      end
      [starts, sizes]
    end

    # Normalises a col:/row: value to [first, last] (inclusive, 0-based).
    def self.span(value, count, axis)
      if value.is_a?(Integer)
        first = last = value
      elsif value.is_a?(Range)
        first = value.first
        last = value.last
        unless first.is_a?(Integer) && last.is_a?(Integer)
          raise ArgumentError, "#{axis}: needs Integer bounds, got #{value.inspect}"
        end
        last -= 1 if value.exclude_end?
        if last < first
          raise ArgumentError, "#{axis}: #{value.inspect} is empty or descending"
        end
      else
        raise ArgumentError, "#{axis}: must be an Integer or a Range, got #{value.inspect}"
      end
      if first < 0 || last >= count
        raise ArgumentError, "#{axis}: #{value.inspect} is outside 0..#{count - 1}"
      end
      [first, last]
    end

    # Pixel rectangle covering the given cells.
    # @return [Array] [x, y, w, h]
    def rect(col_span, row_span)
      c0, c1 = col_span
      r0, r1 = row_span
      x = @col_starts[c0]
      y = @row_starts[r0]
      [x, y, @col_starts[c1] + @col_sizes[c1] - x, @row_starts[r1] + @row_sizes[r1] - y]
    end

    # Parses an areas template into { name => [[c0, c1], [r0, r1]] }.
    # Each name must cover a filled rectangle; "." is an empty cell.
    def self.parse_areas(text)
      lines = []
      text.to_s.split("\n").each do |line|
        cells = line.split(" ")
        lines << cells unless cells.empty?
      end
      raise ArgumentError, "areas: empty template" if lines.empty?
      width = lines[0].size
      lines.each_with_index do |cells, r|
        if cells.size != width
          raise ArgumentError, "areas: row #{r} has #{cells.size} cells, row 0 has #{width}"
        end
      end

      boxes = {}
      lines.each_with_index do |cells, r|
        cells.each_with_index do |name, c|
          next if name == "."
          key = name.to_sym
          b = boxes[key]
          if b
            b[0] = c if c < b[0]
            b[1] = c if c > b[1]
            b[2] = r if r < b[2]
            b[3] = r if r > b[3]
            b[4] += 1
          else
            boxes[key] = [c, c, r, r, 1]
          end
        end
      end

      result = {}
      boxes.each do |key, b|
        if (b[1] - b[0] + 1) * (b[3] - b[2] + 1) != b[4]
          raise ArgumentError, "areas: #{key} is not a rectangle"
        end
        result[key] = [[b[0], b[1]], [b[2], b[3]]]
      end
      [result, width, lines.size]
    end
  end

  class Layout
    # Layouts are known to the C side by an id (there is no fixed number of
    # them): handed out in definition order, so the Home tiles keep that order
    MAX_ID = 0x7FFF
    MAX_WIDGETS = 32
    KEY_STRIDE = 64

    # Kinds as the C side numbers them (ui_widget_kind_t)
    KIND_PAD     = 0
    KIND_KNOB    = 1
    KIND_XYPAD   = 2
    KIND_TOMBOLA = 3
    KIND_KEYS    = 4
    KIND_TEMPO   = 5

    # Below this a widget is hard to hit with a finger. Provisional numbers,
    # to be tuned on the hardware (docs/UI_LAYOUT_DSL.md D13): placing one
    # smaller only logs a warning.
    MIN_SIZE = {
      pad:     [40, 32],
      knob:    [56, 56],
      xypad:   [96, 64],
      tombola: [96, 96],
      keys:    [120, 48],
      tempo:   [120, 72]
    }

    # Only one of these per layout: the C side holds a single model of each
    SINGLETONS = [:xypad, :tombola, :keys, :tempo]

    # One grid level of a definition: its cells, its named areas, and which
    # cells are taken (overlap is an error).
    class Scope
      attr_reader :grid

      def initialize(rect, cols, rows, gap, padding)
        @rect = rect
        @cols_spec = cols
        @rows_spec = rows
        @gap = gap
        @padding = padding
        @areas = nil
        @grid = nil
        @taken = {}
      end

      def areas=(text)
        raise ArgumentError, "areas must come before anything is placed" if @grid
        boxes, width, height = Grid.parse_areas(text)
        if @cols_spec && track_count(@cols_spec) != width
          raise ArgumentError, "areas: #{width} columns but cols: gives #{track_count(@cols_spec)}"
        end
        if @rows_spec && track_count(@rows_spec) != height
          raise ArgumentError, "areas: #{height} rows but rows: gives #{track_count(@rows_spec)}"
        end
        @cols_spec ||= width
        @rows_spec ||= height
        @areas = boxes
      end

      def grid
        @grid ||= Grid.new(@rect, cols: @cols_spec || 1, rows: @rows_spec || 1,
                           gap: @gap, padding: @padding)
      end

      # Resolves col:/row:/area: to a pixel rectangle and claims the cells.
      def place(what, col, row, area)
        g = grid
        if area
          if col || row
            raise ArgumentError, "#{what}: give area: or col:/row:, not both"
          end
          box = @areas && @areas[area.to_sym]
          raise ArgumentError, "#{what}: no area named #{area.inspect}" unless box
          cs, rs = box
        else
          cs = Grid.span(col || 0, g.cols, "#{what} col")
          rs = Grid.span(row || 0, g.rows, "#{what} row")
        end

        r = rs[0]
        while r <= rs[1]
          c = cs[0]
          while c <= cs[1]
            key = r * 1000 + c
            if @taken[key]
              raise ArgumentError, "#{what} at col #{c}, row #{r} overlaps #{@taken[key]}"
            end
            @taken[key] = what
            c += 1
          end
          r += 1
        end
        g.rect(cs, rs)
      end

      def gap
        @gap
      end

      private

      def track_count(spec)
        spec.is_a?(Integer) ? spec : spec.size
      end
    end

    # What the definition block runs against. Collects widget specs; nothing
    # reaches the C side until the whole block has run without error.
    class Builder
      attr_reader :specs, :warnings

      def initialize(scope)
        @scope = scope
        @specs = []
        @warnings = []
        @kinds = {}
      end

      def areas(text)
        @scope.areas = text
        nil
      end

      def pad(col: nil, row: nil, area: nil, id: nil, label: "Pad", color: :gray,
              type: :trigger, &block)
        # Explicit Hashes: mruby does not fold trailing keywords into a
        # positional parameter the way CRuby does.
        add(:pad, col, row, area, id, block,
            { label: label.to_s, color: color, type: type })
      end

      def knob(col: nil, row: nil, area: nil, id: nil, label: nil, color: :gray,
               value: nil, min: 0, max: 127, step: 1, origin: :min, sensitivity: 1.0,
               &block)
        add(:knob, col, row, area, id, block,
            { label: label, color: color, value: value, min: min, max: max,
              step: step, origin: origin, sensitivity: sensitivity })
      end

      # Keywords are UI::XYPad.new's. pad: hands over an existing instance
      # instead (no reset of its slots).
      def xypad(col: nil, row: nil, area: nil, id: nil, pad: nil, **opts)
        add(:xypad, col, row, area, id, nil, { instance: pad, opts: opts })
      end

      # Keywords are UI::Tombola.new's; tombola: hands over an existing one.
      def tombola(col: nil, row: nil, area: nil, id: nil, tombola: nil, **opts)
        add(:tombola, col, row, area, id, nil, { instance: tombola, opts: opts })
      end

      # Keywords are UI::Keys.new's; keys: hands over an existing one.
      def keys(col: nil, row: nil, area: nil, id: nil, keys: nil, **opts)
        add(:keys, col, row, area, id, nil, { instance: keys, opts: opts })
      end

      # The Tempo screen in the cell: BPM (tap for TAP tempo), external clock
      # source, Sync. Returns UI::Tempo.
      def tempo(col: nil, row: nil, area: nil, id: nil)
        add(:tempo, col, row, area, id, nil, {})
      end

      # Splits a cell (or area) into a grid of its own. col:/row: inside the
      # block are that grid's own, 0-based.
      def grid(col: nil, row: nil, area: nil, cols: nil, rows: nil, gap: nil,
               padding: 0, &block)
        rect = @scope.place("grid", col, row, area)
        child = Scope.new(rect, cols, rows, gap || @scope.gap, padding)
        saved = @scope
        @scope = child
        begin
          Layout.run_block(self, block) if block
        ensure
          @scope = saved
        end
        nil
      end

      private

      def add(kind, col, row, area, id, block, params)
        if SINGLETONS.include?(kind)
          raise ArgumentError, "only one #{kind} per layout" if @kinds[kind]
          @kinds[kind] = true
        end
        if @specs.size >= MAX_WIDGETS
          raise ArgumentError, "too many widgets (max #{MAX_WIDGETS})"
        end
        rect = @scope.place(kind.to_s, col, row, area)
        min = MIN_SIZE[kind]
        if min && (rect[2] < min[0] || rect[3] < min[1])
          @warnings << "#{kind} #{id || @specs.size} is #{rect[2]}x#{rect[3]}px, " \
                       "under the #{min[0]}x#{min[1]} minimum"
        end
        spec = { kind: kind, rect: rect, id: id, block: block, params: params }
        @specs << spec
        spec
      end
    end

    # Runs a definition block: with no block parameter it is evaluated against
    # the builder (`pad col: 0`), with one the builder is passed in
    # (`|l| l.pad col: 0`) and self stays the script's.
    def self.run_block(builder, block)
      if block.arity == 1
        block.call(builder)
      else
        builder.instance_eval(&block)
      end
    end

    # Resolves a definition without touching the device: the widget specs
    # (rectangles in screen pixels) and any size warnings.
    def self.resolve(content, cols: nil, rows: nil, gap: 0, padding: nil, &block)
      scope = Scope.new(content, cols, rows, gap, padding.nil? ? gap : padding)
      builder = Builder.new(scope)
      run_block(builder, block) if block
      [builder.specs, builder.warnings]
    end

    # --- Live layouts ----------------------------------------------------

    # UI.layout. Defining a title again replaces that layout, widgets and
    # state alike (D6 / D11): there is no adding to or moving within a live
    # one.
    def self.define(title, cols: nil, rows: nil, gap: nil, padding: nil, &block)
      title = title.to_s
      d = UI.display
      gap = d[:gap] if gap.nil?

      # Resolve first: a mistake in the block leaves the old layout alone
      specs, warnings = resolve(d[:content], cols: cols, rows: rows, gap: gap,
                                padding: padding, &block)

      layout = $ui_layouts[title]
      unless layout
        layout = Layout.new(title, next_id)
        $ui_layouts[title] = layout
      end
      warnings.each { |w| UI.log("layout #{title}: #{w}") }
      layout.commit(specs)
      layout
    end

    # A fresh id: one more than the last, so a deleted layout's id (and any
    # event still on its way for it) does not land on a new layout soon
    def self.next_id
      used = $ui_layouts.values.map { |l| l.slot }
      raise ArgumentError, "too many layouts" if used.size > MAX_ID
      id = $ui_layout_next_id
      id = (id + 1) % (MAX_ID + 1) while used.include?(id)
      $ui_layout_next_id = (id + 1) % (MAX_ID + 1)
      id
    end

    attr_reader :title, :slot

    def initialize(title, slot)
      @title = title
      @slot = slot
      @live = false
      @widgets = []
      @ids = {}
    end

    # Hands the resolved widgets to the device, replacing what this slot
    # showed. Without a display (Freenove) nothing goes over and the layout
    # just is not showable.
    def commit(specs)
      clear_callbacks
      @widgets = []
      @ids = {}
      live = UI._layout_begin(@slot, @title)
      specs.each do |spec|
        handle = build(spec, live)
        @widgets << handle
        @ids[spec[:id]] = handle if spec[:id]
      end
      @live = live ? UI._layout_commit : false
      self
    end

    # The slot to show, or nil when this layout is not on the device
    def layout_slot
      @live ? @slot : nil
    end

    def show
      UI.show(self)
    end

    # The widget defined with id:
    def [](id)
      @ids[id]
    end

    def widgets
      @widgets
    end

    # Calls every knob's block with its current value. Defining knobs sends
    # nothing; use this to push initial values, or to re-sync a synth that
    # was plugged in later.
    def send_all
      @widgets.each { |w| w.send_value if w.is_a?(Knob) }
      nil
    end

    # Removes the layout: its Home tile goes, and the slot is free again.
    def delete
      UI._layout_delete(@slot) if @live
      clear_callbacks
      @live = false
      $ui_layouts.delete(@title)
      nil
    end

    private

    def clear_callbacks
      base = @slot * KEY_STRIDE
      i = 0
      while i < KEY_STRIDE
        $ui_widget_callbacks.delete(base + i)
        i += 1
      end
    end

    def color_code(color)
      color.is_a?(Integer) ? color : UI.color_to_rgb565(color)
    end

    def build(spec, live)
      x, y, w, h = spec[:rect]
      pr = spec[:params]
      case spec[:kind]
      when :pad
        type = UI.type_to_constant(pr[:type])
        id = live ? UI._layout_add_pad(x, y, w, h, pr[:label], color_code(pr[:color]), type) : -1
        register(Pad.new(self, id, type, spec[:block]))
      when :knob
        origin = (pr[:origin] == :center) ? UI::KNOB_ORIGIN_CENTER : UI::KNOB_ORIGIN_MIN
        min = pr[:min].to_f
        max = pr[:max].to_f
        value = pr[:value]
        if value.nil?
          value = (origin == UI::KNOB_ORIGIN_CENTER) ? (min + max) / 2.0 : min
        end
        label = pr[:label] || "Knob"
        id = -1
        if live
          id = UI._layout_add_knob(x, y, w, h, label.to_s, color_code(pr[:color]),
                                   min, max, pr[:step].to_f, value.to_f, origin,
                                   pr[:sensitivity].to_f, spec[:block] ? true : false)
        end
        register(Knob.new(self, id, spec[:block]))
      when :xypad
        pad = pr[:instance] || UI::XYPad.new(**pr[:opts])
        UI._layout_add_view(KIND_XYPAD, x, y, w, h) if live
        pad
      when :tombola
        t = pr[:instance] || UI::Tombola.new(**pr[:opts])
        UI._layout_add_view(KIND_TOMBOLA, x, y, w, h) if live
        t
      when :keys
        k = pr[:instance] || UI::Keys.new(**pr[:opts])
        UI._layout_add_view(KIND_KEYS, x, y, w, h) if live
        k
      when :tempo
        UI._layout_add_view(KIND_TEMPO, x, y, w, h) if live
        UI::Tempo
      end
    end

    def register(handle)
      if handle.id >= 0
        $ui_widget_callbacks[@slot * KEY_STRIDE + handle.id] = handle
      end
      handle
    end

    public

    # A pad on a layout. Its block follows the same rules as UI.pad's:
    # :trigger runs on press, :momentary gets the state on press and release,
    # :toggle gets the new state on press.
    class Pad
      attr_reader :id

      def initialize(layout, id, type, block)
        @layout = layout
        @id = id
        @type = type
        @block = block
      end

      def state
        return false if @id < 0
        UI._lw_pad_state(@layout.slot, @id)
      end

      def label=(text)
        UI._lw_set_label(@layout.slot, @id, text.to_s) if @id >= 0
      end

      def color=(color)
        return if @id < 0
        code = color.is_a?(Integer) ? color : UI.color_to_rgb565(color)
        UI._lw_set_color(@layout.slot, @id, code)
      end

      # Internal: a press / release from the UI event queue
      def dispatch(event)
        return unless @block
        case @type
        when UI::PAD_TYPE_TRIGGER
          @block.call if event[:phase] == :press
        when UI::PAD_TYPE_MOMENTARY
          @block.call(event[:state])
        when UI::PAD_TYPE_TOGGLE
          @block.call(event[:state]) if event[:phase] == :press
        end
      rescue => e
        UI.log("Error in layout pad callback: " + e.message)
      end
    end

    # A knob on a layout. The block gets the new value whenever it moves.
    class Knob
      attr_reader :id

      def initialize(layout, id, block)
        @layout = layout
        @id = id
        @block = block
      end

      def value
        return 0.0 if @id < 0
        UI._lw_knob_value(@layout.slot, @id)
      end

      # Moves the knob and, if the value changed, calls the block (as
      # UI.knob_set does). set_value(v, notify: false) updates the display only.
      def value=(v)
        set_value(v)
      end

      def set_value(v, notify: true)
        return false if @id < 0
        changed = UI._lw_knob_set_value(@layout.slot, @id, v.to_f)
        send_value if changed && notify
        changed
      end

      # Back to the value it was defined with (block runs if that moved it)
      def reset
        return false if @id < 0
        changed = UI._lw_knob_reset(@layout.slot, @id)
        send_value if changed
        changed
      end

      def send_value
        return unless @block
        @block.call(value)
      rescue => e
        UI.log("Error in layout knob callback: " + e.message)
      end

      def label=(text)
        UI._lw_set_label(@layout.slot, @id, text.to_s) if @id >= 0
      end

      def color=(color)
        return if @id < 0
        code = color.is_a?(Integer) ? color : UI.color_to_rgb565(color)
        UI._lw_set_color(@layout.slot, @id, code)
      end

      # Internal: a change from the UI event queue
      def dispatch(event)
        @block.call(event[:value]) if @block
      rescue => e
        UI.log("Error in layout knob callback: " + e.message)
      end
    end
  end

  # Define (or redefine) a layout. See UI::Layout and docs/UI_LAYOUT_DSL.md.
  #
  # @param title [String] status bar title and Home tile; also the layout's key
  # @param cols [Integer, Array] columns (count, or weights / UI.px per track)
  # @param rows [Integer, Array]
  # @param gap [Integer] pixels between cells (board default when nil)
  # @param padding [Integer] pixels around the grid (gap when nil)
  # @return [UI::Layout]
  def self.layout(title, cols: nil, rows: nil, gap: nil, padding: nil, &block)
    Layout.define(title, cols: cols, rows: rows, gap: gap, padding: padding, &block)
  end

  # Internal: route a layout widget event to its handle
  def self.dispatch_widget_event(event)
    handle = $ui_widget_callbacks[event[:layout] * Layout::KEY_STRIDE + event[:id]]
    handle.dispatch(event) if handle
  end
end
