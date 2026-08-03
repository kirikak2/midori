# Tombola - physics driven sequencer
#
# Balls bounce inside a rotating polygon; each wall collision fires the note
# that the scale assigns to that side. The simulation runs on the C++ side and
# sounds every hit the moment it happens, so the timing does not depend on how
# often this script polls.
#
# The pads let you play with the parameters while it runs. If DFRobot Visual
# Rotary Encoders (SEN0502) are wired to Port A, up to four of them are picked
# up automatically and take over the main parameters:
#
#   | #   | 回すと     | クリックすると |
#   |-----|------------|----------------|
#   | 1台目 | rotation   | add_ball       |
#   | 2台目 | sides      | clear_balls    |
#   | 3台目 | gravity    | オクターブ -1  |
#   | 4台目 | bounce     | オクターブ +1  |
#
# 検出は DIP スイッチのアドレス順 (0x54, 0x55, 0x56, 0x57)。繋がっていない
# 分は黙って飛ばされるので、0 台でもこのスクリプトはそのまま動く。
#
# ※ Tab5 の Port A (53/54) は SAM2695 の UART と同じピンなので、エンコーダと
#   SAM2695 は同時に使えない。エンコーダが 1 台でも見つかったら、出力を
#   USB-MIDI デバイス (USB-C) → USB-MIDI ホストの順で選び直す。

require 'midi'
require 'ui'
require 'gpio'
require 'i2c'
require 'machine'
require 'dfrobot_rotary_encoder'

# ---- 設定 ------------------------------------------------------------
SDA  = 53                 # Port A (Tab5)。他のボードでは配線に合わせて変更
SCL  = 54
GAIN = 51                 # 1 ディテント = 51 カウント -> 1 回転でフルレンジ

# ディテントで到達できる最大カウンタ値。カウンタは GAIN 刻みでしか動かないので
# 上限は 1023 ではなく GAIN の倍数に切り下げた値になる。
V_MAX = (1023 / GAIN) * GAIN

# ボタンの多重検出よけ (ms)。SEN0502 のキーステータスは「押下でフラグを立て、
# 読んだらクリア」方式で 1 クリックでも複数回立つため。
BUTTON_LOCKOUT_MS = 300

# エンコーダのポーリング周期 (ms)。UI イベントより粗くてよい。
ENCODER_POLL_MS = 20

# ---- エンコーダ 1 台 = Tombola のパラメータ 1 つ ----------------------
class EncoderKnob
  attr_reader :label

  # @param enc [DFRobotRotaryEncoder]
  # @param label [String] ログ表示用
  # @param min, max [Numeric] パラメータの下限・上限
  # @param integer [Boolean] 整数パラメータなら true
  # @param getter [Proc] 現在値を返す
  # @param setter [Proc] 値を受け取って設定する
  def initialize(enc:, label:, min:, max:, integer:, getter:, setter:)
    @enc = enc
    @label = label
    @min = min
    @max = max
    @integer = integer
    @getter = getter
    @setter = setter
    @last = nil
    @on_click = nil
    @armed = true       # 次の押下を受け付けてよいか
    @lock_until = 0     # この時刻 (ms) までは押下を無視する
  end

  def on_click(&block)
    @on_click = block
  end

  # 現在のパラメータ値をエンコーダのカウンタへ書き戻す。ノブの物理位置と
  # LED リングが、いま鳴っている設定を指すようになる。
  def sync
    span = (@max - @min).to_f
    v = 0
    if span > 0.0
      ratio = (@getter.call - @min).to_f / span
      ratio = 0.0 if ratio < 0.0
      ratio = 1.0 if ratio > 1.0
      v = (ratio * V_MAX.to_f).to_i
    end
    @enc.value = v
    @last = v
  end

  # 毎ループ呼ぶ。クリック -> on_click、回転 -> setter。
  def poll
    # フラグはロック中でも読んでクリアしておく必要があるので、button_down? は
    # 必ず呼ぶ。armed によるエッジ検出と lockout の 2 段で 1 クリック = 1 回に
    # 落とす (dfrobot_encoder_dual_cc.rb と同じ手当て)。
    now = Machine.uptime_us / 1000
    if @enc.button_down?
      if @armed && now >= @lock_until
        @armed = false
        @lock_until = now + BUTTON_LOCKOUT_MS
        @on_click.call if @on_click
        return
      end
    elsif now >= @lock_until
      @armed = true
    end

    v = @enc.value    # 0-V_MAX or nil (I2C 失敗)
    return unless v
    return if v == @last
    @last = v

    value = @min + (@max - @min).to_f * v.to_f / V_MAX.to_f
    if @integer
      # mruby/c の Float には round/floor が無いので +0.5 して切り捨てる
      value = (value + 0.5).to_i
    end
    @setter.call(value)
  end
