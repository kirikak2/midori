# UI Event Handler
#
# Provides Ruby API for handling UI events from M5Stack touch interface

# Global handler storage
$ui_handlers = {}

# Pad callback storage (index => block)
$ui_pad_callbacks = {}

module UI
  # MIDI Clock constant: 24 Pulses Per Quarter Note
  PPQ = 24

  # Pad color constants (RGB565)
  PAD_COLOR_RED     = 0xF800
  PAD_COLOR_GREEN   = 0x07E0
  PAD_COLOR_BLUE    = 0x001F
  PAD_COLOR_YELLOW  = 0xFFE0
  PAD_COLOR_CYAN    = 0x07FF
  PAD_COLOR_MAGENTA = 0xF81F
  PAD_COLOR_ORANGE  = 0xFD20
  PAD_COLOR_PURPLE  = 0x8010
  PAD_COLOR_WHITE   = 0xFFFF
  PAD_COLOR_GRAY    = 0x8410

  # Pad type constants
  PAD_TYPE_TRIGGER   = 0
  PAD_TYPE_MOMENTARY = 1
  PAD_TYPE_TOGGLE    = 2

  # Get current BPM set by UI
  # @return [Float] Current BPM value
  def self.bpm
    _bpm
  end

  # Set BPM displayed/maintained by UI. Used to seed the UI tempo with
  # a script-supplied initial value before letting the user adjust it.
  # @param value [Numeric]
  def self.set_bpm(value)
    _set_bpm(value)
  end

  # Output text to Screen Log
  # @param text [String] Text to output
  def self.log(text)
    _log(text.to_s)
  end

  # Screen indices (must match ui_common.h ui_screen_index_t order)
  SCREEN_MAIN      = 0
  SCREEN_PADS      = 1
  SCREEN_MIDI_INFO = 2
  SCREEN_LOGS      = 3
  SCREEN_SCRIPTS   = 4
  SCREEN_SETTINGS  = 5

  # Switch the active screen (0-based; see SCREEN_* constants)
  def self.set_screen(index)
    _set_screen(index)
  end

  # Currently active screen index
  def self.current_screen
    _current_screen
  end

  # Get number of pending UI events
  # @return [Integer] Number of events in queue
  def self.events_available
    _events_available
  end

  # Pop next UI event from queue
  # @return [Hash, nil] Event hash or nil if no events
  def self.pop_event
    _pop_event
  end

  # Process UI events using registered handlers
  # @return [Integer] Number of events processed
  def self.process
    count = 0
    event = pop_event
    while event
      dispatch_event(event)
      count = count + 1
      event = pop_event
    end
    count
  end

  # Register event handler
  def self.on(event_type, &block)
    if $ui_handlers[event_type]
      $ui_handlers[event_type] << block
    else
      $ui_handlers[event_type] = [block]
    end
  end

  # Clear all event handlers
  def self.clear_handlers
    $ui_handlers = {}
  end

  # Internal: dispatch event to handlers
  def self.dispatch_event(event)
    type = event[:type]

    # Handle pad events with callbacks
    if (type == :pad_press || type == :pad_release)
      dispatch_pad_event(event)
    end

    handlers = $ui_handlers[type]
    if handlers
      handlers.each do |h|
        begin
          h.call(event)
        rescue => e
          log("Error in handler for " + type.to_s + ": " + e.message)
        end
      end
    end
    any_handlers = $ui_handlers[:any]
    if any_handlers
      any_handlers.each do |h|
        begin
          h.call(event)
        rescue => e
          log("Error in :any handler: " + e.message)
        end
      end
    end
  end

  # Internal: dispatch pad event to registered callback
  def self.dispatch_pad_event(event)
    index = event[:index]
    callback = $ui_pad_callbacks[index]
    return unless callback

    pad_type = callback[:type]
    block = callback[:block]

    case pad_type
    when PAD_TYPE_TRIGGER
      # Trigger: Fire only on press
      if event[:type] == :pad_press
        block.call
      end
    when PAD_TYPE_MOMENTARY
      # Momentary: Pass pressed state to block
      block.call(event[:state])
    when PAD_TYPE_TOGGLE
      # Toggle: Only fire on press, pass current state
      if event[:type] == :pad_press
        block.call(event[:state])
      end
    end
  rescue => e
    log("Error in pad callback: " + e.message)
  end

  # Convert color symbol to RGB565 value
  def self.color_to_rgb565(color)
    case color
    when :red     then PAD_COLOR_RED
    when :green   then PAD_COLOR_GREEN
    when :blue    then PAD_COLOR_BLUE
    when :yellow  then PAD_COLOR_YELLOW
    when :cyan    then PAD_COLOR_CYAN
    when :magenta then PAD_COLOR_MAGENTA
    when :orange  then PAD_COLOR_ORANGE
    when :purple  then PAD_COLOR_PURPLE
    when :white   then PAD_COLOR_WHITE
    when :gray    then PAD_COLOR_GRAY
    else
      PAD_COLOR_GRAY  # Default color
    end
  end

  # Convert type symbol to type constant
  def self.type_to_constant(type)
    case type
    when :trigger   then PAD_TYPE_TRIGGER
    when :momentary then PAD_TYPE_MOMENTARY
    when :toggle    then PAD_TYPE_TOGGLE
    else
      PAD_TYPE_TRIGGER  # Default type
    end
  end

  # Configure a pad button
  # @param index [Integer] Pad index (1-12)
  # @param label [String] Button label
  # @param color [Symbol] Button color (:red, :green, :blue, etc.)
  # @param type [Symbol] Button type (:trigger, :momentary, :toggle)
  # @param block [Proc] Callback block
  def self.pad(index, label: "Pad", color: :gray, type: :trigger, &block)
    # Convert 1-based index to 0-based
    idx = index - 1
    return unless idx >= 0 && idx < 12

    # Convert symbols to constants
    color_code = color_to_rgb565(color)
    type_code = type_to_constant(type)

    # Set pad configuration
    _pad_set(idx, label, color_code, type_code)

    # Store callback if provided
    if block
      $ui_pad_callbacks[idx] = { type: type_code, block: block }
    end
  end

  # Get pad state
  # @param index [Integer] Pad index (1-12)
  # @return [Boolean] Pad state
  def self.pad_state(index)
    idx = index - 1
    return false unless idx >= 0 && idx < 12
    _pad_get_state(idx)
  end

  # Set pad label dynamically
  # @param index [Integer] Pad index (1-12)
  # @param label [String] New label
  def self.pad_label(index, label)
    idx = index - 1
    return unless idx >= 0 && idx < 12
    _pad_set_label(idx, label)
  end

  # Set pad color dynamically
  # @param index [Integer] Pad index (1-12)
  # @param color [Symbol] New color
  def self.pad_color(index, color)
    idx = index - 1
    return unless idx >= 0 && idx < 12
    color_code = color_to_rgb565(color)
    _pad_set_color(idx, color_code)
  end

  # Clear a pad configuration
  # @param index [Integer] Pad index (1-12)
  def self.pad_clear(index)
    idx = index - 1
    return unless idx >= 0 && idx < 12
    _pad_clear(idx)
    $ui_pad_callbacks.delete(idx)
  end

  # Clear all pad configurations
  def self.pad_clear_all
    _pad_clear_all
    $ui_pad_callbacks = {}
  end
end
