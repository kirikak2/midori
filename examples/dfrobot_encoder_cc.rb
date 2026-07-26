# dfrobot_encoder_cc.rb
# DFRobot Visual Rotary Encoder (SEN0502) を MIDI Control Change コントローラにする。
# encoder8_cc.rb の DFRobot 版。
#
# Tab5 の Port A (SDA=53 / SCL=54) にモジュールを接続し、ノブの絶対値 (0-1023) を
# CC 値 (0-127) にして USB-MIDI Device (USB-C) からホストへ送る。
#
#   - ノブを回すと CC #(BASE_CC + i) が増減 (LED リングが現在値を表示)
#   - ノブのボタンを押すと その CC を中央 (64) にリセット
#     (MODE = :relative のときはカウンタを中央に戻すだけで CC は送らない)
#   - UI パッド (momentary): 押している間だけ note_on、離すと note_off
#
# 8Encoder との違い:
#   - 1 モジュール = 1 ノブ。基板の DIP スイッチで 0x54..0x57 を選び、最大 4 台まで
#     同じバスにカスケードできる (起動時に自動検出して CC を順に割り当てる)
#   - カウンタは「差分」ではなく 0-1023 の絶対値。回し切ると端で止まる
#   - LED はモジュールが値から自動で点灯させるので、こちらから色を書く必要はない
#
# ※ Port A を I2C に占有するため SAM2695 (UART 53/54) とは同時使用不可。
#
# ---- Ableton Live で使うときの注意 -----------------------------------
# SEN0502 は 20 PPR (1 回転 = 20 ディテント = LED 20 個) で、1 ディテントあたり
# カウンタが GAIN だけ増える (フルスケール 1023)。つまり:
#
#   GAIN | 1ディテント | CC変化/ディテント | フルスイープ
#   -----+------------+------------------+------------------------
#     51 |  51 カウント|  6.3             |  20 ディテント = 1.0 回転
#     25 |  25        |  3.1             |  41         = 2.0 回転
#     16 |  16        |  2.0             |  64         = 3.2 回転
#      8 |   8        |  1.0             | 128         = 6.4 回転
#
# GAIN が小さいほど細かく刻めるが、端まで回すのに何回転も必要になる。Live で
# 「ノブを回してもパラメータがほとんど動かない」ときは、まずここを大きくする。
# 既定は GAIN=51 (1 回転でフルレンジ)。
#
# もう一点、CC 換算は 1023 ではなく V_MAX (下記) で正規化する必要がある。
# カウンタは GAIN 刻みでしか動かないので到達可能な上限は 1023 未満であり、
# 1023 で割ると CC 127 に届かず、端数で「回しても CC が変わらないディテント」が
# 生じる。Live の MIDI マップモードでノブを 1 クリックしても何も送信されず、
# マッピングを学習できない原因になる。
#
# MODE = :absolute で使う場合、Live 側の設定を確認すること:
#   - Preferences > Link/Tempo/MIDI > Takeover Mode を "None" にする
#     ("Pickup" だとノブがパラメータの現在値を通過するまで反応しない)
#   - MIDI マッピングブラウザの Mode 列は "Absolute"
#
# ジャンプが気になる場合は MODE = :relative にして、Live 側の Mode 列を
# "Relative (2's Comp.)" にする。パラメータの現在値から相対的に動くので
# Takeover Mode の影響を受けない。

require 'midi'
require 'ui'
require 'gpio'
require 'i2c'
require 'dfrobot_rotary_encoder'

# ---- 設定 ------------------------------------------------------------
CH          = 0           # MIDI チャンネル (0-15)
BASE_CC     = 16          # 1台目 -> CC16, 2台目 -> CC17, ...
SDA         = 53
SCL         = 54
NOTE_VELOCITY = 100       # パッドの note_on ベロシティ
ADDRS       = DFRobotRotaryEncoder::ADDRESSES   # [0x54, 0x55, 0x56, 0x57]

