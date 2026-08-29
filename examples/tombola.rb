# Tombola - physics driven sequencer
#
# Balls bounce inside a rotating polygon; each wall collision fires the note
# that the scale assigns to that side. The simulation runs on the C++ side and
# sounds every hit the moment it happens, so the timing does not depend on how
# often this script polls.
#
# Output always goes to the USB-MIDI device port (USB-C, TinyUSB), regardless
# of whether any encoders are attached -- the primary I2C bus is free either
# way.
#
# The variable parameters (rotation, sides, gravity, bounce) are Knobs, so they
# can be touched on the Knobs screen or turned from a physical encoder. If
# DFRobot Visual Rotary Encoders (SEN0502) are wired to the primary I2C bus
# (BoardConfig::PRIMARY_I2C_SDA_PIN / PRIMARY_I2C_SCL_PIN), up to four of them
# are picked up automatically and take over those same knobs:
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
# パッドは Ball+ / Clear / Octave- / Octave+ の4つ。エンコーダのクリックと
# 同じアクションを呼ぶ。

require 'midi'
require 'ui'
require 'gpio'
require 'i2c'
require 'machine'
require 'dfrobot_rotary_encoder'

# ---- 設定 ------------------------------------------------------------
GAIN = 51                 # 1 ディテント = 51 カウント -> 1 回転でフルレンジ

# ディテントで到達できる最大カウンタ値。カウンタは GAIN 刻みでしか動かないので
# 上限は 1023 ではなく GAIN の倍数に切り下げた値になる。
V_MAX = (1023 / GAIN) * GAIN

# ボタンの多重検出よけ (ms)。SEN0502 のキーステータスは「押下でフラグを立て、
# 読んだらクリア」方式で 1 クリックでも複数回立つため。
BUTTON_LOCKOUT_MS = 300

# エンコーダのポーリング周期 (ms)。UI イベントより粗くてよい。
ENCODER_POLL_MS = 20

# ---- ノブの割り当て ----------------------------------------------------
KNOB_ROTATION = 1
KNOB_SIDES    = 2
KNOB_GRAVITY  = 3
KNOB_BOUNCE   = 4

# ---- エンコーダ 1 台 = ノブ 1 個 ---------------------------------------
# 値はノブ (UI.knob_set / UI.knob_value) 経由で読み書きする。ブロックの中で
# Tombola のパラメータを変えるのはノブ側の仕事で、ここはノブとエンコーダの
# カウンタを橋渡しするだけ。
class EncoderKnob
  def initialize(enc:, knob_index:, min:, max:, integer:)
    @enc = enc
    @knob_index = knob_index
    @min = min
    @max = max
    @integer = integer
    @last = nil
    @on_click = nil
    @armed = true       # 次の押下を受け付けてよいか
    @lock_until = 0     # この時刻 (ms) までは押下を無視する
  end

  def on_click(&block)
    @on_click = block
  end

  # ノブの現在値をエンコーダのカウンタへ書き戻す。ノブの物理位置と LED リング
  # が、いま鳴っている設定を指すようになる。
  def sync
    span = (@max - @min).to_f
    v = 0
    if span > 0.0
      ratio = (UI.knob_value(@knob_index) - @min).to_f / span
      ratio = 0.0 if ratio < 0.0
      ratio = 1.0 if ratio > 1.0
      v = (ratio * V_MAX.to_f).to_i
    end
    @enc.value = v
    @last = v
  end

  # 毎ループ呼ぶ。クリック -> on_click、回転 -> UI.knob_set。
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
    UI.knob_set(@knob_index, value)
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

# ---- エンコーダ検出 (プライマリ I2C) -------------------------------------
# ボードにプライマリ I2C が無ければ 0 台で続行する。
encoders = []
if BoardConfig::HAS_PRIMARY_I2C
  begin
    sda = BoardConfig::PRIMARY_I2C_SDA_PIN
    scl = BoardConfig::PRIMARY_I2C_SCL_PIN
    GPIO.new(sda, GPIO::IN)
    GPIO.new(scl, GPIO::IN)
    i2c = I2C.new(unit: BoardConfig::PRIMARY_I2C_UNIT, sda_pin: sda, scl_pin: scl, frequency: 100_000)

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
end

