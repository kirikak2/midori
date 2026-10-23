# Tombola + Knobs + Pads on one screen (UI.layout)
#
# examples/tombola.rb と同じ演奏を、Tombola 画面・Knobs 画面・Pads 画面を
# 行き来せずに 1 画面で操作する版。レイアウト DSL（docs/UI_LAYOUT_DSL.md）で
# 並べている:
#
#   ┌────────────┬──────────┬──────────┐
#   │            │ Rotation │  Sides   │
#   │  Tombola   ├──────────┼──────────┤
#   │  (正方形)  │ Gravity  │  Bounce  │
#   │            ├────┬─────┼────┬─────┤
#   │            │Bal+│Clear│Oct-│Oct+ │
#   └────────────┴────┴─────┴────┴─────┘
#
# 左の列は Tombola がちょうど正方形になる幅に固定し（UI.px）、右の 2 列で残りを
# 分ける。パッドの行は右 2 列ぶんをさらに 4 列のグリッドに割っている。
# CoreS3 は狭いので Tombola を小さめにして、ノブとパッドが最小サイズに収まるようにした。
#
# 発音・パラメータ・パッドの動作・エンコーダの割り当ては tombola.rb と同じ:
#
#   | #     | 回すと     | クリックすると |
#   |-------|------------|----------------|
#   | 1台目 | rotation   | add_ball       |
#   | 2台目 | sides      | clear_balls    |
#   | 3台目 | gravity    | オクターブ -1  |
#   | 4台目 | bounce     | オクターブ +1  |
#
# DFRobot Visual Rotary Encoder（SEN0502）がプライマリ I2C に無ければ 0 台で動く。
# 出力は MIDI Devices 画面で選んだ OUT（未選択なら USB-MIDI デバイス、USB-C）。
#
# Tombola 画面と違ってナビバー中央に Start / Stop は無い（スクリプトで start 済み）。

require 'midi'
require 'ui'
require 'gpio'
require 'i2c'
require 'machine'
require 'dfrobot_rotary_encoder'

# ---- 設定 ------------------------------------------------------------
GAIN = 51                 # 1 ディテント = 51 カウント -> 1 回転でフルレンジ
V_MAX = (1023 / GAIN) * GAIN
BUTTON_LOCKOUT_MS = 300
ENCODER_POLL_MS = 20

# ---- エンコーダ 1 台 = ノブ 1 個 ---------------------------------------
# tombola.rb の EncoderKnob と同じ。違いはノブを番号（UI.knob_value /
# UI.knob_set）ではなく、レイアウトのノブのハンドルで読み書きすること。
class EncoderKnob
  def initialize(enc:, knob:, min:, max:, integer:)
    @enc = enc
    @knob = knob          # UI::Layout::Knob
    @min = min
    @max = max
    @integer = integer
    @last = nil
    @on_click = nil
    @armed = true
    @lock_until = 0
  end

  def knob
    @knob
  end

  def on_click(&block)
    @on_click = block
  end

  # ノブの現在値をエンコーダのカウンタ（と LED リング）へ書き戻す
  def sync
    span = (@max - @min).to_f
    v = 0
    if span > 0.0
      ratio = (@knob.value - @min).to_f / span
      ratio = 0.0 if ratio < 0.0
      ratio = 1.0 if ratio > 1.0
      v = (ratio * V_MAX.to_f).to_i
    end
    @enc.value = v
    @last = v
  end

  # 毎ループ呼ぶ。クリック -> on_click、回転 -> ノブの値（ブロックも呼ばれる）
  def poll
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
    value = (value + 0.5).to_i if @integer
    @knob.value = value
  end
end

# ---- オクターブシフト（tombola.rb と同じ） -------------------------------
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
  UI.log("no encoder found on 0x54-0x57 (touch only)")
else
  UI.log("#{encoders.size} encoder(s) found")
end

# ---- 出力先 ----------------------------------------------------------
# MIDI Devices 画面で選んだ OUT へ送る。未選択なら USB-MIDI デバイス (USB-C)。
# エンコーダの有無に関係なく使える（プライマリ I2C とは競合しない）。
md = UI::MIDIDevices
md.midi_out ||= MIDIDevices.usb_midi_device

unless md.midi_out
  puts "No MIDI output available"
  return
end

md.midi_out.program_change(0, channel: 9)  # Standard drum kit

# ---- Tombola（tombola.rb と同じ設定） ------------------------------------
t = UI::Tombola.new(
  sides: 6,
  rotation: 12,
  gravity: 0.5,
  gravity_mode: :down,
  bounce: 0.8,
  spin_transfer: 0.3,
  channel: 9,
  duration: 120,
  velocity_range: 40..127,
  device: md.midi_out
)

