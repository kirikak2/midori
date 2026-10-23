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
# 鍵盤は MIDI Devices 画面で選んだ OUT（未選択なら AMY / SAM2695 / PC のうち
# 最初に見つかったもの）を鳴らし、テンポも MIDI クロックとして同じ OUT へ送る
# （docs/TEMPO.md）。OUT を選び直すと両方とも移る。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

# クロックの送り先（MIDI Devices 画面で Tempo の行をパッチしても同じ）
clock_to = nil
follow_out = Proc.new do
  MIDI.unroute(UI::Tempo, clock_to) if clock_to
  clock_to = md.midi_out
  MIDI.route(UI::Tempo, clock_to) if clock_to
end
follow_out.call

screen = UI.layout("Tempo + Keys", cols: 4, rows: 2) do
  tempo col: 0..2, row: 0
  knob id: :bpm, col: 3, row: 0, label: "BPM", color: :cyan,
       min: 40, max: 240, step: 1, value: UI::Tempo.bpm do |v|
    UI::Tempo.bpm = v
  end
  keys col: 0..3, row: 1, id: :keys, octaves: 2, low: 48, device: md.midi_out
end

# TAP / Sync / 他のスクリプトでテンポが変わったら、ノブの表示を合わせる
# （notify: false でノブのブロックは呼ばない）
UI::Tempo.on_change { |bpm| screen[:bpm].set_value(bpm, notify: false) }

# OUT を選び直したら、鍵盤の送信先とクロックの送り先を移す
md.on_change do |e|
  next unless e[:kind] == :select_out
  screen[:keys].device = md.midi_out
  follow_out.call
end

screen.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)
end
