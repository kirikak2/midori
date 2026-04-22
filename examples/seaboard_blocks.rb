# seaboard_blocks.rb
# ROLI Seaboard BLOCKS からの MPE MIDI 入力（ch 1〜15）を
# 単一チャンネルに変換して MIDI-DIN（SAM2695）に出力する。
#
# ROLI Seaboard BLOCKS は MPE（MIDI Polyphonic Expression）を使用し、
# 各指を独立したチャンネル（1〜15）に割り当てる。
#
# 対応 MPE メッセージ:
#   Note On / Note Off      … 打鍵・離鍵（velocity を記録）
#   Channel Pressure        … Press：打鍵 velocity + pressure を加算して送信
#   Pitch Bend              … Glide（水平スライド）
#                             ※ 受信側の PB レンジを ±48 半音に設定して転送
#   Control Change (CC#74)  … Slide（鍵盤上の縦スライド）
#
# UI パッド 1〜12：出力 MIDI チャンネル（ch 1〜12）を選択

require 'midi'
require 'ui'

usb = MIDIDevices.usb_midi_host
sam = MIDIDevices.sam2695

unless usb
  UI.log("USB-MIDI Host not available")
  exit
end

unless sam
  UI.log("SAM2695 (MIDI-DIN) not available")
  exit
end

$din    = MIDI::Device.new(sam)
$usb_in = MIDI::Input.new(MIDI::Device.new(usb))
$din_in = MIDI::Input.new(MIDI::Device.new(sam))

# 出力チャンネル（0-indexed: 0 = MIDI ch1, 11 = MIDI ch12）
$out_ch = 0

# 打鍵チャンネルごとのノート状態を記録（Seaboard ch 0〜14）
# channel_pressure 受信時に velocity + pressure を加算して note_on / channel_pressure 両方に反映する
# 値: {note: Integer, velocity: Integer} または nil（発音なし）
$note_states = {}

# ---- ピッチベンドレンジ設定 ------------------------------------------------
#
# 問題: Seaboard BLOCKS の MPE ピッチベンドは ±48 半音を前提に送出する。
# 受信側シンセのデフォルトは ±2 半音（GM 標準）のため、わずかな動きしか
# 反映されない（例: 1半音のグライド → 送出値 ≈170 → 受信側で 0.04 半音）。
#
# 修正: RPN (Registered Parameter Number) 0 = Pitch Bend Sensitivity で
# 受信チャンネルのピッチベンドレンジを ±48 半音に設定する。
#
PB_SEMITONES = 48  # Seaboard BLOCKS MPE デフォルトに合わせる

def setup_pb_range(device, ch, semitones)
  device.control_change(101, 0,        channel: ch)  # RPN MSB = 0
  device.control_change(100, 0,        channel: ch)  # RPN LSB = 0 (PB Sensitivity)
  device.control_change(6,   semitones, channel: ch)  # Data Entry MSB = semitones
  device.control_change(38,  0,        channel: ch)  # Data Entry LSB = 0 cents
  device.control_change(101, 127,      channel: ch)  # RPN Null（解除）
  device.control_change(100, 127,      channel: ch)
end

setup_pb_range($din, $out_ch, PB_SEMITONES)

UI.log("Seaboard BLOCKS -> MIDI-DIN")
UI.log("Output: ch1  PB: +-#{PB_SEMITONES}st")

# ---- MPE → 単チャンネル変換 ------------------------------------------------

$usb_in.on(:note_on) do |e|
  $note_states[e[:channel]] = {note: e[:note], velocity: e[:velocity]}
  $din.note_on(e[:note], e[:velocity], channel: $out_ch)
end

$usb_in.on(:note_off) do |e|
  $note_states[e[:channel]] = nil
  $din.note_off(e[:note], e[:velocity], channel: $out_ch)
end

# Channel Pressure（Press）: 打鍵 velocity + pressure を加算して
# channel_pressure と note_on の両方に反映する。
# note_on を再送することで、シンセ側の velocity 感度にも押し込みが作用する。
$usb_in.on(:channel_pressure) do |e|
  state = $note_states[e[:channel]]
  base = state ? state[:velocity] : 64
  combined = base + e[:pressure]
  combined = 127 if combined > 127
  combined = 0   if combined < 0
  $din.channel_pressure(combined, channel: $out_ch)
end

$usb_in.on(:pitch_bend) do |e|
  $din.pitch_bend(e[:value], channel: $out_ch)
end

$usb_in.on(:control_change) do |e|
  $din.control_change(e[:cc], e[:value], channel: $out_ch)
end

# ---- MIDI-DIN IN → MIDI-DIN OUT パススルー --------------------------------

$din_in.on(:note_on) do |e|
  $din.note_on(e[:note], e[:velocity], channel: e[:channel])
end

$din_in.on(:note_off) do |e|
  $din.note_off(e[:note], e[:velocity], channel: e[:channel])
end

$din_in.on(:control_change) do |e|
  $din.control_change(e[:cc], e[:value], channel: e[:channel])
end

$din_in.on(:program_change) do |e|
  $din.program_change(e[:program], channel: e[:channel])
end

$din_in.on(:pitch_bend) do |e|
  $din.pitch_bend(e[:value], channel: e[:channel])
end

$din_in.on(:channel_pressure) do |e|
  $din.channel_pressure(e[:pressure], channel: e[:channel])
end

$din_in.on(:poly_aftertouch) do |e|
  $din.poly_aftertouch(e[:note], e[:pressure], channel: e[:channel])
end

# ---- 出力チャンネル選択 UI（パッド 1〜12）----------------------------------

PAD_COLORS = [
  :blue,   :cyan,  :green,  :yellow,
  :orange, :red,   :purple, :pink,
  :white,  :cyan,  :green,  :orange,
]

(1..12).each do |ch|
  UI.pad(ch, label: "CH#{ch}", color: PAD_COLORS[ch - 1], type: :trigger)
end

UI.on(:pad_press) do |event|
  idx = event[:index]
  if idx >= 0 && idx < 12
    $out_ch = idx
    setup_pb_range($din, $out_ch, PB_SEMITONES)
    UI.log("Output ch#{idx + 1}  PB: +-#{PB_SEMITONES}st")
  end
end

# ---- メインループ -----------------------------------------------------------

on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # MIDI イベントは bpm_loop が自動ディスパッチ
end