if encoders.size == 0
  UI.log("no encoder found on 0x54-0x57 (pads only)")
else
  UI.log("#{encoders.size} encoder(s) found")
end

# ---- 出力先 ----------------------------------------------------------
# エンコーダの有無に関係なく USB-MIDI デバイス (USB-C) を使う。
transport = MIDIDevices.usb_midi_device

unless transport
  puts "No MIDI output available (USB-MIDI device not enabled on this board)"
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

# ---- ノブ: rotation / sides / gravity / bounce ------------------------
# クランプ範囲は Tombola 側より狭い、演奏に使いやすい幅にしてある。
UI.knob(KNOB_ROTATION, label: "Rotation", color: :cyan,
        min: -60, max: 60, step: 0, value: t.rotation) do |v|
  t.rotation = v
end

UI.knob(KNOB_SIDES, label: "Sides", color: :green,
        min: 3, max: 16, step: 1, value: t.sides) do |v|
  t.sides = v.to_i
end

UI.knob(KNOB_GRAVITY, label: "Gravity", color: :magenta,
        min: 0, max: 2, step: 0, value: t.gravity) do |v|
  t.gravity = v
end

UI.knob(KNOB_BOUNCE, label: "Bounce", color: :yellow,
        min: 0, max: 1.2, step: 0, value: t.bounce) do |v|
  t.bounce = v
end

# ---- パッド ----------------------------------------------------------
UI.pad(1, label: "Ball+", color: :orange) { t.add_ball }
UI.pad(2, label: "Clear", color: :red) { t.clear_balls }
UI.pad(3, label: "Oct -", color: :blue) { shift_octave(t, -1) }
UI.pad(4, label: "Oct +", color: :blue) { shift_octave(t, 1) }

# ---- エンコーダ割り当て ----------------------------------------------
# 1台目 rotation / 2台目 sides / 3台目 gravity / 4台目 bounce。
# クリックは add_ball / clear_balls / オクターブ -1 / オクターブ +1
# (パッドと同じアクション)。
knobs = []
knob_rotation = nil
knob_sides = nil
knob_gravity = nil
knob_bounce = nil

if encoders[0]
  knob_rotation = EncoderKnob.new(
    enc: encoders[0], knob_index: KNOB_ROTATION, min: -60.0, max: 60.0, integer: false
  )
  knob_rotation.on_click { t.add_ball }
  knobs << knob_rotation
end

if encoders[1]
  knob_sides = EncoderKnob.new(
    enc: encoders[1], knob_index: KNOB_SIDES, min: 3, max: 16, integer: true
  )
  knob_sides.on_click { t.clear_balls }
  knobs << knob_sides
end

if encoders[2]
  knob_gravity = EncoderKnob.new(
    enc: encoders[2], knob_index: KNOB_GRAVITY, min: 0.0, max: 2.0, integer: false
  )
  knob_gravity.on_click { shift_octave(t, -1) }
  knobs << knob_gravity
end

if encoders[3]
  knob_bounce = EncoderKnob.new(
    enc: encoders[3], knob_index: KNOB_BOUNCE, min: 0.0, max: 1.2, integer: false
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

# 画面 (タッチ) でノブを動かした分もエンコーダのカウンタへ書き戻す。そうしな
# いと LED リングとノブの位置がパラメータからずれ、次にノブを回した瞬間に
# 値が飛ぶ。
encoder_by_knob = {
  KNOB_ROTATION => knob_rotation,
  KNOB_SIDES    => knob_sides,
  KNOB_GRAVITY  => knob_gravity,
  KNOB_BOUNCE   => knob_bounce
}

UI.on(:knob_change) do |e|
  next unless e[:final]
  enc_knob = encoder_by_knob[e[:index]]
  enc_knob.sync if enc_knob
end

# ---- メインループ ----------------------------------------------------
# The tombola keeps stepping on its own; this loop only delivers UI events
# (pad presses, knob touches and, because on_hit is registered, the hits) and
# reads the encoders.
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