# :absolute … ノブの絶対位置をそのまま CC 0-127 で送る (LED リング = CC 値)
# :relative … 動いた分だけ 2の補数の相対値で送る (Live: Relative (2's Comp.))
MODE        = :absolute

# 増分係数 (1-51) = 1 ディテントあたりのカウンタ増分。冒頭の表を参照。
# absolute: 51 なら 1 回転でフルレンジ、1 ディテント = CC 約 6.3
# relative: 8 にして余裕を持たせる (中央から ±64 ディテント回せる)
GAIN_ABSOLUTE = 51
GAIN_RELATIVE = 8
GAIN = (MODE == :relative) ? GAIN_RELATIVE : GAIN_ABSOLUTE

# relative 時の 1 ディテントあたりの CC 変化量。4 なら 32 ディテント (1.6 回転) で
# パラメータを端から端まで動かせる。
REL_STEP    = 4

# relative 時、カウンタが端に近づいたら中央へ戻す (無限回転させるため)。
REL_CENTER  = 512
REL_MARGIN  = GAIN_RELATIVE * 4

# ディテントを刻んで到達できる最大カウンタ値。カウンタは 0 から GAIN 刻みでしか
# 動かないので、上限は 1023 ではなく GAIN の倍数に切り下げた値になる
# (GAIN=51 なら 20*51=1020)。CC 換算をこの値で正規化しないと
#   - CC 127 に到達できない (1020*127/1023 = 126)
#   - 端数のせいで「回しても CC が変わらないディテント」が出る
# という 2 つの問題が起きる。
V_MAX = (1023 / GAIN) * GAIN

# ---- 出力先 (USB-MIDI Device / USB-C) --------------------------------
usb = MIDIDevices.usb_midi_device
unless usb
  UI.log("USB-MIDI Device not available on this board")
  exit
end
device = MIDI::Device.new(usb)

# ---- 値の変換 --------------------------------------------------------
# エンコーダ値 (0-V_MAX) -> CC (0-127)
def to_cc(v)
  cc = v * 127 / V_MAX
  cc > 127 ? 127 : cc
end

# CC (0-127) -> エンコーダ値。その CC になる最小の値 (切り上げ) を返すので
# to_cc(to_value(cc)) == cc が 0-127 の全域で成り立つ。
def to_value(cc)
  (cc * V_MAX + 126) / 127
end

# 相対値 (ディテント差) -> 2の補数の CC データバイト。
# +1..+63 -> 0x01..0x3F / -1..-63 -> 0x7F..0x41 (Live の "Relative (2's Comp.)")
def to_relative(d)
  d = 63 if d > 63
  d = -63 if d < -63
  d < 0 ? (128 + d) : d
end

# ---- エンコーダ検出 (Port A: SDA=53, SCL=54) -------------------------
# 直前まで SAM2695(UART) が 53/54 を使っていた場合のルーティング残りを剥がす
GPIO.new(SDA, GPIO::IN)
GPIO.new(SCL, GPIO::IN)

i2c = I2C.new(unit: "ESP32_I2C0", sda_pin: SDA, scl_pin: SCL, frequency: 100_000)

encs = []
a = 0
while a < ADDRS.size
  e = DFRobotRotaryEncoder.new(i2c: i2c, address: ADDRS[a])
  if e.connected?
    e.gain = GAIN
    encs << e
    UI.log(sprintf("0x%02x -> CC%d", ADDRS[a], BASE_CC + encs.size - 1))
  end
  a += 1
end

N = encs.size
if N == 0
  UI.log("no encoder found on 0x54-0x57")
  UI.log("-> check DIP switch / wiring (i2c_scan.rb)")
else
  UI.log("#{N} enc  #{MODE}  ch#{CH + 1}  -> USB-MIDI")
end

# ---- 状態 ------------------------------------------------------------
value = Array.new(N, 0)   # absolute: 各 CC の現在値 (0-127)
raw   = Array.new(N, 0)   # relative: 前回読んだカウンタ値

