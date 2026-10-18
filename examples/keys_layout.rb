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

require 'midi'
require 'ui'

out = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless out
  puts "No MIDI output available"
  return
end
dev = MIDI::Device.new(out)

# 先に作って keys: で渡す（パッドのブロックから参照するため）
kb = UI::Keys.new(octaves: 3, low: 48, device: dev)

UI.layout("Play", cols: 4, rows: [1, 2]) do
  knob col: 0, row: 0, label: "Cutoff", color: :cyan, value: 64 do |v|
    dev.control_change(74, v.to_i)
  end
  knob col: 1, row: 0, label: "Reso", color: :magenta, value: 20 do |v|
    dev.control_change(71, v.to_i)
  end
  pad col: 2, row: 0, label: "Oct-", color: :blue do kb.octave_down end
  pad col: 3, row: 0, label: "Oct+", color: :blue do kb.octave_up end
  # keys: を省くと UI::Keys.new(**キーワード) で作られる（layout[:id] で取れる）
  keys col: 0..3, row: 1, keys: kb
end.show

sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  UI.process
  sleep_ms 10
end
