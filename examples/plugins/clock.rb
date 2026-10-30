# clock.rb -- テンポと MIDI クロック（docs/TEMPO.md）
#
# /sd/plugins/ に置くと起動時に読み込まれ（プラグイン）、UI モードでは Home に Clock のタイルが出る。
#
#   - Tempo 画面をレイアウトのセルに置く（BPM をタップで TAP tempo）
#   - BPM ノブ。TAP や Sync でテンポが変わるとノブも追従する
#   - -1 / +1 / TAP / Play（Start / Stop）のパッド
#
# クロックを機器へ送るには、MIDI Devices 画面で Tempo の行から機器の列へパッチする
# （画面のパッチは保存されるので、次の起動からもそのまま送られる）。
# Sync を ON にすると外部クロックのテンポで送り直す（外部クロックの中継）。
#
# 起動時に読むファイルなので、ループも show も書かず、テンポも変えない。

require 'midi'
require 'ui'

# レイアウトは UI モードだけ（スクリプトの画面はスクリプトのもの。XYPad / Keys のモデルも 1 つ）
return unless $midori_mode == :ui

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
