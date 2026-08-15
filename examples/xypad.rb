# xypad.rb
#
# タッチ位置から音程 (X) と CC (Y) を送る演奏面。5本の指はそれぞれ独立した
# 「スロット」で、指ごとに挙動を丸ごと変えられる（詳細は docs/XYPAD.md）。
#
# ここでは:
#   1〜4本目: Cメジャースケールにスナップ + グライド、Y は CC74 (Cutoff)
#   5本目   : 音階を使わず、X/Y ともに生の CC を送るエフェクトコントローラ
#
# パッド画面の [Hold]（トグル）で全スロット一括ラッチ。ナビの矢印で
# Pads 画面と行き来できる。

require 'midi'
require 'ui'

transport = MIDIDevices.usb_midi_device || MIDIDevices.sam2695
unless transport
  puts "No MIDI output available"
  return
end

device = MIDI::Device.new(transport)

# ---- 1〜5本目: 既定はスケール + グライド --------------------------------
pad = UI::XYPad.new(
  scale: [36, 38, 40, 41, 43, 45, 47, 48],  # C2から1オクターブ+、Cメジャー抜粋
  glide_range: 2,        # 全幅スライドで±2半音
  y_cc: 74,               # フィルターカットオフ
  y_range: 0..127,
  device: device
)

# ---- 5本目だけ上書き: 音階を使わない CC コントローラに ------------------
pad.slot(5,
  x_mode: :cc, x_cc: 70, x_range: 0..127,   # サウンドの明るさ
  y_cc: 71, y_range: 0..127,                 # レゾナンス
  note: 60,                                   # タッチのゲート用固定ノート (C4)
  velocity: 100,
  device: device
)

pad.show

# ---- Hold（全スロット一括） ----------------------------------------------
UI.pad(1, label: "Hold", type: :toggle) { |on| pad.hold = on }

# ---- 生イベントのログ（任意） --------------------------------------------
pad.on_touch do |t|
  UI.log("slot#{t[:slot]} #{t[:phase]} ch#{t[:channel]} note#{t[:note]}")
end

sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  UI.process
  sleep_ms 5
end
