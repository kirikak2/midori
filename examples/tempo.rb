# tempo.rb
#
# UI::Tempo（docs/TEMPO.md）の例。テンポを MIDI クロックとして MIDI Devices
# 画面で選んだ OUT（未選択なら PC の DAW）へ送り、テンポはノブとパッドで変える。
# OUT を選び直すとクロックの送り先も移る。ほかの機器へも送りたいときは、
# 同じ画面で Tempo の行から機器の列へパッチする。
#
#   - Tempo 画面をレイアウトのセルに置く（BPM をタップで TAP tempo）
#   - BPM ノブ。TAP や Sync でテンポが変わるとノブも追従する
#   - -1 / +1 / Play（Start / Stop）のパッド
#
# Sync を ON にすると外部クロックのテンポで送り直す（外部クロックの中継）。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_out ||= MIDIDevices.usb_midi_device || MIDIDevices.sam2695

UI::Tempo.bpm = 120

# クロックを OUT へ。OUT を選び直したら張り直す
clock_to = nil
follow_out = Proc.new do
  MIDI.unroute(UI::Tempo, clock_to) if clock_to
  clock_to = md.midi_out
  MIDI.route(UI::Tempo, clock_to) if clock_to
end
follow_out.call
md.on_change { |e| follow_out.call if e[:kind] == :select_out }

clock = UI.layout("Clock", cols: 4, rows: 3) do
  tempo col: 0..2, row: 0..1
  knob id: :bpm, col: 3, row: 0..1, label: "BPM", min: 40, max: 240, step: 1,
       value: UI::Tempo.bpm do |v|
    UI::Tempo.bpm = v
  end
  pad col: 0, row: 2, label: "-1" do UI::Tempo.bpm -= 1 end
  pad col: 1, row: 2, label: "+1" do UI::Tempo.bpm += 1 end
  pad col: 2, row: 2, label: "TAP" do UI::Tempo.tap end
  pad col: 3, row: 2, label: "Play", type: :toggle, color: :green do |on|
    on ? UI::Tempo.start : UI::Tempo.stop
  end
end

UI::Tempo.on_change { |bpm| clock[:bpm].set_value(bpm, notify: false) }
clock.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)
end
