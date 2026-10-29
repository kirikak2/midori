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
#   - 1〜16 のスロット: タップで切り替え（再生中は予約）、長押しでメニュー（再生中のまま編集 / Copy / Paste /
#     Clear / Length）。Chain の長押しでチェーンの編集。ヘッダの和音をタップで試聴
#
#   - クロックは Tempo から（UI::Sequencer.new の既定）。Play で Tempo を Start する
#   - パターン 0 = 基本のビート、パターン 1 = 2 小節目の終わりにフィル。どちらも 2 小節（Bass が 32 ステップ）。
#     チェーンは 0 × 3 → 1（2 小節 × 3 回のあとにフィル）
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

# --- パターン 1: 0 をコピーして、2 小節目の終わりだけスネアをフィルに ---
# パターンの長さはいちばん長いライン（Bass の 32 ステップ = 2 小節）。スネアも 32 ステップにして
# 2 小節分を書く（16 ステップのままだと、フィルが 2 小節の両方で鳴る）
seq.pattern(0).copy_to(1)
seq.pattern(1).line(1, steps: 32).pattern("....x.......x...|....x.....x.xxxx", notes: [38])

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

# --- on_pattern: パターンが切り替わるたびに Bass と Keys のフレーズをランダムに作り直す ---
# 書き換えるのは鳴り終わったほうのパターン（e[:from]）。鳴っているパターンのノートを差し替えると
# 発音中のノートを消してしまうことがあるので、次にそのパターンが回ってきたときに新しいフレーズで鳴らす。
# Random パッドを ON にしている間だけ書き換える（OFF ならそのときのフレーズのまま）。
BASS_NOTES  = [36, 36, 39, 41, 43, 46, 48]   # C マイナー・ペンタ（ルートを多めに）
KEYS_CHORDS = [[60, 63, 67], [58, 62, 65], [56, 60, 63], [55, 58, 62], [53, 56, 60], [55, 58, 63]]
KEYS_RHYTHMS = [   # [ステップ, 長さ]（1 小節 = 16 ステップ）
  [[0, 6], [8, 6]],
  [[0, 3], [4, 3], [8, 6]],
  [[0, 6], [6, 2], [8, 6]],
  [[0, 14]],
  [[2, 2], [6, 2], [10, 2], [14, 2]]
]

def pick(list)
  list[rand(list.size)]
end

# 8 分音符の位置ごとに 2/3 の確率で置く。小節の頭（0 と 16）は必ずルート
def randomize_bass(line)
  steps = line.steps
  line.clear
  free = 0
  (steps / 2).times do |i|
    s = i * 2
    next if s < free
    head = s % 16 == 0
    next unless head || rand(3) > 0
    len = pick([1, 2, 2, 3])
    len = steps - s if s + len > steps
    line.set(s, notes: [head ? 36 : pick(BASS_NOTES)], length: len)
    free = s + len
  end
end

def randomize_keys(line)
  line.clear
  pick(KEYS_RHYTHMS).each do |s, len|
    line.set(s, notes: pick(KEYS_CHORDS), length: len) if s < line.steps
  end
end

$randomize = false
UI.pad(7, label: "Random", color: :yellow, type: :toggle) { |on| $randomize = on }

seq.on_pattern do |e|
  from = e[:from]
  next if !$randomize || from.nil? || from < 0 || from == e[:pattern]
  pat = seq.pattern(from)
  randomize_bass(pat.line(3))
  randomize_keys(pat.line(4))
  UI.log("pattern #{from}: new Bass / Keys")
end

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
