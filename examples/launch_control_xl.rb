# launch_control_xl.rb
# Novation Launch Control XL の入力を受け取り UI にログ出力するサンプル
#
# 接続: Launch Control XL を USB-MIDI ホストポートに接続し、Factory Template 1
# （出荷時デフォルト、起動時に点灯する一番上のテンプレート）を選択。
#
# Factory Template 1 の MIDI マッピング（全て Channel 9 = 表記上は 8）:
#   Send A   (上段ノブ 8 個) : CC 13-20
#   Send B   (中段ノブ 8 個) : CC 29-36
#   Pan/Dev  (下段ノブ 8 個) : CC 49-56
#   Faders   (フェーダ 8 個) : CC 77-84
#   Track Focus (上段ボタン): Note 41-48
#   Track Control (下段ボタン): Note 73-80
#   Device / Mute / Solo / Record Arm: Note 105-108
#   Up / Down / Left / Right: CC 104-107（Note ではなく CC）
#
# テンプレートによって番号が変わるため、スクリプト側では番号で分岐せず
# 生のイベント内容をそのままログに出しています。

require 'midi'
require 'ui'

usb = MIDIDevices.usb_midi_host

unless usb
  UI.log("Error: USB-MIDI host is required")
  exit
end

lcxl  = MIDI::Device.new(usb)
input = MIDI::Input.new(lcxl)

UI.log("Launch Control XL listener started")
UI.log("Board: #{BoardConfig::BOARD_NAME}")

# ノート番号 → 音名（ログ表示用）
NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B]
def note_label(n)
  "#{NOTE_NAMES[n % 12]}#{(n / 12) - 1} (#{n})"
end

# ボタン類（Track Focus / Track Control / Device・Mute・Solo・Arm）
input.on(:note_on) do |e|
  if e[:velocity] == 0
    UI.log("BTN release ch=#{e[:channel]} note=#{note_label(e[:note])}")
  else
    UI.log("BTN press   ch=#{e[:channel]} note=#{note_label(e[:note])} vel=#{e[:velocity]}")
  end
end

input.on(:note_off) do |e|
  UI.log("BTN release ch=#{e[:channel]} note=#{note_label(e[:note])}")
end

# ノブ / フェーダ / 矢印ボタン
input.on(:control_change) do |e|
  UI.log("CC  ch=#{e[:channel]} cc=#{e[:cc]} value=#{e[:value]}")
end

# その他（Program Change / Pitch Bend / Aftertouch / SysEx 等）
input.on(:program_change) do |e|
  UI.log("PC  ch=#{e[:channel]} program=#{e[:program]}")
end

input.on(:pitch_bend) do |e|
  UI.log("PB  ch=#{e[:channel]} value=#{e[:value]}")
end

input.on(:channel_pressure) do |e|
  UI.log("CP  ch=#{e[:channel]} pressure=#{e[:pressure] || e[:value]}")
end

input.on(:poly_aftertouch) do |e|
  UI.log("PAT ch=#{e[:channel]} note=#{note_label(e[:note])} pressure=#{e[:pressure]}")
end

# ログクリア用パッド
UI.pad(1, label: "Clear", color: :red, type: :trigger) do
  UI.clear_log if UI.respond_to?(:clear_log)
  UI.log("-- cleared --")
end

# メインループ: 20ms 間隔で UI と MIDI 入力を処理
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # 入力ハンドラは MIDI.bpm_loop 内で自動的に dispatch されるので空でよい
end
