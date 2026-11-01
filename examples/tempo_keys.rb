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
# 鍵盤はスクリプトの出力（MIDI Devices 画面の Script。何もパッチされていなければ
# AMY / SAM2695 / PC のうち最初に見つかったものへパッチする）を鳴らす。Script の
# パッチを画面で張り替えると鍵盤の行き先も移る。テンポは MIDI クロックとして
# 同じ機器へ送る（docs/TEMPO.md。画面で Tempo の行を張り替えられる）。

require 'midi'
require 'ui'

md = UI::MIDIDevices

dest = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless dest
  puts "No MIDI output available"
  return
end

# Script から何もパッチされていなければここでつなぐ（画面で張り替えられる）
md.midi_out ||= dest
# クロックも同じ機器へ（Script へのルートは Script の入力になるので、機器へ直接）
MIDI.route(UI::Tempo, dest, channel: :clock)

screen = UI.layout("Tempo + Keys", cols: 4, rows: 2) do
  tempo col: 0..2, row: 0
  knob id: :bpm, col: 3, row: 0, label: "BPM", color: :cyan,
       min: 40, max: 240, step: 1, value: UI::Tempo.bpm do |v|
    UI::Tempo.bpm = v
  end
  keys col: 0..3, row: 1, id: :keys, octaves: 2, low: 48, device: md.output
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