end

# ---- オクターブシフト ------------------------------------------------
# scale をまるごと ±12 半音して入れ直す。1 音でも 0-127 を外れるならシフト
# 自体を諦める: 端に当たった音だけクランプすると音列の形が崩れてしまう。
def shift_octave(t, direction)
  notes = t.scale
  return unless notes

  semitones = direction * 12
  shifted = []
  i = 0
  while i < notes.size
    v = notes[i] + semitones
    return if v < 0 || v > 127
    shifted << v
    i += 1
  end

  t.scale = shifted
  UI.log("octave #{direction > 0 ? '+' : '-'}1 -> #{shifted[0]}")
end

# ---- エンコーダ検出 (Port A) -----------------------------------------
# 直前まで SAM2695(UART) が同じピンを使っていた場合のルーティング残りを剥がす。
# ピンがそのボードに無ければ I2C ごと諦めて 0 台で続行する。
encoders = []
begin
  GPIO.new(SDA, GPIO::IN)
  GPIO.new(SCL, GPIO::IN)
  i2c = I2C.new(unit: "ESP32_I2C0", sda_pin: SDA, scl_pin: SCL, frequency: 100_000)

  addrs = DFRobotRotaryEncoder::ADDRESSES   # [0x54, 0x55, 0x56, 0x57]
  a = 0
  while a < addrs.size
    e = DFRobotRotaryEncoder.new(i2c: i2c, address: addrs[a])
    if e.connected?
      e.gain = GAIN
      encoders << e
      UI.log(sprintf("enc%d -> 0x%02x", encoders.size, addrs[a]))
    end
    a += 1
  end
rescue => ex
  UI.log("encoder init failed: #{ex.message}")
end

if encoders.size == 0
  UI.log("no encoder found on 0x54-0x57 (pads only)")
else
  UI.log("#{encoders.size} encoder(s) found")
end

# ---- 出力先 ----------------------------------------------------------
# エンコーダが Port A を占有している間は SAM2695 が使えないので、USB 側へ回す。
transport = nil
if encoders.size > 0
  transport = MIDIDevices.usb_midi_device
  transport = MIDIDevices.usb_midi_host unless transport
  UI.log("Port A is used by the encoders -> SAM2695 unavailable") unless transport
else
  transport = MIDIDevices.sam2695
end
transport = MIDIDevices.sam2695 unless transport

unless transport
  puts "No MIDI output available on this board"
  exit
end

device = MIDI::Device.new(transport)
device.program_change(0, channel: 9)  # Standard drum kit

t = UI::Tombola.new(
  sides: 6,
  rotation: 12,          # RPM, negative to spin the other way
  gravity: 0.5,
  gravity_mode: :down,
  bounce: 0.8,
  spin_transfer: 0.3,    # Without this the balls settle at the bottom
  channel: 9,
  duration: 120,
  velocity_range: 40..127,
  device: device
)

# General MIDI drums, one per side of the hexagon
t.scale = [36, 38, 42, 45, 46, 49]
t.sound = false

