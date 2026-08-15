# UI Event Handler
#
# Provides Ruby API for handling UI events from M5Stack touch interface

# Global handler storage
$ui_handlers = {}

# Pad callback storage (index => block)
$ui_pad_callbacks = {}

# Knob callback storage (bank * UI::KNOB_KEY_STRIDE + index => block)
$ui_knob_callbacks = {}

# Tombola hit callback (single sequencer instance)
$ui_tombola_handler = nil

# The live UI::XYPad instance (single instance, like Tombola -- the C-side
# model is a global singleton). Holds the Ruby-only per-slot state (CC
# numbers, velocity, device, auto_midi) that ui_xypad_* on the C side does not
# know about.
$ui_xypad_instance = nil

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
  SCREEN_KNOBS     = 7
  SCREEN_XYPAD     = 8

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

    # Handle knob turns with callbacks
    if type == :knob_change
      dispatch_knob_event(event)
    end

    # Handle tombola hits registered through Tombola#on_hit
    if type == :tombola_hit
      dispatch_tombola_event(event)
    end

    # Handle XYPad touch phases (built-in MIDI handler + on_touch)
    if type == :xypad_touch
      dispatch_xypad_event(event)
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

  # Internal: dispatch knob change to the registered callback
  def self.dispatch_knob_event(event)
    block = $ui_knob_callbacks[knob_key(event[:bank] - 1, event[:index] - 1)]
    return unless block
    block.call(event[:value])
  rescue => e
    log("Error in knob callback: " + e.message)
  end

  # Internal: dispatch tombola hit to the registered callback
  def self.dispatch_tombola_event(event)
    handler = $ui_tombola_handler
    return unless handler
    handler.call(event)
  rescue => e
    log("Error in tombola callback: " + e.message)
  end

  # Internal: dispatch an XYPad touch phase to the live instance
  def self.dispatch_xypad_event(event)
    pad = $ui_xypad_instance
    return unless pad
    pad.dispatch_touch(event)
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

  # === Knobs ===
  #
  # Continuous values turned by sweeping a finger around the ring. Nothing here
  # sends MIDI: like a pad, a knob calls the block it was given and the block
  # decides what that means.
  #
  #   UI.knob(1, label: "Cutoff", color: :cyan) do |v|
  #     device.control_change(74, v.to_i)
  #   end
  #
  # The block runs from UI.process, so a script has to keep calling it. Only
  # one change per knob is ever queued -- a new value replaces the one waiting
  # -- so polling slowly costs resolution, never a backlog.

  KNOB_ORIGIN_MIN    = 0
  KNOB_ORIGIN_CENTER = 1

  # Callback keys are bank * stride + index. A flat integer key beats an array
  # one in mruby/c, and the stride only has to exceed the largest board's knob
  # count (12 on Tab5).
  KNOB_KEY_STRIDE = 16

  # Knobs per bank on this board (6 on CoreS3, 12 on Tab5)
  def self.knob_count
    _knob_count
  end

  # Number of banks (4: A-D)
  def self.knob_banks
    _knob_banks
  end

  # Bank currently on screen (1-based)
  def self.knob_bank
    _knob_get_bank + 1
  end

  # Switch the visible bank
  def self.knob_bank=(bank)
    _knob_set_bank(bank.to_i - 1)
    bank
  end

  def self.knob_key(bank_index, index)
    bank_index * KNOB_KEY_STRIDE + index
  end

  # Internal: resolve a bank: argument to a 0-based index, defaulting to the
  # visible bank so an encoder loop naturally follows what is on screen.
  def self.knob_bank_index(bank)
    return _knob_get_bank if bank.nil?
    b = bank.to_i - 1
    return nil if b < 0 || b >= _knob_banks
    b
  end

  # Configure a knob
  # @param index [Integer] Knob index (1-based)
  # @param label [String] Display label
  # @param color [Symbol] Gauge color
  # @param value [Numeric] Initial value (default: min, or the middle when
  #   origin is :center). Setting it does not call the block.
  # @param min [Numeric] Range low end
  # @param max [Numeric] Range high end
  # @param step [Numeric] Quantum; 0 for continuous
  # @param origin [Symbol] :min or :center (which end the gauge grows from)
  # @param sensitivity [Float] 1.0 makes the gauge track the fingertip exactly
  # @param bank [Integer] Bank to define in (default: the visible one)
  # @param block [Proc] Called with the new value whenever the knob moves
  def self.knob(index, label: nil, color: :gray, value: nil, min: 0, max: 127,
                step: 1, origin: :min, sensitivity: 1.0, bank: nil, &block)
    idx = index - 1
    return unless idx >= 0 && idx < _knob_count
    b = knob_bank_index(bank)
    return unless b

    origin_code = (origin == :center) ? KNOB_ORIGIN_CENTER : KNOB_ORIGIN_MIN
    min_f = min.to_f
    max_f = max.to_f
    if value.nil?
      initial = (origin_code == KNOB_ORIGIN_CENTER) ? (min_f + max_f) / 2.0 : min_f
    else
      initial = value.to_f
    end

    _knob_set(b, idx, label || ("Knob " + index.to_s), color_to_rgb565(color),
              min_f, max_f, step.to_f, initial, origin_code, sensitivity.to_f,
              block ? true : false)

    key = knob_key(b, idx)
    if block
      $ui_knob_callbacks[key] = block
    else
      $ui_knob_callbacks.delete(key)
    end
  end

  # Current value
  # @return [Float]
  def self.knob_value(index, bank: nil)
    idx = index - 1
    b = knob_bank_index(bank)
    return 0.0 unless b && idx >= 0 && idx < _knob_count
    _knob_value(b, idx)
  end

  # Set a knob's value, as an encoder or another script would.
  #
  # Calls the block, because a knob moved from anywhere still has to reach
  # whatever it drives. That does not loop: the block only runs when the
  # quantized value actually changed, so writing the value back from inside it
  # stops immediately. Pass notify: false to update the display alone.
  #
  # @return [Boolean] whether the value moved
  def self.knob_set(index, value, bank: nil, notify: true)
    idx = index - 1
    b = knob_bank_index(bank)
    return false unless b && idx >= 0 && idx < _knob_count

    changed = _knob_set_value(b, idx, value.to_f)
    if changed && notify
      block = $ui_knob_callbacks[knob_key(b, idx)]
      if block
        begin
          block.call(_knob_value(b, idx))
        rescue => e
          log("Error in knob callback: " + e.message)
        end
      end
    end
    changed
  end

  # Put a knob back to the value it was defined with (block runs)
  def self.knob_reset(index, bank: nil)
    idx = index - 1
    b = knob_bank_index(bank)
    return false unless b && idx >= 0 && idx < _knob_count
    changed = _knob_reset(b, idx)
    if changed
      block = $ui_knob_callbacks[knob_key(b, idx)]
      block.call(_knob_value(b, idx)) if block
    end
    changed
  end

  def self.knob_reset_all(bank: nil)
    b = knob_bank_index(bank)
    return unless b
    i = 0
    while i < _knob_count
      knob_reset(i + 1, bank: b + 1)
      i += 1
    end
  end

  # Call every knob's block with its current value. Use it after defining the
  # knobs to push the initial values out, or to re-sync a synth that was
  # plugged in later. bank: :all covers every bank, not just the visible one.
  def self.knob_send_all(bank: nil)
    if bank == :all
      b = 0
      while b < _knob_banks
        knob_send_bank(b)
        b += 1
      end
    else
      idx = knob_bank_index(bank)
      knob_send_bank(idx) if idx
    end
  end

  def self.knob_send_bank(bank_index)
    i = 0
    while i < _knob_count
      block = $ui_knob_callbacks[knob_key(bank_index, i)]
      if block && _knob_assigned(bank_index, i)
        begin
          block.call(_knob_value(bank_index, i))
        rescue => e
          log("Error in knob callback: " + e.message)
        end
      end
      i += 1
    end
  end

  def self.knob_label(index, label, bank: nil)
    idx = index - 1
    b = knob_bank_index(bank)
    return unless b && idx >= 0 && idx < _knob_count
    _knob_set_label(b, idx, label)
  end

  def self.knob_color(index, color, bank: nil)
    idx = index - 1
    b = knob_bank_index(bank)
    return unless b && idx >= 0 && idx < _knob_count
    _knob_set_color(b, idx, color_to_rgb565(color))
  end

  def self.knob_clear(index, bank: nil)
    idx = index - 1
    b = knob_bank_index(bank)
    return unless b && idx >= 0 && idx < _knob_count
    _knob_clear(b, idx)
    $ui_knob_callbacks.delete(knob_key(b, idx))
  end

  def self.knob_clear_all
    _knob_clear_all
    $ui_knob_callbacks = {}
  end

  # Bring the Knobs screen to the front
  def self.knobs
    set_screen(SCREEN_KNOBS)
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

  # XYPad - up to five independently configured touch slots
  #
  # Each of up to five simultaneous fingers is its own "slot": X can snap to
  # a scale and glide via pitch bend, or behave exactly like Y (a plain CC).
  # Each slot keeps its own scale/CC numbers, channel, Hold flag and even its
  # own MIDI destination -- one finger can play a synth while another drives
  # a completely different effects box. See docs/XYPAD.md for the design.
  #
  #   pad = UI::XYPad.new(scale: [36, 38, 40, 41, 43, 45, 47, 48],
  #                       y_cc: 74, device: dev)
  #   pad.slot(3, x_mode: :cc, x_cc: 70, y_cc: 71, note: 60, device: fx)
  #   pad.show
  #
  # C++ side (the ui_xypad_* API in ui_common.h) owns touch tracking, scale
  # snapping and the glide math, exactly like Knobs -- nothing there sends
  # MIDI. The built-in handler here does that from #dispatch_touch, unless a
  # slot's auto_midi is false.
  class XYPad
    MAX_TOUCHES = 5
    XMODE_NOTE = 0
    XMODE_CC   = 1

    def initialize(x_mode: nil, scale: nil, glide_range: nil,
                   x_cc: nil, x_range: nil,
                   y_cc: nil, y_range: nil, y_invert: nil,
                   note: nil, velocity: nil,
                   channel: nil, channel_base: nil, hold: nil,
                   auto_midi: nil, device: nil, max_touches: nil)
      # UI._xypad_reset does not itself send MIDI (nothing in C does, see
      # docs/XYPAD.md), so a previous instance's slots latched via Hold would
      # otherwise go silently stuck the moment the model resets under them.
      # Releasing them here, through the *old* instance while it is still
      # $ui_xypad_instance, sends a proper note-off on whatever device/channel
      # that instance was actually using.
      prev = $ui_xypad_instance
      if prev
        prev.hold = false
        UI.process
      end

      UI._xypad_reset
      $ui_xypad_instance = self
      @on_touch = nil
      @channel_base = 0
      @slots = []
      @touch_x_mode = []
      i = 0
      while i < MAX_TOUCHES
        @slots << { x_cc: 70, y_cc: 74, velocity: 100, device: nil, auto_midi: true }
        @touch_x_mode << :note
        i += 1
      end

      self.max_touches = max_touches unless max_touches.nil?
      self.channel_base = channel_base unless channel_base.nil?

      # Every slot starts out identical. channel is the one field that must
      # not collapse to the same value on every slot by default, so it gets
      # resolved per index here rather than left to configure_slot's plain
      # "only touch what was given" rule.
      i = 0
      while i < MAX_TOUCHES
        ch = channel.nil? ? (@channel_base + i) : channel
        configure_slot(i, x_mode: x_mode, scale: scale, glide_range: glide_range,
                       x_cc: x_cc, x_range: x_range,
                       y_cc: y_cc, y_range: y_range, y_invert: y_invert,
                       note: note, velocity: velocity,
                       channel: ch, hold: hold,
                       auto_midi: auto_midi, device: device)
        i += 1
      end
    end

    def max_touches
      @max_touches
    end

    def max_touches=(n)
      @max_touches = n.to_i
      UI._xypad_set_max_touches(@max_touches)
      n
    end

    def channel_base
      @channel_base
    end

    def channel_base=(n)
      @channel_base = n.to_i
    end

    # Configure slot +index+ (1-based, like UI.knob). Only the keywords
    # actually given are changed -- everything else keeps its current value.
    # Called with no keywords at all, returns the slot's current config as a
    # Hash instead of changing anything.
    def slot(index, x_mode: nil, scale: nil, glide_range: nil,
             x_cc: nil, x_range: nil,
             y_cc: nil, y_range: nil, y_invert: nil,
             note: nil, velocity: nil,
             channel: nil, hold: nil,
             auto_midi: nil, device: nil)
      idx = index - 1
      return nil unless idx >= 0 && idx < MAX_TOUCHES

      if x_mode.nil? && scale.nil? && glide_range.nil? && x_cc.nil? && x_range.nil? &&
         y_cc.nil? && y_range.nil? && y_invert.nil? && note.nil? && velocity.nil? &&
         channel.nil? && hold.nil? && auto_midi.nil? && device.nil?
        return slot_info(idx)
      end

      configure_slot(idx, x_mode: x_mode, scale: scale, glide_range: glide_range,
                     x_cc: x_cc, x_range: x_range,
                     y_cc: y_cc, y_range: y_range, y_invert: y_invert,
                     note: note, velocity: velocity,
                     channel: channel, hold: hold,
                     auto_midi: auto_midi, device: device)
      nil
    end

    # Broadcast Hold to every slot at once, so a script can keep the simple
    # "one Hold button for the whole pad" feel from before slots existed.
    # pad.slot(n, hold: on) still overrides one slot only.
    def hold=(value)
      i = 0
      while i < MAX_TOUCHES
        UI._xypad_set_i(i, :hold, value ? 1 : 0)
        i += 1
      end
      value
    end

    # Called with the raw event hash for every touch phase, on every slot.
    def on_touch(&block)
      @on_touch = block
    end

    # Bring the XYPad screen to the front
    def show
      UI.set_screen(UI::SCREEN_XYPAD)
    end

    # Internal: applies the built-in MIDI handler for one slot's touch event,
    # then forwards to #on_touch if one is registered. Called from
    # UI.dispatch_xypad_event, itself called from UI.process.
    def dispatch_touch(event)
      idx = event[:slot] - 1
      return unless idx >= 0 && idx < MAX_TOUCHES
      s = @slots[idx]

      # The mode that governs this touch is frozen at its touchdown (the C
      # side does the same with touch_x_mode/touch_glide_range), so a script
      # changing x_mode mid-drag cannot flip how bend_semitones/x are read
      # for a touch already in progress.
      if event[:phase] == :down
        @touch_x_mode[idx] = (UI._xypad_get_i(idx, :x_mode) == XMODE_CC) ? :cc : :note
      end

      send_midi(s, @touch_x_mode[idx], idx, event) if s[:auto_midi] && s[:device]

      @on_touch.call(event) if @on_touch
    rescue => e
      UI.log("Error in xypad callback: " + e.message)
    end

    private

    def slot_info(idx)
      s = @slots[idx]
      x_mode = (UI._xypad_get_i(idx, :x_mode) == XMODE_CC) ? :cc : :note
      {
        x_mode: x_mode,
        scale: UI._xypad_get_scale(idx),
        glide_range: UI._xypad_get_f(idx, :glide_range),
        x_cc: s[:x_cc],
        x_range: UI._xypad_get_f(idx, :x_min)..UI._xypad_get_f(idx, :x_max),
        y_cc: s[:y_cc],
        y_range: UI._xypad_get_f(idx, :y_min)..UI._xypad_get_f(idx, :y_max),
        y_invert: UI._xypad_get_i(idx, :y_invert) != 0,
        note: UI._xypad_get_i(idx, :gate_note),
        velocity: s[:velocity],
        channel: UI._xypad_get_i(idx, :channel),
        hold: UI._xypad_get_i(idx, :hold) != 0,
        auto_midi: s[:auto_midi],
        device: s[:device]
      }
    end

    def configure_slot(idx, x_mode: nil, scale: nil, glide_range: nil,
                        x_cc: nil, x_range: nil,
                        y_cc: nil, y_range: nil, y_invert: nil,
                        note: nil, velocity: nil,
                        channel: nil, hold: nil,
                        auto_midi: nil, device: nil)
      s = @slots[idx]
      s[:x_cc] = x_cc unless x_cc.nil?
      s[:y_cc] = y_cc unless y_cc.nil?
      s[:velocity] = velocity unless velocity.nil?
      s[:device] = device unless device.nil?
      s[:auto_midi] = auto_midi unless auto_midi.nil?

      UI._xypad_set_i(idx, :x_mode, x_mode == :cc ? XMODE_CC : XMODE_NOTE) unless x_mode.nil?
      UI._xypad_set_scale(idx, scale) unless scale.nil?
      UI._xypad_set_f(idx, :glide_range, glide_range.to_f) unless glide_range.nil?
      unless x_range.nil?
        UI._xypad_set_f(idx, :x_min, x_range.first.to_f)
        UI._xypad_set_f(idx, :x_max, x_range.last.to_f)
      end
      unless y_range.nil?
        UI._xypad_set_f(idx, :y_min, y_range.first.to_f)
        UI._xypad_set_f(idx, :y_max, y_range.last.to_f)
      end
      UI._xypad_set_i(idx, :y_invert, y_invert ? 1 : 0) unless y_invert.nil?
      UI._xypad_set_i(idx, :gate_note, note) unless note.nil?
      UI._xypad_set_i(idx, :channel, channel) unless channel.nil?
      UI._xypad_set_i(idx, :hold, hold ? 1 : 0) unless hold.nil?
    end

    # Sends the actual MIDI for one touch phase. mode is this touch's frozen
    # x_mode (see #dispatch_touch), not necessarily the slot's live one.
    def send_midi(s, mode, idx, event)
      dev = s[:device]
      ch = event[:channel]

      case event[:phase]
      when :down
        dev.note_on(event[:note], s[:velocity], channel: ch)
        dev.control_change(s[:x_cc], event[:x].to_i, channel: ch) if mode == :cc
        dev.control_change(s[:y_cc], event[:y].to_i, channel: ch)
      when :move
        if mode == :note
          dev.pitch_bend(bend_raw(idx, event[:bend_semitones]), channel: ch)
        else
          dev.control_change(s[:x_cc], event[:x].to_i, channel: ch)
        end
        dev.control_change(s[:y_cc], event[:y].to_i, channel: ch)
      when :up
        dev.note_off(event[:note], 0, channel: ch)
        dev.pitch_bend(0, channel: ch) if mode == :note
      end
    end

    # bend_semitones -> the raw +-8192 unit MIDI::Device#pitch_bend takes,
    # scaled by the glide_range that was actually in effect when this touch
    # landed (not the slot's current one, which a script may have since
    # changed).
    def bend_raw(idx, bend_semitones)
      range = UI._xypad_get_f(idx, :touch_glide_range)
      return 0 if range <= 0.0
      raw = (bend_semitones / range * 8192).to_i
      raw = 8191 if raw > 8191
      raw = -8192 if raw < -8192
      raw
    end
  end
end