j = 0
while j < N
  if MODE == :relative
    # 中央から始めれば左右どちらにも回せる。相対モードでは起動時に CC を送らない
    # (受け取った側が「その分だけ動かせ」と解釈してしまうため)。
    encs[j].value = REL_CENTER
    raw[j] = REL_CENTER
  else
    # 起動時: 現在の CC 値をホストへ送って状態を同期
    v = encs[j].value
    value[j] = v ? to_cc(v) : 0
    device.control_change(BASE_CC + j, value[j], channel: CH)
  end
  j += 1
end

# ---- ノートパッド (momentary): 押下で note_on / 離しで note_off -------
UI.pad(1, label: "C4", color: :red, type: :momentary) do |on|
  on ? device.note_on(60, NOTE_VELOCITY, channel: CH) : device.note_off(60, channel: CH)
end
UI.pad(2, label: "D4", color: :orange, type: :momentary) do |on|
  on ? device.note_on(62, NOTE_VELOCITY, channel: CH) : device.note_off(62, channel: CH)
end
UI.pad(3, label: "E4", color: :yellow, type: :momentary) do |on|
  on ? device.note_on(64, NOTE_VELOCITY, channel: CH) : device.note_off(64, channel: CH)
end
UI.pad(4, label: "G4", color: :green, type: :momentary) do |on|
  on ? device.note_on(67, NOTE_VELOCITY, channel: CH) : device.note_off(67, channel: CH)
end
UI.pad(5, label: "A4", color: :cyan, type: :momentary) do |on|
  on ? device.note_on(69, NOTE_VELOCITY, channel: CH) : device.note_off(69, channel: CH)
end
UI.pad(6, label: "C5", color: :blue, type: :momentary) do |on|
  on ? device.note_on(72, NOTE_VELOCITY, channel: CH) : device.note_off(72, channel: CH)
end
UI.pad(7, label: "D5", color: :purple, type: :momentary) do |on|
  on ? device.note_on(74, NOTE_VELOCITY, channel: CH) : device.note_off(74, channel: CH)
end
UI.pad(8, label: "E5", color: :white, type: :momentary) do |on|
  on ? device.note_on(76, NOTE_VELOCITY, channel: CH) : device.note_off(76, channel: CH)
end

# ---- メインループ ----------------------------------------------------
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  i = 0
  while i < N
    enc = encs[i]

    # --- ボタン ---
    # (フラグはモジュール側でラッチされるので、こちらでエッジ検出は不要)
    if enc.button_down?
      if MODE == :relative
        # 相対モードに「絶対値」はないので、カウンタを中央へ戻すだけ。
        # ここで CC 64 を送ると受け手は +64 と解釈してしまう。
        enc.value = REL_CENTER
        raw[i] = REL_CENTER
        UI.log("enc#{i}: re-centered")
      else
        enc.value = to_value(64)
        value[i] = 64
        device.control_change(BASE_CC + i, 64, channel: CH)
        UI.log("enc#{i}: reset CC#{BASE_CC + i}=64")
      end
    else
      v = enc.value   # 0-1023 or nil (I2C 失敗)
      if v
        if MODE == :relative
          # --- 回転量: カウンタ差分 -> ディテント数 -> 2の補数の相対 CC ---
          d = (v - raw[i]) / GAIN
          raw[i] = v
          device.control_change(BASE_CC + i, to_relative(d * REL_STEP), channel: CH) if d != 0
          # 端に達すると回せなくなるので、近づいたら中央へ戻す
          if v < REL_MARGIN || v > 1023 - REL_MARGIN
            enc.value = REL_CENTER
            raw[i] = REL_CENTER
          end
        else
          # --- 回転量: 絶対値 -> CC (変化時のみ送信) ---
          cc = to_cc(v)
          if cc != value[i]
            value[i] = cc
            device.control_change(BASE_CC + i, cc, channel: CH)
          end
        end
      end
    end

    i += 1
  end
end