t.add_ball(color: :red)
t.add_ball(color: :cyan)
t.add_ball(color: :yellow, velocity_scale: 0.7)

# Optional: watch the hits from Ruby. The C++ side sounds them too, so this
# doubles every note unless you set t.sound = false.
t.on_hit do |hit|
  device.trigger(hit[:note], hit[:velocity], duration: t.duration)
end

t.start
t.show

# ---- エンコーダ割り当て ----------------------------------------------
# 1台目 rotation / 2台目 sides / 3台目 gravity / 4台目 bounce。
# それぞれ Tombola 側のクランプ範囲より狭い、演奏に使いやすい幅にしてある。
# クリックは add_ball / clear_balls / オクターブ -1 / オクターブ +1。
knobs = []
knob_rotation = nil
knob_sides = nil

if encoders[0]
  knob_rotation = EncoderKnob.new(
    enc: encoders[0], label: "rotation",
    min: -60.0, max: 60.0, integer: false,
    getter: Proc.new { t.rotation },
    setter: Proc.new { |v| t.rotation = v }
  )
  knob_rotation.on_click { t.add_ball }
  knobs << knob_rotation
end

if encoders[1]
  knob_sides = EncoderKnob.new(
    enc: encoders[1], label: "sides",
    min: 3, max: 16, integer: true,
    getter: Proc.new { t.sides },
    setter: Proc.new { |v| t.sides = v }
  )
  knob_sides.on_click { t.clear_balls }
  knobs << knob_sides
end

if encoders[2]
  knob_gravity = EncoderKnob.new(
    enc: encoders[2], label: "gravity",
    min: 0.0, max: 2.0, integer: false,
    getter: Proc.new { t.gravity },
    setter: Proc.new { |v| t.gravity = v }
  )
  knob_gravity.on_click { shift_octave(t, -1) }
  knobs << knob_gravity
end

if encoders[3]
  knob_bounce = EncoderKnob.new(
    enc: encoders[3], label: "bounce",
    min: 0.0, max: 1.2, integer: false,
    getter: Proc.new { t.bounce },
    setter: Proc.new { |v| t.bounce = v }
  )
  knob_bounce.on_click { shift_octave(t, 1) }
  knobs << knob_bounce
end

# LED リングを現在値に合わせてから回し始める
i = 0
while i < knobs.size
  knobs[i].sync
  i += 1
end

# ---- パッド ----------------------------------------------------------
# パッドで動かしたぶんはエンコーダのカウンタに書き戻す。そうしないと LED
# リングとノブの位置がパラメータからずれ、次にノブを回した瞬間に値が飛ぶ。
UI.pad(1, label: "Slower", color: :blue) do
  t.rotation = t.rotation - 4
  knob_rotation.sync if knob_rotation
end
UI.pad(2, label: "Faster", color: :blue) do
  t.rotation = t.rotation + 4
  knob_rotation.sync if knob_rotation
end
UI.pad(3, label: "Sides-", color: :green) do
  t.sides = t.sides - 1
  knob_sides.sync if knob_sides
end
UI.pad(4, label: "Sides+", color: :green) do
  t.sides = t.sides + 1
  knob_sides.sync if knob_sides
end
UI.pad(5, label: "Ball+", color: :orange) { t.add_ball }
UI.pad(6, label: "Clear", color: :red) { t.clear_balls }

# ---- メインループ ----------------------------------------------------
# The tombola keeps stepping on its own; this loop only delivers UI events
# (pad presses and, because on_hit is registered, the hits) and reads the
# encoders.
sm = ScriptManager.new
last_poll = 0
loop do
  UI.process

  now = Machine.uptime_us / 1000
  if now - last_poll >= ENCODER_POLL_MS
    last_poll = now
    i = 0
    while i < knobs.size
      knobs[i].poll
      i += 1
    end
  end

  break if sm.stop_requested?
  sleep_ms 5
end

t.stop
