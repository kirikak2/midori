# seaboard_blocks.rb
# ROLI Seaboard BLOCKS からの MPE MIDI 入力（ch 1〜15）を
# 単一チャンネルに変換して出力する。
#
# 入力は MIDI Devices 画面で選んだ IN（未選択なら USB-MIDI Host）、
# 出力は選んだ OUT（未選択なら MIDI-DIN = SAM2695）。
#
# ROLI Seaboard BLOCKS は MPE（MIDI Polyphonic Expression）を使用し、
# 各指を独立したチャンネル（1〜15）に割り当てる。
#
# 対応 MPE メッセージ:
#   Note On / Note Off      … 打鍵・離鍵
#   Channel Pressure        … Press（鍵盤を押す縦方向の圧力）
#   Pitch Bend              … Glide（水平スライド）
#                             ※ 受信側の PB レンジを ±48 半音に設定して転送
#   Control Change (CC#74)  … Slide（鍵盤上の縦スライド）
#
# UI パッド 1〜12：出力 MIDI チャンネル（ch 1〜12）を選択
#
# MIDI-DIN の入力もそのまま鳴らしたいときは、MIDI Devices 画面で SAM2695 の
# 行から音源の列へパッチする（Ruby を通らず C で転送される）。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in  ||= MIDIDevices.usb_midi_host
md.midi_out ||= MIDIDevices.sam2695

unless md.midi_in && md.midi_out
  UI.log("Select the Seaboard (IN) and a synth (OUT) on the MIDI Devices screen")
  return
end

# 出力チャンネル（0-indexed: 0 = MIDI ch1, 11 = MIDI ch12）
$out_ch = 0

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

setup_pb_range(md.midi_out, $out_ch, PB_SEMITONES)

# OUT を選び直したら、新しい OUT にもピッチベンドレンジを設定する
md.on_change do |e|
  if e[:kind] == :select_out && md.midi_out
    setup_pb_range(md.midi_out, $out_ch, PB_SEMITONES)
    UI.log("OUT: #{md.midi_out.info}")
  end
end

UI.log("Seaboard BLOCKS -> #{md.midi_out.info}")
UI.log("Output: ch1  PB: +-#{PB_SEMITONES}st")

# ---- MPE → 単チャンネル変換 ------------------------------------------------
# md.input は選んだ IN に追従する。m は MIDI::Message（チャンネルは捨てて
# $out_ch に寄せる）。OUT は毎回 md.midi_out を聞き直す

inp = md.input

inp.on(:note_on) do |m|
  md.midi_out&.note_on(m.note, m.velocity, channel: $out_ch)
end

inp.on(:note_off) do |m|
  md.midi_out&.note_off(m.note, m.velocity, channel: $out_ch)
end

inp.on(:channel_pressure) do |m|
  md.midi_out&.channel_pressure(m.value, channel: $out_ch)
end

inp.on(:pitch_bend) do |m|
  md.midi_out&.pitch_bend(m.value, channel: $out_ch)   # -8192..8191
end

inp.on(:control_change) do |m|
  md.midi_out&.control_change(m.cc, m.value, channel: $out_ch)
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
    setup_pb_range(md.midi_out, $out_ch, PB_SEMITONES) if md.midi_out
    UI.log("Output ch#{idx + 1}  PB: +-#{PB_SEMITONES}st")
  end
end

# ---- メインループ -----------------------------------------------------------

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)    # MIDI のハンドラはここ（と UI.process）で呼ばれる
end
