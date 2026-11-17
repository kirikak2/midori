# timer.rb
#
# UI::Timer（docs/TIMER.md）: Play でカウントダウンを始め、0 秒で MIDI Stop を送る。
#
#   ┌──────────────────────┬──────┐
#   │  Timer（00:30）       │Tempo │  ← Play（Timer でも Tempo でも）で開始
#   │  -1m -10s +10s +1m    │      │     1 秒ごとにクリック、最後の 5 秒は高い音
#   │  Play  Reset  Armed   ├──────┤     0 秒で Stop（Tempo のルートの先へ FC）
#   │                       │ 1:00 │  ← パッドで時間を選ぶ（カウント中は次の回から）
#   └──────────────────────┴──────┘
#
# クロックと Start / Stop は、音を鳴らす機器と同じ所へ送る（MIDI Devices 画面の
# Tempo の行で張り替えられる）。

require 'midi'
require 'ui'

dest = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless dest
  puts "No MIDI output available"
  return
end
dev = MIDI::Device.new(dest)
MIDI.route(UI::Tempo, dest, channel: :clock)

UI.layout("Timer", cols: 4, rows: 3) do
  timer col: 0..2, row: 0..2, seconds: 30 do |sec|
    next if sec == 0
    dev.trigger(sec <= 5 ? 84 : 76, 90, duration: 40, channel: 9)
  end
  tempo col: 3, row: 0
  pad col: 3, row: 1, label: "1:00" do UI::Timer.seconds = 60 end
  pad col: 3, row: 2, label: "3:00" do UI::Timer.seconds = 180 end
end.show

UI::Timer.on_start { |sec| UI.log("Timer: #{sec} s") }
UI::Timer.on_finish do
  UI.log("Timer: time up")
  dev.trigger(96, 120, duration: 300, channel: 9)
end
UI::Timer.on_stop { |sec| UI.log("Timer: stopped with #{sec} s left") }

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)
end
