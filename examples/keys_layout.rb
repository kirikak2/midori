# keys_layout.rb
#
# レイアウト DSL（docs/UI_LAYOUT_DSL.md）で鍵盤とノブ・パッドを 1 画面に並べる。
#
#   ┌──────┬──────┬──────┬──────┐
#   │Cutoff│ Reso │ Oct- │ Oct+ │
#   ├──────┴──────┴──────┴──────┤
#   │                           │
#   │      Keys（3 オクターブ）   │
#   │                           │
#   └───────────────────────────┘
#
# 鍵盤とノブは MIDI Devices 画面で選んだ OUT を鳴らす。選んだ IN
# （PC の DAW など）から来たノートで鍵盤が光る。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in  ||= MIDIDevices.usb_midi_device
md.midi_out ||= MIDIDevices.usb_midi_device || MIDIDevices.amy || MIDIDevices.sam2695
unless md.midi_out
  puts "No MIDI output available"
  return
end

# 先に作って keys: で渡す（パッドのブロックから参照するため）
kb = UI::Keys.new(octaves: 3, low: 48, device: md.midi_out)

UI.layout("Play", cols: 4, rows: [1, 2]) do
  knob col: 0, row: 0, label: "Cutoff", color: :cyan, value: 64 do |v|
    md.midi_out&.control_change(74, v.to_i)
  end
  knob col: 1, row: 0, label: "Reso", color: :magenta, value: 20 do |v|
    md.midi_out&.control_change(71, v.to_i)
  end
  pad col: 2, row: 0, label: "Oct-", color: :blue do kb.octave_down end
  pad col: 3, row: 0, label: "Oct+", color: :blue do kb.octave_up end
  # keys: を省くと UI::Keys.new(**キーワード) で作られる（layout[:id] で取れる）
  keys col: 0..3, row: 1, keys: kb
end.show

# IN から来たノートで鍵盤を光らせる。IN を選び直したら張り直す
lit_by = nil
follow_in = Proc.new do
  MIDI.unroute(lit_by, kb) if lit_by
  lit_by = md.midi_in
  MIDI.route(lit_by, kb) if lit_by
end
follow_in.call

md.on_change do |e|
  case e[:kind]
  when :select_in  then follow_in.call
  when :select_out then kb.device = md.midi_out
  end
end

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
