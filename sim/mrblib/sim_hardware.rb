# The device's hardware gems (components/midori-runtime/build_config/
# picoruby_gems.rb) in the simulator, which has no pins. sim/build.sh puts
# this file before picoruby-gpio's and picoruby-dfrobot_rotary_encoder's
# mrblib and sim_boot.rb.
#
# - Their require succeeds (it raised LoadError and stopped scripts that use
#   the hardware only when the board has it, as examples/knobs.rb does with
#   BoardConfig::HAS_PRIMARY_I2C).
# - GPIO is picoruby-gpio's own Ruby (its mrblib, put after this file) over
#   mock pins: an output reads back what was written; an input reads what the
#   script drives it to (GPIO.drive), else its pull (PULL_UP 1, else 0).
# - I2C is a bus with nothing on it, as on a device with no wires: new
#   succeeds, reading or writing an address raises IOError, as the device's
#   does. A script can put mock devices on it (I2C.attach, I2C::RegisterDevice).
#   DFRobotRotaryEncoder is the gem's own (pure Ruby over I2C).
# - The other classes (SPI, UART, ...) are there with their constants, but making one raises
#   NotImplementedError.

SIM_NO_HARDWARE = %w[gpio i2c spi adc pwm uart rmt sdmmc adafruit_sk6812 dfrobot_rotary_encoder]

module Kernel
  alias sim_require_with_hardware require
  def require(name)
    if SIM_NO_HARDWARE.include?(name)
      return false if $LOADED_FEATURES.include?(name)
      $LOADED_FEATURES << name
      return true
    end
    sim_require_with_hardware(name)
  end
end

class SimNoHardware
  def self.new(*args, **kw)
    raise NotImplementedError, "#{self} is not available in the simulator"
  end

  # Any other constant (UART::PARITY_NONE, ...) reads as 0
  def self.const_missing(name)
    0
  end
end

%i[SPI ADC PWM UART RMT SDMMC].each do |sym|
  Object.const_set(sym, Class.new(SimNoHardware)) unless Object.const_defined?(sym)
end

# The part of picoruby-gpio its C binding (src/mruby/gpio.c) defines, over a
# table of mock pins; GPIO#initialize, #setmode, ... are the gem's mrblib.
#
#   led = GPIO.new(10, GPIO::OUT)
#   led.write(1)
#   GPIO.read_at(10)                   # => 1
#   btn = GPIO.new(0, GPIO::IN | GPIO::PULL_UP)
#   btn.read                           # => 1 (the pull-up)
#   GPIO.drive(0, 0)                   # as if the button were pressed
#   btn.low?                           # => true
#   GPIO.drive(0, nil)                 # released: the pull-up again
unless Object.const_defined?(:GPIO)
  class GPIO
    IN         = 0b0000001
    OUT        = 0b0000010
    HIGH_Z     = 0b0000100
    PULL_UP    = 0b0001000
    PULL_DOWN  = 0b0010000
    OPEN_DRAIN = 0b0100000
    ALT        = 0b1000000

    # pin => { dir:, pull:, open_drain:, function:, out:, in: }
    @pins = {}

    class << self
      # A pin's key, as the binding reads a pin (an Integer, or a name)
      def _pin(pin)
        key = case pin
              when Integer then pin
              when String, Symbol
                name = pin.to_s
                digits = !name.empty? && name.bytes.all? { |b| b >= 0x30 && b <= 0x39 }
                digits ? name.to_i : name
              end
        if key.nil? || (key.is_a?(Integer) && key < 0) || key == ""
          raise ArgumentError, "Wrong GPIO pin name: #{pin.inspect}"
        end
        key
      end

      def _state(pin)
        @pins[_pin(pin)] ||= { dir: nil, pull: nil, open_drain: false, function: nil, out: 0, in: nil }
      end

      def _value(value)
        raise ArgumentError, "Wrong value. 0 and 1 are only valid" unless value == 0 || value == 1
        value
      end

      # The mock pins (the script's side, not the binding's)

      # Drives a pin from outside, as a button or another chip would
      # (0 / 1; nil releases it)
      def drive(pin, level)
        _state(pin)[:in] = level.nil? ? nil : _value(level)
        level
      end

      # Every pin a script has touched: pin => { dir:, pull:, out:, in:, ... }
      def pins
        @pins
      end

      # The binding's class methods

      def set_function_at(pin, alt_function)
        _state(pin)[:function] = alt_function
        0
      end

      def set_dir_at(pin, flags)
        _state(pin)[:dir] = flags & (IN | OUT | HIGH_Z)
        0
      end

      def pull_up_at(pin)
        _state(pin)[:pull] = PULL_UP
        0
      end

      def pull_down_at(pin)
        _state(pin)[:pull] = PULL_DOWN
        0
      end

      def open_drain_at(pin)
        _state(pin)[:open_drain] = true
        0
      end

      # An output pushes its level; an open-drain output's 1 lets go, as an
      # input does: what drives it, else its pull
      def read_at(pin)
        st = _state(pin)
        if st[:dir] == OUT && !(st[:open_drain] && st[:out] == 1)
          st[:out]
        elsif !st[:in].nil?
          st[:in]
        else
          st[:pull] == PULL_UP ? 1 : 0
        end
      end

      def write_at(pin, value)
        _state(pin)[:out] = _value(value)
        0
      end

      def high_at?(pin)
        read_at(pin) != 0
      end

      def low_at?(pin)
        read_at(pin) == 0
      end
    end

    def read
      GPIO.read_at(@pin)
    end

    def write(value)
      GPIO.write_at(@pin, value)
    end

    def high?
      GPIO.high_at?(@pin)
    end

    def low?
      GPIO.low_at?(@pin)
    end

    private

    def _init(pin)
      GPIO._state(pin)
      0
    end
  end
