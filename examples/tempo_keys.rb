# tempo_keys.rb
#
# レイアウト DSL（docs/UI_LAYOUT_DSL.md）で Tempo・BPM ノブ・鍵盤を 1 画面に並べる。
#
#   ┌──────────────────────┬──────┐
#   │  Tempo（120 BPM）     │ BPM  │  ← ノブを回すとテンポが変わる
#   │  タップで TAP tempo   │ ノブ │     TAP / Sync で変わったときはノブが追従
#   ├──────────────────────┴──────┤
#   │       Keys（2 オクターブ）     │
#   └─────────────────────────────┘
#
# テンポは MIDI クロックとして PC（USB-MIDI Device）へ送る（docs/TEMPO.md）。
# 鍵盤は AMY / SAM2695 / PC のうち最初に見つかったものを鳴らす。

require 'midi'
require 'ui'

out = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless out
  puts "No MIDI output available"
  return
end

# クロックの送り先（MIDI Devices 画面で Tempo の行をパッチしても同じ）
pc = MIDIDevices.usb_midi_device
MIDI.route(UI::Tempo, pc) if pc

screen = UI.layout("Tempo + Keys", cols: 4, rows: 2) do
  tempo col: 0..2, row: 0
  knob id: :bpm, col: 3, row: 0, label: "BPM", color: :cyan,
       min: 40, max: 240, step: 1, value: UI::Tempo.bpm do |v|
    UI::Tempo.bpm = v
  end
  keys col: 0..3, row: 1, octaves: 2, low: 48, device: out
end

# TAP / Sync / 他のスクリプトでテンポが変わったら、ノブの表示を合わせる
# （notify: false でノブのブロックは呼ばない）
UI::Tempo.on_change { |bpm| screen[:bpm].set_value(bpm, notify: false) }

screen.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)
end
