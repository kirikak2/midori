# dfrobot_encoder_test.rb
# DFRobot Visual Rotary Encoder (SEN0502) の動作確認。
#
# プライマリ I2C (BoardConfig::PRIMARY_I2C_SDA_PIN / PRIMARY_I2C_SCL_PIN) にモジュールを接続し、
# 値・ボタン・基本情報を LOGS 画面に出す。
#   - "PID=0x01f6" が出る      -> 通信 OK
#   - "NOT connected" が出る   -> アドレス(DIPスイッチ)/配線/電源を確認
#                                 examples/i2c_scan.rb でアドレスを確認する
#
# ※ プライマリ I2C を占有するため SAM2695 (UART, 同じピン) とは同時使用不可。

require 'ui'
require 'gpio'
require 'i2c'
require 'dfrobot_rotary_encoder'

unless BoardConfig::HAS_PRIMARY_I2C
  UI.log("primary I2C not available on this board")
  exit
end

SDA  = BoardConfig::PRIMARY_I2C_SDA_PIN
SCL  = BoardConfig::PRIMARY_I2C_SCL_PIN
ADDR = DFRobotRotaryEncoder::DEFAULT_ADDRESS   # 0x54 (DIP スイッチに合わせる)

# 直前まで SAM2695(UART) がこのピンを使っていた場合のルーティング残りを剥がす
GPIO.new(SDA, GPIO::IN)
GPIO.new(SCL, GPIO::IN)

i2c = I2C.new(unit: BoardConfig::PRIMARY_I2C_UNIT, sda_pin: SDA, scl_pin: SCL, frequency: 100_000)
enc = DFRobotRotaryEncoder.new(i2c: i2c, address: ADDR)

info = enc.basic_info
if info
  UI.log(sprintf("PID=0x%04x VID=0x%04x", info[:pid], info[:vid]))
  UI.log(sprintf("ver=0x%04x addr=0x%02x", info[:version], info[:address]))
else
  UI.log(sprintf("NOT connected (0x%02x)", ADDR))
end

# 1 ステップあたり +20 カウント (LED リングの進み方が分かりやすい)
enc.gain = 20
enc.value = 0
g = enc.gain
UI.log("gain=#{g.nil? ? 'nil' : g} -> turn the knob")

prev = nil
loop do
  v = enc.value
  UI.log("pressed") if enc.button_down?
  if v != prev
    UI.log("value=#{v.nil? ? 'nil' : v}")
    prev = v
  end
  sleep_ms 50
end
