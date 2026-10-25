# sequencer.rb
#
# UI::Sequencer（docs/SEQUENCER.md）をスクリプトから組んで鳴らす。
# 組み終わったら Sequencer 画面を開く（Home の Sequencer タイルからも開ける）。
# 再生 / 停止は Sequencer 画面の Play / Stop（Tempo 画面の Play、Pads 画面の Play パッドでも同じ）。
#
# 画面での編集:
#   - 空のマスをタップ → 最後に弾いた和音（ヘッダの黄色い表示）を置く。ドラッグで長さ
#   - ノートの先頭をタップ → 消す / ドラッグ → 長さを変える / 長押し → 和音に差し替え
#   - ライン名を長押し → 設定（チャンネル・steps・rate・gate・Clear・Delete）
#   - 1〜16 のスロット: タップで切り替え（再生中は予約）、長押しで「再生中のまま編集」
#
#   - クロックは Tempo から（UI::Sequencer.new の既定）。Play で Tempo を Start する
#   - パターン 0 = 基本のビート、パターン 1 = フィル入り。チェーンは 0 × 3 → 1
#   - 選んだ IN（外部の鍵盤など）で弾いた和音を、Chord パッドで Keys ラインに置ける
#
# IN / OUT は MIDI Devices 画面の Prog(input) 列 / Prog(output) 行で選ぶ。

require 'midi'
require 'ui'

md = UI::MIDIDevices
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

seq = UI::Sequencer.new(device: md.midi_out, input: md.midi_in)

# --- パターン 0 ---
seq.line(0, name: "Kick",  channel: 9).pattern("x...x...x...x...", notes: [36])
seq.line(1, name: "Snare", channel: 9).pattern("....x.......x...", notes: [38])
seq.line(2, name: "Hat",   channel: 9, steps: 8, rate: 8).pattern("xxxxxxxx", notes: [42], velocity: 70)

bass = seq.line(3, name: "Bass", channel: 0, steps: 32, gate: 80)
bass.set(0,  notes: [36], length: 3)
bass.set(4,  notes: [36])
bass.set(8,  notes: [39], length: 2)
bass.set(12, notes: [41])
bass.set(16, notes: [36], length: 3)
bass.set(20, notes: [43], length: 2)
bass.set(24, notes: [41], length: 4)

keys = seq.line(4, name: "Keys", channel: 1, steps: 16, gate: 90)
keys.set(0, notes: [60, 63, 67], length: 6)
keys.set(8, notes: [58, 62, 65], length: 6)

# --- パターン 1: 0 をコピーしてスネアをフィルに ---
seq.pattern(0).copy_to(1)
seq.pattern(1).line(1).pattern("....x.....x.xxxx", notes: [38])

seq.chain = [[0, 3], [1, 1]]

UI.pad(1, label: "Play", color: :green, type: :toggle) do |on|
  on ? UI::Tempo.start : UI::Tempo.stop
end
UI.pad(2, label: "Pat A", color: :blue) { seq.play_pattern = 0 }
UI.pad(3, label: "Pat B", color: :blue) { seq.play_pattern = 1 }
UI.pad(4, label: "Chain", color: :purple, type: :toggle) { |on| seq.chain_on = on }
UI.pad(5, label: "Chord", color: :orange) do
  # 最後に弾いた和音を Keys ラインの先頭に置く
  keys[0] = seq.chord
  UI.log("Keys: #{seq.chord.inspect}")
end
UI.pad(6, label: "Mute Hat", color: :gray, type: :toggle) { |on| seq.line(2).mute = on }

seq.show

sm = ScriptManager.new
last = nil
until sm.stop_requested?
  UI.process
  pos = seq.position
  if seq.running? && pos[:pattern] != last
    last = pos[:pattern]
    UI.log("pattern #{last}")
  end
  MIDI.sleep_ms(10)
end
