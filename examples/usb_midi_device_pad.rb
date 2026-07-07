# usb_midi_device_pad.rb
# picoruby-usb_midi_device の動作確認サンプル (M5Stack Tab5 専用)
#
# Tab5 は USB-C 経由で PC から "M5Stack Tab5 MIDI" (303a:4009) という
# USB-MIDI デバイスとして見える。UI パッドのタップを trigger
# (note_on + 自動 note_off) として接続先ホストの MIDI IN に送る。
#
# PC 側での確認方法:
#   Linux : aseqdump -p "Midori MIDI"
#   Windows: DAW の MIDI 入力に "Midori MIDI" を選択

require 'midi'
require 'ui'

usb_dev = MIDIDevices.usb_midi_device
unless usb_dev
  UI.log("USB-MIDI Device not available on this board")
  exit
end

device = MIDI::Device.new(usb_dev)

UI.log("USB-MIDI Device pad started")
if usb_dev.connected?
  UI.log("Host connected")
else
  UI.log("Host not connected yet")
end

# C メジャースケール (C4-E5)
UI.pad(1, label: "C4", color: :red, type: :trigger) do
  device.trigger(60, 110, duration: 150)
end

UI.pad(2, label: "D4", color: :orange, type: :trigger) do
  device.trigger(62, 110, duration: 150)
end

UI.pad(3, label: "E4", color: :yellow, type: :trigger) do
  device.trigger(64, 110, duration: 150)
end

UI.pad(4, label: "G4", color: :green, type: :trigger) do
  device.trigger(67, 110, duration: 150)
end

UI.pad(5, label: "A4", color: :cyan, type: :trigger) do
  device.trigger(69, 110, duration: 150)
end

UI.pad(6, label: "C5", color: :blue, type: :trigger) do
  device.trigger(72, 110, duration: 150)
end

UI.pad(7, label: "D5", color: :purple, type: :trigger) do
  device.trigger(74, 110, duration: 150)
end

UI.pad(8, label: "E5", color: :white, type: :trigger) do
  device.trigger(76, 110, duration: 150)
end

# メインループ: UI イベント処理のみ (クロックは送らない)
# trigger の note_off は C 側タイマーが自動送信する
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # empty
end