end

# picoruby-i2c's API (sig/i2c.rbs) over a bus shared by every instance.
#
#   i2c = I2C.new(unit: :ESP32_I2C0, sda_pin: 53, scl_pin: 54)
#   i2c.read(0x54, 2)                  # IOError: nothing at 0x54
#   dev = I2C::RegisterDevice.new
#   dev.set(0x00, 0x01, 0xF6)          # registers 0x00, 0x01
#   I2C.attach(0x54, dev)
#   i2c.write(0x54, 0x00)              # => 1 (the register pointer)
#   i2c.read(0x54, 2)                  # => "\x01\xF6"
#
# A device is any object with i2c_write(bytes) and i2c_read(len) (an Array
# of Integer or a String); write and read call them as the bus would.
unless Object.const_defined?(:I2C)
  class I2C
    DEFAULT_FREQUENCY = 100_000 # Hz
    DEFAULT_TIMEOUT = 500 # ms

    @devices = {}

    class << self
      # Puts a mock device at a 7-bit address (nil takes it off)
      def attach(address, device)
        if device
          @devices[address] = device
        else
          @devices.delete(address)
        end
        device
      end

      def detach(address)
        @devices.delete(address)
      end

      def device(address)
        @devices[address]
      end

      def devices
        @devices
      end

      # The bytes of write / read's outputs, as the device's binding takes them
      def _bytes(outputs)
        bytes = []
        outputs.each do |o|
          case o
          when Integer
            bytes << (o & 0xFF)
          when String
            o.each_byte { |b| bytes << b }
          when Array
            o.each do |e|
              raise TypeError, "array element must be Fixnum" unless e.is_a?(Integer)
              bytes << (e & 0xFF)
            end
          else
            raise TypeError, "argument must be Integer, Array or String"
          end
        end
        bytes
      end

      def _string(data, len)
        data = data.bytes if data.is_a?(String)
        s = ""
        i = 0
        while i < len
          s << ((data[i] || 0) & 0xFF).chr
          i += 1
        end
        s
      end
    end

    attr_reader :unit, :frequency, :sda_pin, :scl_pin

    def initialize(unit: nil, frequency: DEFAULT_FREQUENCY, sda_pin: -1, scl_pin: -1, timeout: DEFAULT_TIMEOUT)
      @unit = unit
      @frequency = frequency
      @sda_pin = sda_pin
      @scl_pin = scl_pin
      @timeout = timeout
    end

    def write(i2c_adrs_7, *outputs, timeout: @timeout, nostop: false)
      bytes = I2C._bytes(outputs)
      dev = I2C.device(i2c_adrs_7)
      raise IOError, "I2C write failed" unless dev
      dev.i2c_write(bytes)
      bytes.size
    end

    def read(i2c_adrs_7, len, *outputs, timeout: @timeout)
      bytes = I2C._bytes(outputs)
      dev = I2C.device(i2c_adrs_7)
      unless dev
        raise IOError, (bytes.empty? ? "I2C read failed" : "I2C write (for read) failed")
      end
      dev.i2c_write(bytes) unless bytes.empty?
      I2C._string(dev.i2c_read(len), len)
    end

    def scan(timeout: @timeout)
      msg_format = "I2C device found at 7-bit address 0x%02x (0b%07b) +%s"
      i2c_adrs_7 = 0x08
      while i2c_adrs_7 <= 0x77
        if I2C.device(i2c_adrs_7)
          puts sprintf(msg_format, i2c_adrs_7, i2c_adrs_7, "R")
          puts sprintf(msg_format, i2c_adrs_7, i2c_adrs_7, "W")
        end
        i2c_adrs_7 += 1
      end
      nil
    end

    # A mock device with registers, as most I2C chips: a write's first byte
    # sets the register pointer, the rest are stored from there on; a read
    # returns bytes from the pointer. The pointer moves on with each byte.
    class RegisterDevice
      attr_reader :registers
      attr_accessor :pointer

      def initialize(size = 256)
        @registers = Array.new(size, 0)
        @pointer = 0
      end

      def [](reg)
        @registers[reg % @registers.size]
      end

      def []=(reg, value)
        @registers[reg % @registers.size] = value & 0xFF
      end

      # Sets registers from reg on (the script's side, not the bus's)
      def set(reg, *values)
        values.each_with_index { |v, i| self[reg + i] = v }
        self
      end

      def i2c_write(bytes)
        return if bytes.empty?
        @pointer = bytes[0]
        i = 1
        while i < bytes.size
          self[@pointer] = bytes[i]
          @pointer += 1
          i += 1
        end
      end

      def i2c_read(len)
        out = []
        len.times do
          out << self[@pointer]
          @pointer += 1
        end
        out
      end
    end
  end
end
