# Knobs - continuous controls turned with a finger
#
# Each knob is a ring gauge you turn by sweeping a finger around it. Turning
# one calls its block, and the block is what sends MIDI -- exactly like a pad.
# Nothing is sent unless UI.process is running, so keep the loop at the bottom
# tight (5-10ms) or the knobs will feel like they are dragging.
#
# Two banks are set up here:
#
#   A: filter and amp        B: effects and mix
#   1 Cutoff   2 Reso        1 Reverb   2 Chorus
#   3 Attack   4 Release     3 Volume   4 Pan (centred)
#
# Tap the A/B/C/D strip on the right to switch. The nav bar's [Send] pushes
# the visible bank's values out again, for a synth plugged in afterwards.
#
# If DFRobot Visual Rotary Encoders (SEN0502) are wired to the primary I2C bus
# (BoardConfig::PRIMARY_I2C_SDA_PIN / PRIMARY_I2C_SCL_PIN), up to four of them
# (DIP addresses 0x54..0x57) are picked up and take over knobs 1..4 of whichever
# bank is on screen. Nothing breaks without any.
#
# ※ プライマリ I2C は SAM2695 の UART と同じピンなので、エンコーダと SAM2695 は
#   同時に使えない。エンコーダが見つかったら出力を USB-MIDI へ回す。

require 'midi'
require 'ui'
require 'i2c'
require 'machine'
require 'dfrobot_rotary_encoder'

GAIN = 51                 # 1 ディテント = LED 1 個
ENCODER_POLL_MS = 20

# ---- エンコーダ (任意・最大4台) --------------------------------------
# encoders[i] が画面のノブ (i + 1) を担当する。DIP スイッチのアドレス順
# (0x54, 0x55, 0x56, 0x57) に検出し、繋がっていない分は黙って飛ばす。
encoders = []
begin
  if BoardConfig::HAS_PRIMARY_I2C
    i2c = I2C.new(unit: BoardConfig::PRIMARY_I2C_UNIT, sda_pin: BoardConfig::PRIMARY_I2C_SDA_PIN,
                  scl_pin: BoardConfig::PRIMARY_I2C_SCL_PIN, frequency: 100_000)
    addrs = DFRobotRotaryEncoder::ADDRESSES   # [0x54, 0x55, 0x56, 0x57]
    a = 0
    while a < addrs.size
      enc = DFRobotRotaryEncoder.new(i2c: i2c, address: addrs[a])
      if enc.connected?
        enc.gain = GAIN
        encoders << enc
        puts sprintf("Encoder %d found at 0x%02x -> knob %d", encoders.size, addrs[a], encoders.size)
      end
      a += 1
    end
  end
rescue => e
  puts "No encoder: #{e.message}"
end

# ---- 出力先 ----------------------------------------------------------
# エンコーダを使う場合、プライマリ I2C が SAM2695 と競合するので USB を優先する。
transport = if encoders.empty?
              MIDIDevices.sam2695 || MIDIDevices.usb_midi_host
            else
              MIDIDevices.usb_midi_device || MIDIDevices.usb_midi_host || MIDIDevices.sam2695
            end

unless transport
  puts "No MIDI output available"
  return
end

dev = MIDI::Device.new(transport)
CH = 0

# ---- Bank A: filter / amp --------------------------------------------
UI.knob(1, bank: 1, label: "Cutoff", color: :cyan, value: 100) do |v|
  dev.control_change(74, v.to_i, channel: CH)
end

UI.knob(2, bank: 1, label: "Reso", color: :magenta, value: 0) do |v|
  dev.control_change(71, v.to_i, channel: CH)
end

UI.knob(3, bank: 1, label: "Attack", color: :green, value: 64) do |v|
  dev.control_change(73, v.to_i, channel: CH)
end

UI.knob(4, bank: 1, label: "Release", color: :yellow, value: 64) do |v|
  dev.control_change(72, v.to_i, channel: CH)
end

# ---- Bank B: effects / mix -------------------------------------------
UI.knob(1, bank: 2, label: "Reverb", color: :blue, value: 40) do |v|
  dev.control_change(91, v.to_i, channel: CH)
end

UI.knob(2, bank: 2, label: "Chorus", color: :purple, value: 0) do |v|
  dev.control_change(93, v.to_i, channel: CH)
end

UI.knob(3, bank: 2, label: "Volume", color: :white, value: 100) do |v|
  dev.control_change(7, v.to_i, channel: CH)
end

# origin: :center makes the gauge grow either way from 12 o'clock, which is
# what a pan control should look like.
UI.knob(4, bank: 2, label: "Pan", color: :orange, value: 64, origin: :center) do |v|
  dev.control_change(10, v.to_i, channel: CH)
end

UI.knob_bank = 1
UI.knob_send_all(bank: :all)   # 定義しただけでは何も送られないので初期送信
UI.knobs                       # Knobs 画面へ

# ---- エンコーダを画面のノブ 1..N に括りつける -----------------------
# bank: を渡さないメソッドは「見えているバンク」を指すので、バンクを切り替え
# ればエンコーダの担当も一緒に移る。encoders[i] <-> knob (i + 1)。
#
# 0-127 のノブ値を SEN0502 のカウンタ (0-1023) へ。書き込みはモジュール内部の
# レジスタ (REG_COUNT) を更新するので、LED リングも次に指で回したときの起点も
# その場で新しい値になる。
def encoder_write(enc, knob_value)
  return unless enc
  enc.value = (knob_value * 1023 / 127).to_i
end

def sync_encoder(encoders, knob_index)
  encoder_write(encoders[knob_index - 1], UI.knob_value(knob_index))
end

def sync_all_encoders(encoders)
  i = 1
  while i <= encoders.size
    sync_encoder(encoders, i)
    i += 1
  end
end

UI.on(:knob_bank) do |e|
  puts "Bank #{e[:bank]}"
  sync_all_encoders(encoders)
end

# 画面 (タッチ) でノブが動いた分をエンコーダのカウンタへ書き戻す。ドラッグ中の
# 途中経過も含めて追従させたいので e[:final] は待たない。:knob_change はタッチ
# 由来 (と knob_send_all / knob_reset) でしか飛ばず、エンコーダ入力の
# UI.knob_set は notify:false なので発火しない。ループにはならない。
UI.on(:knob_change) do |e|
  next unless e[:bank] == UI.knob_bank        # 見えているバンクだけ
  encoder_write(encoders[e[:index] - 1], e[:value])
end

sync_all_encoders(encoders)

sm = ScriptManager.new
last_encoder_poll = 0

puts "Knobs ready. Turn them, or tap A/B on the right."

loop do
  break if sm.stop_requested?

  UI.process

  unless encoders.empty?
    now = Machine.uptime_us / 1000
    if now - last_encoder_poll >= ENCODER_POLL_MS
      last_encoder_poll = now
      i = 0
      while i < encoders.size
        raw = encoders[i].value
        UI.knob_set(i + 1, raw * 127 / 1023) if raw
        i += 1
      end
    end
  end

  sleep_ms 5
end
