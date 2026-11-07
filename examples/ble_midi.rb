# ble_midi.rb
#
# BLE-MIDI（docs/BLE_MIDI.md）。ESP32-S3 のボード（CoreS3 / Freenove）のみ。
#
#   - BLE-MIDI Device: ボードが BLE-MIDI 機器として広告している（Settings の Bluetooth で On / Off）。
#     Mac の「Audio MIDI 設定」→ Bluetooth 設定、iPad の GarageBand / AUM の Bluetooth MIDI
#     デバイスからつなぐ。つないだ相手（DAW）から来たノートを SAM2695 で鳴らし、
#     ボードの画面の鍵盤 / パッドで弾いた音を DAW へ送る
#   - BLE-MIDI Host: 近くの BLE-MIDI 機器（鍵盤、WIDI などのアダプタ）を探してつなぎ、
#     その演奏も SAM2695 へ。一度つないだ機器は覚えていて、次からは自動でつながる
#
# ルートはスクリプト限り（終わると消える）。ずっと使うなら MIDI Devices 画面でパッチする。

require 'midi'

ble = MIDIDevices.ble_midi_device
unless ble
  puts "No BLE-MIDI on this board"
  return
end
synth = MIDIDevices.sam2695

puts "Advertising as \"#{BLE_MIDI_DEVICE.name}\" (#{BLE_MIDI_DEVICE.state})"

# DAW -> SAM2695
MIDI.route(ble, synth) if synth

# BLE-MIDI の鍵盤を探す（見つかった最初の 1 台。名前で選ぶなら connect("WIDI")）
found = BLE_MIDI_HOST.scan(timeout_ms: 4000)
found.each { |p| puts "  #{p[:name]}  #{p[:address]}  #{p[:rssi]} dBm#{p[:known] ? '  (known)' : ''}" }
kb = found.empty? ? nil : BLE_MIDI_HOST.connect(address: found.first[:address])
if kb
  puts "Connected: #{kb.name}"
  MIDI.route(kb, synth) if synth   # 鍵盤 -> SAM2695
  MIDI.route(kb, ble)              # 鍵盤 -> DAW（ボードを BLE-MIDI のハブにする）
end

# DAW から来たメッセージを表示しつつ、10 秒ごとに状態を出す
MIDI.on(ble, :note_on) { |m| puts "from DAW: note #{m.note} vel #{m.velocity}" }
sm = ScriptManager.new
t = 0
until sm.stop_requested?
  MIDI.sleep_ms(100)
  t += 100
  if t % 10000 == 0
    i = BLE_MIDI_DEVICE.info
    puts "device #{i[:state]} peer=#{i[:peer]} mtu=#{i[:mtu]} rx=#{i[:rx_messages]} tx=#{i[:tx_messages]}"
    BLE_MIDI_HOST.devices.each { |d| puts "host #{d.name} #{d.status}" }
  end
end
