# sequencer_events.rb
#
# UI::Sequencer のイベント（docs/SEQUENCER.md「状態・イベント」）を使う例。
# Sequencer とパッドを 1 つのレイアウトに並べ、
#
#   - on_step    … Kick ラインのステップで拍のランプ（下段の 1〜4）を点ける。
#                  Bass ライン（パターンと同じ 2 小節）のステップ 0 でパターンの周回を数える
#   - on_pattern … パターンが切り替わったら表示を更新する。Auto が ON なら
#                  「A を 3 周 → B（フィル）を 1 周 → A に戻る」をスクリプトで予約する（チェーンを使わない曲構成）
#   - on_edit    … Sequencer 画面（このレイアウトの Sequencer 部品も同じ）で編集されたら、
#                  何を変えたかを Edit パッドとログに出す
#
# 再生 / 停止は Sequencer 部品の Play / Stop（Tempo 画面の Play でも同じ）。
# イベントのブロックは UI.process の中で走るので、メインループで UI.process を回す。
# on_step は 1 拍に何通も来るため、UI.process が遅れて UI のイベントキューが半分埋まると捨てられる
# （そのときは周回の数え漏れが出る）。ループを短く回し、見るラインも on_step(line:) で絞る。
# OUT は MIDI Devices 画面の Prog(output) 行で選ぶ。

require 'midi'
require 'ui'

md = UI::MIDIDevices
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

seq = UI::Sequencer.new(device: md.midi_out, input: md.midi_in)
unless seq.respond_to?(:on_step)
  puts "This firmware has no UI::Sequencer#on_step: flash a build with docs/SEQUENCER.md stage 6"
  return
end

# --- ソング: パターン 0 = A（基本のビート）、パターン 1 = B（2 小節目の終わりにフィル） ---
KICK = 0
BASS = 3
seq.line(KICK, name: "Kick", channel: 9).pattern("x...x...x...x...", notes: [36])
seq.line(1, name: "Snare", channel: 9).pattern("....x.......x...", notes: [38])
seq.line(2, name: "Hat", channel: 9, steps: 8, rate: 8).pattern("xxxxxxxx", notes: [42], velocity: 70)
bass = seq.line(BASS, name: "Bass", channel: 0, steps: 32, gate: 80)   # 2 小節 = パターンの長さ
bass.set(0, notes: [36], length: 3)
bass.set(8, notes: [39], length: 2)
bass.set(16, notes: [36], length: 3)
bass.set(24, notes: [41], length: 4)

seq.pattern(0).copy_to(1)
seq.pattern(1).line(1, steps: 32).pattern("....x.......x...|....x.....x.xxxx", notes: [38])
seq.play_pattern = 0

PATTERN_NAMES = { 0 => "A", 1 => "B" }
A_LOOPS = 3   # Auto: A をこの回数だけ回してから B へ

def pattern_name(p)
  PATTERN_NAMES[p] || "P#{p + 1}"
end

# --- レイアウト: 上 3 段が Sequencer、下段が拍のランプ・パターン・Auto・Edit ---
$auto = false
panel = UI.layout("Seq Events", cols: 7, rows: 4) do
  areas <<~GRID
    s  s  s  s  s   s    s
    s  s  s  s  s   s    s
    s  s  s  s  s   s    s
    b0 b1 b2 b3 pat auto edit
  GRID
  sequencer area: :s, sequencer: seq
  4.times { |i| pad area: "b#{i}".to_sym, id: "b#{i}".to_sym, label: (i + 1).to_s, color: :gray }
  pad area: :pat, id: :pat, label: "Pat A", color: :blue
  pad(area: :auto, id: :auto, label: "Auto", color: :purple, type: :toggle) do |on|
    $auto = on
    UI.log(on ? "Auto: A x#{A_LOOPS} -> B" : "Auto: off")
  end
  pad area: :edit, id: :edit, label: "Edit -", color: :gray
end

# 拍のランプ: いまの拍だけ点ける（1 拍目は赤、ほかは黄色）。nil で全部消す
$lit = nil
def light_beat(panel, beat)
  return if beat == $lit
  4.times do |i|
    color = if i != beat then :gray
            elsif i == 0 then :red
            else :yellow
            end
    panel["b#{i}".to_sym].color = color
  end
  $lit = beat
end

# --- on_step: Kick（16 ステップ = 1 小節）の 4 ステップごとが 1 拍 ---
seq.on_step(line: KICK) do |e|
  light_beat(panel, e[:step] / 4) if e[:step] % 4 == 0
end

# --- on_step: Bass（パターンと同じ長さ）のステップ 0 = パターンの頭。同じパターンが
# ループしても on_pattern は来ないので、周回はこちらで数える ---
$loops = 0
seq.on_step(line: BASS) do |e|
  next unless e[:step] == 0
  $loops += 1
  panel[:pat].label = "Pat #{pattern_name(e[:pattern])} #{$loops}"
  # Auto: A の最後の周回に入ったら B を予約する（このパターンの終わりで切り替わる）
  seq.play_pattern = 1 if $auto && e[:pattern] == 0 && $loops >= A_LOOPS
end

# --- on_pattern: 切り替わったら周回を数え直し、B に入ったら次の A を予約しておく ---
seq.on_pattern do |e|
  $loops = 0
  panel[:pat].label = "Pat #{pattern_name(e[:pattern])}"
  UI.log("pattern #{pattern_name(e[:from])} -> #{pattern_name(e[:pattern])}") if e[:from]
  seq.play_pattern = 0 if $auto && e[:pattern] == 1   # B は 1 周だけ
end

# --- on_edit: 画面で変えられたもの（line が nil ならパターン全体かソング） ---
seq.on_edit do |e|
  what = e[:line] ? seq.line(e[:line]).name : "all"
  what = "L#{e[:line] + 1}" if what.empty?
  panel[:edit].label = "Ed #{pattern_name(e[:pattern])} #{what}"
  panel[:edit].color = :orange
  UI.log("edited pattern #{pattern_name(e[:pattern])}: #{what}")
end

panel.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  unless seq.running?   # 止まったらランプを消し、周回は次の Play から数え直す
    light_beat(panel, nil)
    $loops = 0
  end
  MIDI.sleep_ms(10)
end
