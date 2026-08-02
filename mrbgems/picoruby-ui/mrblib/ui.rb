# UI Event Handler
#
# Provides Ruby API for handling UI events from M5Stack touch interface

# Global handler storage
$ui_handlers = {}

# Pad callback storage (index => block)
$ui_pad_callbacks = {}

# Tombola hit callback (single sequencer instance)
$ui_tombola_handler = nil

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
  SCREEN_TOMBOLA   = 6

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

    # Handle tombola hits registered through Tombola#on_hit
    if type == :tombola_hit
      dispatch_tombola_event(event)
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

  # Internal: dispatch tombola hit to the registered callback
  def self.dispatch_tombola_event(event)
    handler = $ui_tombola_handler
    return unless handler
    handler.call(event)
  rescue => e
    log("Error in tombola callback: " + e.message)
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

  # Tombola - physics driven sequencer
  #
  # Balls bounce inside a rotating polygon and every wall collision fires a
  # note; which note comes from the scale, indexed by the side that was hit.
  # The simulation runs on the C++ side (main/ui/screen_tombola.cpp) and sounds
  # each hit the instant it happens, so note timing does not depend on how
  # often the script polls. Registering #on_hit additionally queues the hit for
  # Ruby; set #sound = false if Ruby should be the only one making noise.
  #
  # That C++ state is a single global, so one Tombola exists at a time:
  # Tombola.new resets it and applies the options given.
  #
  # @example
  #   t = UI::Tombola.new(sides: 6, gravity: 0.5, rotation: 12)
  #   t.device = MIDI::Device.new(MIDIDevices.sam2695)
  #   t.scale = [36, 38, 42, 45, 48, 50]
  #   t.add_ball(color: :red)
  #   t.start
  #   t.show
  class Tombola
    # Mirrors UI_TOMBOLA_MAX_BALLS in main/ui/ui_common.h
    MAX_BALLS = 16

    # Mirrors DEFAULT_SCALE in main/ui/screen_tombola.cpp, so #scale reports
    # what the C++ side is actually playing before anything is assigned.
    DEFAULT_SCALE = [36, 38, 42, 45, 46, 49]

    def initialize(sides: nil, rotation: nil, radius: nil,
                   gravity: nil, gravity_mode: nil, bounce: nil,
                   friction: nil, spin_transfer: nil, ball_size: nil,
                   balls: nil, scale: nil, channel: nil, duration: nil,
                   velocity_range: nil, retrigger_ms: nil, max_voices: nil,
                   sound: nil, touch_add: nil, device: nil)
      UI._tombola_reset
      $ui_tombola_handler = nil
      @scale = DEFAULT_SCALE
      @device = nil

      self.sides = sides                   unless sides.nil?
      self.rotation = rotation             unless rotation.nil?
      self.radius = radius                 unless radius.nil?
      self.gravity = gravity               unless gravity.nil?
      self.gravity_mode = gravity_mode     unless gravity_mode.nil?
      self.bounce = bounce                 unless bounce.nil?
      self.friction = friction             unless friction.nil?
      self.spin_transfer = spin_transfer   unless spin_transfer.nil?
      self.ball_size = ball_size           unless ball_size.nil?
      self.scale = scale                   unless scale.nil?
      self.channel = channel               unless channel.nil?
      self.duration = duration             unless duration.nil?
      self.velocity_range = velocity_range unless velocity_range.nil?
      self.retrigger_ms = retrigger_ms     unless retrigger_ms.nil?
      self.max_voices = max_voices         unless max_voices.nil?
      self.sound = sound                   unless sound.nil?
      self.touch_add = touch_add           unless touch_add.nil?
      self.device = device                 unless device.nil?
      self.balls = balls                   unless balls.nil?
    end

    # --- Geometry ---

    # Number of polygon sides (3-16). Also the length of the note cycle:
    # side N plays scale[N % scale.size].
    def sides
      UI._tombola_get_i(:sides)
    end

    def sides=(value)
      UI._tombola_set_i(:sides, value)
    end

    # Rotation speed in RPM. Negative spins the other way.
    def rotation
      UI._tombola_get_f(:rotation)
    end

    def rotation=(value)
      UI._tombola_set_f(:rotation, value)
    end

    # Polygon size as a fraction (0.2-1.0) of the usable screen area
    def radius
      UI._tombola_get_f(:radius)
    end

    def radius=(value)
      UI._tombola_set_f(:radius, value)
    end

    # --- Physics ---

    # Gravity strength. 0.0 with gravity_mode :none gives free-floating balls.
    def gravity
      UI._tombola_get_f(:gravity)
    end

    def gravity=(value)
      UI._tombola_set_f(:gravity, value)
    end

    # :down (constant pull), :center (toward the middle) or :none
    def gravity_mode
      case UI._tombola_get_i(:gravity_mode)
      when 1 then :center
      when 2 then :none
      else        :down
      end
    end

    def gravity_mode=(mode)
      value = case mode
              when :center, 1 then 1
              when :none, 2   then 2
              else                 0
              end
      UI._tombola_set_i(:gravity_mode, value)
    end

    # Restitution, 0.0-1.2. Above 1.0 the balls gain energy on every bounce.
    def bounce
      UI._tombola_get_f(:bounce)
    end

    def bounce=(value)
      UI._tombola_set_f(:bounce, value)
    end

    # Tangential damping on impact, 0.0-1.0 (1.0 = frictionless)
    def friction
      UI._tombola_get_f(:friction)
    end

    def friction=(value)
      UI._tombola_set_f(:friction, value)
    end

    # How much of the spinning wall's motion is handed to the ball, 0.0-1.0.
    # At 0.0 a patch with gravity tends to settle at the bottom and go quiet.
    def spin_transfer
      UI._tombola_get_f(:spin_transfer)
    end

    def spin_transfer=(value)
      UI._tombola_set_f(:spin_transfer, value)
    end

    # Ball radius relative to the polygon (0.005-0.4)
    def ball_size
      UI._tombola_get_f(:ball_size)
    end

    def ball_size=(value)
      UI._tombola_set_f(:ball_size, value)
    end

    # --- Sound ---

    # Note numbers indexed by the side that was hit
    def scale
      @scale
    end

    def scale=(notes)
      return if notes.nil? || notes.size < 1
      @scale = notes
      UI._tombola_set_scale(notes)
    end

    # Default MIDI channel for balls that do not carry their own
    def channel
      UI._tombola_get_i(:channel)
    end

    def channel=(value)
      UI._tombola_set_i(:channel, value)
    end

    # Note length in ms
    def duration
      UI._tombola_get_i(:duration)
    end

    def duration=(value)
      UI._tombola_set_i(:duration, value)
    end

    # Impact speed is mapped into this velocity range
    # @return [Array<Integer>] [min, max]
    def velocity_range
      [UI._tombola_get_i(:velocity_min), UI._tombola_get_i(:velocity_max)]
    end

    # @param range [Range, Array] e.g. 40..127 or [40, 127]
    def velocity_range=(range)
      if range.is_a?(Array)
        low = range[0]
        high = range[1]
      else
        low = range.first
        high = range.last
      end
      UI._tombola_set_i(:velocity_min, low)
      UI._tombola_set_i(:velocity_max, high)
    end

    # Minimum gap between two notes from the same ball. A ball grinding along a
    # wall would otherwise machine-gun.
    def retrigger_ms
      UI._tombola_get_i(:retrigger_ms)
    end

    def retrigger_ms=(value)
      UI._tombola_set_i(:retrigger_ms, value)
    end

    # Maximum notes emitted in a single physics step
    def max_voices
      UI._tombola_get_i(:max_voices)
    end

    def max_voices=(value)
      UI._tombola_set_i(:max_voices, value)
    end

    # Let the C++ side sound the hits (default true)
    def sound?
      UI._tombola_get_i(:sound) != 0
    end

    def sound=(value)
      UI._tombola_set_i(:sound, value ? 1 : 0)
    end

    # Where the notes go. Accepts a MIDI::Device or a raw transport.
    def device
      @device
    end

    def device=(dev)
      @device = dev
      UI._tombola_set_i(:transport, Tombola.transport_mask_for(dev))
    end

    # Mirrors MIDI::Device#_get_transport_mask, which is private there.
    def self.transport_mask_for(device)
      transport = device.respond_to?(:transport) ? device.transport : device
      if transport.respond_to?(:transport_id)
        transport.transport_id
      else
        case transport.class.to_s
        when "USB_MIDI"        then 0x01
        when "SAM2695"         then 0x02
        when "USB_MIDI_DEVICE" then 0x04
        else                        0x03
        end
      end
    end

    # --- Balls ---

    # Number of balls currently in the polygon
    def balls
      UI._tombola_ball_count
    end

    # Grow or shrink the population to +count+ using default balls
    def balls=(count)
      while UI._tombola_ball_count < count
        break if add_ball < 0
      end
      index = MAX_BALLS
      while UI._tombola_ball_count > count && index > 0
        index -= 1
        UI._tombola_remove_ball(index)
      end
    end

    # @param note [Integer, nil] fixed note; nil takes the note from the scale
    # @param channel [Integer, nil] nil uses the sequencer's default channel
    # @param color [Symbol, Integer] ball color
    # @param velocity_scale [Float] per-ball velocity multiplier
    # @return [Integer] ball index, or -1 when all 16 slots are in use
    def add_ball(note: nil, channel: nil, color: :white, velocity_scale: 1.0)
      color_code = color.is_a?(Integer) ? color : UI.color_to_rgb565(color)
      UI._tombola_add_ball(note, channel, color_code, velocity_scale)
    end

    def remove_ball(index)
      UI._tombola_remove_ball(index)
    end

    def clear_balls
      UI._tombola_clear_balls
    end

    # --- Hits ---

    # Called for every collision with {ball:, side:, note:, velocity:}.
    # Delivered from UI.process, so the script must keep calling it.
    def on_hit(&block)
      $ui_tombola_handler = block
      UI._tombola_set_i(:notify, block ? 1 : 0)
    end

    # Whether a tap inside the polygon spawns a ball
    def touch_add?
      UI._tombola_get_i(:touch_add) != 0
    end

    # Tapping inside the polygon spawns a ball there (default true)
    def touch_add=(value)
      UI._tombola_set_i(:touch_add, value ? 1 : 0)
    end

    # --- Transport ---

    # Start the simulation. Spawns three default balls if none exist yet.
    def start
      UI._tombola_start
    end

    def stop
      UI._tombola_stop
    end

    def running?
      UI._tombola_running
    end

    # Restore every parameter to its default and drop all balls
    def reset
      UI._tombola_reset
      $ui_tombola_handler = nil
      @scale = nil
      @device = nil
    end

    # Bring the Tombola screen to the front
    def show
      UI.set_screen(UI::SCREEN_TOMBOLA)
    end
  end
end
