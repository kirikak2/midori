# keys.rb -- 鍵盤とノブ・パッドを 1 画面に（docs/UI_LAYOUT_DSL.md / docs/KEYS.md）
#
# /sd/plugins/ に置くと起動時に読み込まれ（プラグイン）、UI モードでは Home に Play のタイルが出る。
#
#   ┌──────┬──────┬──────┬──────┐
#   │Cutoff│ Reso │ Oct- │ Oct+ │
#   ├──────┴──────┴──────┴──────┤
#   │                           │
#   │      Keys（3 オクターブ）   │
#   │                           │
#   └───────────────────────────┘
#
# 鍵盤とノブは MIDI Devices 画面で選んだ OUT を鳴らす（未選択なら鳴らない）。
# 外部の鍵盤や PC の DAW から来たノートで鍵を光らせるには、MIDI Devices 画面で
# その機器の行から Keys の列へパッチする（画面のパッチは保存される）。
#
# 起動時に読むファイルなので、ループも show も書かない。
# 画面の鍵盤はモデルが 1 つなので、/sd/plugins で keys を置くのはこのファイルだけにする。

require 'midi'
require 'ui'

# レイアウトは UI モードだけ（スクリプトの画面はスクリプトのもの。XYPad / Keys のモデルも 1 つ）
return unless $midori_mode == :ui

md = UI::MIDIDevices

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
end

# 鍵盤の送信先は C が覚えているので、OUT を選び直したら渡し直す
md.on_change do |e|
  kb.device = md.midi_out if e[:kind] == :select_out
end
