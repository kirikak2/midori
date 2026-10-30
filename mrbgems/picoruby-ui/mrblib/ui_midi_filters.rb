# Named MIDI filters on the MIDI Devices screen (docs/MIDI_FILTER_REGISTRY.md)
#
# picoruby-midi keeps the filters (MIDI::Filter, MIDI.register_filter) and
# runs the chains on the routes (MIDI.chain). This keeps the screen's model
# (C, ui_filters.cpp) in step with them:
#
#   - every registered filter is defined on the C side (name, label,
#     parameters), so the screen can list it
#   - every chain Ruby sets up is mirrored there; a chain the screen changed
#     (its revision moved) is applied here with MIDI.chain
#   - a filter on an active chain gets its operating panel (MIDI::Filter#
#     layout), which goes when the filter stops being used
#
# UI::MIDIDevices.boot installs this (every mode, at start).

$ui_filters_installed = false
$ui_filters_serial = -1
$ui_filters_applying = false
$ui_filter_revs = {}      # src * 0x1000 + dst => revision mirrored / applied
$ui_filter_panels = {}    # MIDI::Filter (object_id) => UI::Layout
$ui_layout_filter = nil   # The filter whose panel is being defined

module UI
  module MIDIFilters
    KIND_CODES = { int: 0, enum: 1, bool: 2, note: 3, channel: 4 }
    TITLE_MAX = 23   # UI_LAYOUT_TITLE_LEN - 1

    # What MIDI::Filter#layout gets: ui.layout(cols:, rows:) { ... } builds
    # the filter's panel, a layout titled after the filter and its patch.
    class PanelUI
      def initialize(filter, chain)
        @filter = filter
        @chain = chain
      end

      def filter
        @filter
      end

      def layout(cols: nil, rows: nil, gap: nil, padding: nil, &block)
        title = MIDIFilters._panel_title(@filter, @chain)
        $ui_layout_filter = @filter
        begin
          l = UI.layout(title, cols: cols, rows: rows, gap: gap, padding: padding, &block)
        ensure
          $ui_layout_filter = nil
        end
        $ui_filter_panels[@filter.object_id] = l
        l
      end
    end

    class << self
      # Hook into picoruby-midi and define what is registered already
      # (the /sd/plugins are read before boot installs this)
      def install
        return if $ui_filters_installed
        $ui_filters_installed = true
        $__midi_filter_registry_hooks__ << lambda { |event, name| MIDIFilters._registry(event, name) }
        $__midi_chain_hooks__ << lambda { |event, chain, filter, extra| MIDIFilters._chain(event, chain, filter, extra) }
        $__midi_device_resolver__ = lambda { |bit| MIDIDevices._device_for_bit(bit) }
        MIDI.filters.each_key { |name| _define(name) }
      end

      # From UI::MIDIDevices._poll: apply what the screen changed
      def _poll
        return unless $ui_filters_installed
        serial = UI._filter_serial
        return if serial == $ui_filters_serial
        $ui_filters_serial = serial
        seen = {}
        UI._filter_chains.each do |e|
          src, dst, rev, text = e
          key = src * 0x1000 + dst
          seen[key] = true
          next if $ui_filter_revs[key] == rev
          _apply(src, dst, parse(text))
        end
        # Chains the screen took off (with their patch, or from the Filters tab)
        MIDI.chains.values.each do |c|
          key = c.src * 0x1000 + c.dst
          next if seen[key] || !$ui_filter_revs.key?(key)
          _apply(c.src, c.dst, [])
        end
      end

      # "name(param:value,...),name2" -> [[name, {param => value}], ...]
      def parse(text)
        out = []
        return out if text.nil? || text.empty?
        name = ""
        params = {}
        pname = ""
        value = ""
        state = :name
        text.each_char do |ch|
          case state
          when :name
            if ch == ","
              out << [name.to_sym, params] unless name.empty?
              name = ""
              params = {}
            elsif ch == "("
              state = :pname
            else
              name << ch
            end
          when :pname
            if ch == ":"
              state = :value
            elsif ch == ")"
              state = :after
            else
              pname << ch
            end
          when :value
            if ch == "," || ch == ")"
              params[pname.to_sym] = value.to_i unless pname.empty?
              pname = ""
              value = ""
              state = ch == ")" ? :after : :pname
            else
              value << ch
            end
          when :after
            if ch == ","
              out << [name.to_sym, params] unless name.empty?
              name = ""
              params = {}
              state = :name
            end
          end
        end
        out << [name.to_sym, params] unless name.empty?
        out
      end

      # [[name, {param => value}], ...] -> text, values as the screen keeps
      # them (an enum's index, a bool's 0 / 1)
      def format(chain)
        parts = []
        chain.specs.each_with_index do |s, i|
          f = chain.filters[i]
          vals = []
          s[1].each do |k, v|
            spec = f ? f.class.param_spec(k) : nil
            code = spec ? f.class._code(spec, v) : (v.is_a?(Integer) ? v : nil)
            vals << "#{k}:#{code}" unless code.nil?
          end
          parts << (vals.empty? ? s[0].to_s : "#{s[0]}(#{vals.join(',')})")
        end
        parts.join(",")
      end

      # --- Hooks ---

      def _registry(event, name)
        if event == :register
          _define(name)
        else
          UI._filter_undefine(name.to_s)
        end
      end

      def _chain(event, chain, filter, extra)
        case event
        when :attach
          _open_panel(chain, filter)
        when :detach
          _close_panel(filter)
        when :chain
          _mirror(chain, false) unless $ui_filters_applying
        when :param
          _param_shown(filter, extra)
          _mirror(chain, true) unless $ui_filters_applying
        end
      end

      def _define(name)
        proto = MIDI.registered_filter(name)
        return unless proto
        lines = proto.class.param_specs.map do |s|
          choices = s[:choices] ? s[:choices].map { |c| c.to_s }.join(";") : ""
          [s[:name], s[:label], KIND_CODES[s[:kind]], s[:min], s[:max],
           proto.class._code(s, proto.params[s[:name]]), s[:step], choices].join("|")
        end
        UI._filter_define(name.to_s, proto.label.to_s, lines.join("\n"))
      end

      # Tell the model about a chain of Ruby's (a script's, or a value a panel
      # turned when panel is true)
      def _mirror(chain, panel)
        key = chain.src * 0x1000 + chain.dst
        live = MIDI.chains[[chain.src, chain.dst]]
        text = live ? format(live) : ""
        rev = UI._filter_chain_set(chain.src, chain.dst, text, panel)
        # (The serial is left alone: a change the screen made meanwhile must
        # still be seen. Our own chain is recognised by its revision.)
        $ui_filter_revs[key] = rev if rev != 0
      end

      # Apply a chain the screen set (specs empty: take it off)
      def _apply(src, dst, specs)
        key = src * 0x1000 + dst
        $ui_filters_applying = true
        begin
          MIDI.chain(src, dst, *(specs.empty? ? [[]] : specs))
        rescue => e
          puts "[Filter] #{e.message}"
        ensure
          $ui_filters_applying = false
        end
        # Back with the values as they are now (kept in range)
        c = MIDI.chains[[src, dst]]
        if c
          _mirror(c, false)
        else
          $ui_filter_revs.delete(key)
        end
      end

      # --- Panels ---

      def _open_panel(chain, filter)
        return if $ui_filter_panels[filter.object_id]
        filter._guard { filter.layout(PanelUI.new(filter, chain)) }
      rescue => e
        puts "[Filter] #{filter.name}: no panel (#{e.message})"
      end

      def _close_panel(filter)
        l = $ui_filter_panels.delete(filter.object_id)
        return unless l
        # Out of it first when it is on screen: back where filters are picked
        r = UI._requested_screen
        if l.layout_slot && r[0] == UI::SCREEN_LAYOUT && r[1] == l.layout_slot
          UI.show(:midi_devices)
        end
        l.delete
      end

      # A value changed elsewhere (the screen, a script): the panel shows it
      def _param_shown(filter, name)
        l = $ui_filter_panels[filter.object_id]
        return unless l
        w = l["param_#{name}".to_sym]
        return unless w
        spec = filter.class.param_spec(name)
        value = filter.params[name]
        if w.is_a?(UI::Layout::Knob)
          w.set_value(filter.class._code(spec, value), notify: false)
        else
          w.label = _param_label(spec, value)
        end
      end

      def _param_label(spec, value)
        case spec[:kind]
        when :bool then "#{spec[:label]} #{value ? 'on' : 'off'}"
        when :channel then "#{spec[:label]} #{value.nil? ? '-' : value + 1}"
        else "#{spec[:label]} #{value}"
        end
      end

      def _panel_title(filter, chain)
        from = MIDIDevices._name_of_bit(chain.src)
        to = MIDIDevices._name_of_bit(chain.dst)
        base = "#{filter.label} #{from}>#{to}"
        base = base[0, TITLE_MAX] if base.size > TITLE_MAX
        title = base
        n = 2
        # The title is a layout's key: another panel (the same filter on a
        # patch with the same names) or a script's layout may have it already
        while $ui_layouts[title]
          suffix = " #{n}"
          title = base[0, TITLE_MAX - suffix.size] + suffix
          n += 1
        end
        title
      end
    end
  end

  class Layout
    class Builder
      # A knob for a declared parameter of the filter whose panel this is
      # (:int / :note / :channel): its range, value and label come from the
      # declaration; turning it sets the parameter.
      def param_knob(name, col: nil, row: nil, area: nil, label: nil, color: :gray,
                     sensitivity: 1.0)
        f, spec = _param_of(name, "param_knob")
        value = f.class._code(spec, f.params[spec[:name]])
        pname = spec[:name]
        knob(col: col, row: row, area: area, id: "param_#{pname}".to_sym,
             label: label || spec[:label], color: color, value: value,
             min: spec[:min], max: spec[:max], step: spec[:step], sensitivity: sensitivity) do |v|
          f.set_param(pname, v.to_i)
        end
      end

      # A pad for a declared parameter: a :bool toggles, an :enum steps to
      # the next choice (its label shows the value)
      def param_pad(name, col: nil, row: nil, area: nil, color: :gray)
        f, spec = _param_of(name, "param_pad")
        pname = spec[:name]
        label = MIDIFilters._param_label(spec, f.params[pname])
        id = "param_#{pname}".to_sym
        case spec[:kind]
        when :bool
          pad(col: col, row: row, area: area, id: id, label: label, color: color, type: :trigger) do
            f.set_param(pname, !f.params[pname])
          end
        when :enum
          pad(col: col, row: row, area: area, id: id, label: label, color: color, type: :trigger) do
            i = spec[:choices].index(f.params[pname]) || 0
            f.set_param(pname, spec[:choices][(i + 1) % spec[:choices].size])
          end
        else
          raise ArgumentError, "param_pad: #{pname} is not a :bool or :enum parameter"
        end
      end

      private

      def _param_of(name, who)
        f = $ui_layout_filter
        raise ArgumentError, "#{who}: only in a MIDI::Filter's layout" unless f
        spec = f.class.param_spec(name.to_sym)
        raise ArgumentError, "#{who}: #{f.class} has no parameter #{name.inspect}" unless spec
        [f, spec]
      end
    end
  end
end