t.scale = [36, 38, 42, 45, 46, 49]
t.sound = false

t.add_ball(color: :red)
t.add_ball(color: :cyan)
t.add_ball(color: :yellow, velocity_scale: 0.7)

t.on_hit do |hit|
  md.midi_out&.trigger(hit[:note], hit[:velocity], duration: t.duration)
end

# OUT を選び直したら Tombola の送信先も差し替える
md.on_change do |e|
  t.device = md.midi_out if e[:kind] == :select_out
end

t.start

# ---- レイアウト ------------------------------------------------------
# 左の列の幅 = Tombola の一辺。大きい画面ではコンテンツの高さいっぱいの正方形、
# CoreS3（320x180）では 2/3 にして、右側のノブとパッドに幅を回す。
d = UI.display
content_h = d[:content][3]
gap = d[:gap]
tombola_size = d[:large] ? content_h - gap * 2 : content_h * 2 / 3

layout = UI.layout("Tombola", cols: [UI.px(tombola_size), 1, 1], rows: [3, 3, 2]) do
  areas <<~GRID
    tb k1 k2
    tb k3 k4
    tb p  p
  GRID

  # 作成済みの Tombola をそのまま置く（tombola: を渡さないと Tombola.new で
  # 設定がリセットされる）
  tombola area: :tb, tombola: t

  # クランプ範囲は Tombola 側より狭い、演奏に使いやすい幅（tombola.rb と同じ）
  knob area: :k1, id: :rotation, label: "Rotation", color: :cyan,
       min: -60, max: 60, step: 0, value: t.rotation do |v|
    t.rotation = v
  end
  knob area: :k2, id: :sides, label: "Sides", color: :green,
       min: 3, max: 16, step: 1, value: t.sides do |v|
    t.sides = v.to_i
  end
  knob area: :k3, id: :gravity, label: "Gravity", color: :magenta,
       min: 0, max: 2, step: 0, value: t.gravity do |v|
    t.gravity = v
  end
  knob area: :k4, id: :bounce, label: "Bounce", color: :yellow,
       min: 0, max: 1.2, step: 0, value: t.bounce do |v|
    t.bounce = v
  end

  grid area: :p, cols: 4 do
    pad col: 0, label: "Ball+", color: :orange do t.add_ball end
    pad col: 1, label: "Clear", color: :red do t.clear_balls end
    pad col: 2, label: "Oct -", color: :blue do shift_octave(t, -1) end
    pad col: 3, label: "Oct +", color: :blue do shift_octave(t, 1) end
  end
end

layout.show

# ---- エンコーダ割り当て ----------------------------------------------
# 1台目 rotation / 2台目 sides / 3台目 gravity / 4台目 bounce。
# クリックはパッドと同じアクション。
assignments = [
  [:rotation, -60.0, 60.0, false, Proc.new { t.add_ball }],
  [:sides,      3,   16,   true,  Proc.new { t.clear_balls }],
  [:gravity,  0.0,  2.0,   false, Proc.new { shift_octave(t, -1) }],
  [:bounce,   0.0,  1.2,   false, Proc.new { shift_octave(t, 1) }]
]

enc_knobs = []
i = 0
while i < encoders.size && i < assignments.size
  id, min, max, integer, click = assignments[i]
  ek = EncoderKnob.new(enc: encoders[i], knob: layout[id], min: min, max: max, integer: integer)
  ek.on_click(&click)
  ek.sync               # LED リングを現在値に合わせてから回し始める
  enc_knobs << ek
  i += 1
end

# 画面（タッチ）でノブを動かした分もエンコーダへ書き戻す。指を離したとき
# （final）だけにして、ドラッグ中に I2C を叩き続けないようにする。
UI.on(:widget) do |e|
  next unless e[:kind] == :knob && e[:final] && e[:layout] == layout.slot
  enc_knobs.each { |ek| ek.sync if ek.knob.id == e[:id] }
end

# ---- メインループ ----------------------------------------------------
# Tombola は C++ 側で動き続ける。このループは UI イベント（パッド・ノブ・
# on_hit の衝突）を配り、エンコーダを読むだけ。
sm = ScriptManager.new
last_poll = 0
until sm.stop_requested?
  UI.process

  now = Machine.uptime_us / 1000
  if now - last_poll >= ENCODER_POLL_MS
    last_poll = now
    enc_knobs.each { |ek| ek.poll }
  end

  MIDI.sleep_ms(5)
end

t.stop
