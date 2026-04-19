# midi_monitor.rb
# USB-MIDI と SAM2695 から入ってくる MIDI 信号をすべてキャプチャして
# UI ログに出力する汎用モニタ。
#
# 対応イベント:
#   Channel Voice   : note_on / note_off / control_change / program_change
#                     pitch_bend / poly_aftertouch / channel_pressure
#   System Common   : system_reset
#   System Real-time: start / stop / continue
#                     (clock と active_sensing はログが溢れるので既定では抑制)
#   System Exclusive: sysex — event[:data] に F0..F7 を含む Array<Integer>。
#                     MIDI_SYSEX_MAX_LEN (512B) を超えた場合は event[:truncated]
#                     が true で、末尾が切り詰められる。

require 'midi'
require 'ui'

sam = MIDIDevices.sam2695
usb = MIDIDevices.usb_midi_host

unless sam || usb
  UI.log("No MIDI input available on this board")
  exit
end

UI.log("MIDI Monitor started")
UI.log("Board: #{BoardConfig::BOARD_NAME}")
UI.log("SAM2695: #{sam ? 'ON' : '--'}  USB-MIDI: #{usb ? 'ON' : '--'}")

# ---- 表示ヘルパ --------------------------------------------------------

NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B]

def note_label(n)
  "#{NOTE_NAMES[n % 12]}#{(n / 12) - 1}(#{n})"
end

def hex2(n)
  s = n.to_s(16).upcase
  s.length < 2 ? "0#{s}" : s
end

# SysEx などのバイト列を "F0 7E 7F 06 01 F7" 形式に整形
def hex_dump(bytes)
  return "(empty)" if bytes.nil? || bytes.empty?
  out = ""
  bytes.each_with_index do |b, i|
    out << " " unless i == 0
    out << hex2(b)
  end
  out
end

# ---- ログ用タグ設定 ----------------------------------------------------
#
# LOG_CLOCK を true にすると大量の 0xF8 までログに出る（デバッグ用）。
# Active Sensing は既定で送信側が出力しない想定なので対応不要。
LOG_CLOCK = false

# ---- ハンドラ ----------------------------------------------------------

def install_handlers(input, tag)
  input.on(:note_on) do |e|
    UI.log("#{tag} NoteOn  ch=#{e[:channel]} #{note_label(e[:note])} vel=#{e[:velocity]}")
  end

  input.on(:note_off) do |e|
    UI.log("#{tag} NoteOff ch=#{e[:channel]} #{note_label(e[:note])} vel=#{e[:velocity]}")
  end

  input.on(:control_change) do |e|
    UI.log("#{tag} CC      ch=#{e[:channel]} cc=#{e[:cc]} val=#{e[:value]}")
  end

  input.on(:program_change) do |e|
    UI.log("#{tag} PC      ch=#{e[:channel]} program=#{e[:program]}")
  end

  input.on(:pitch_bend) do |e|
    UI.log("#{tag} PB      ch=#{e[:channel]} value=#{e[:value]}")
  end

  input.on(:poly_aftertouch) do |e|
    UI.log("#{tag} PAT     ch=#{e[:channel]} #{note_label(e[:note])} pressure=#{e[:pressure]}")
  end

  input.on(:channel_pressure) do |e|
    UI.log("#{tag} CPress  ch=#{e[:channel]} pressure=#{e[:pressure]}")
  end

  input.on(:start)    { UI.log("#{tag} >> Start")    }
  input.on(:stop)     { UI.log("#{tag} >> Stop")     }
  input.on(:continue) { UI.log("#{tag} >> Continue") }

  input.on(:system_reset) do
    UI.log("#{tag} >> SystemReset")
  end

  if LOG_CLOCK
    input.on(:clock) { UI.log("#{tag} .clock") }
  end

  # SysEx: event[:data] に F0..F7 の Array<Integer> が入る
  input.on(:sysex) do |e|
    bytes = e[:data]
    mark = e[:truncated] ? " TRUNC" : ""
    UI.log("#{tag} SysEx#{mark} len=#{bytes ? bytes.length : 0} [#{hex_dump(bytes)}]")
  end

  # 未知のイベント型が来たら :any で拾ってデバッグ出力
  input.on(:any) do |e|
    known = [:note_on, :note_off, :control_change, :program_change,
             :pitch_bend, :poly_aftertouch, :channel_pressure,
             :start, :stop, :continue, :system_reset, :sysex]
    known << :clock if LOG_CLOCK
    unless known.include?(e[:type])
      UI.log("#{tag} ?#{e[:type]} #{e.inspect}")
    end
  end
end

sam_input = nil
usb_input = nil

if sam
  sam_input = MIDI::Input.new(MIDI::Device.new(sam))
  install_handlers(sam_input, "SAM")
end

if usb
  usb_input = MIDI::Input.new(MIDI::Device.new(usb))
  install_handlers(usb_input, "USB")
end

# ---- UI --------------------------------------------------------------

UI.pad(1, label: "Clear", color: :red, type: :trigger) do
  UI.clear_log if UI.respond_to?(:clear_log)
  UI.log("-- cleared --")
end

# ---- メインループ ----------------------------------------------------

on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # 入力イベントは MIDI.bpm_loop が自動で dispatch する
end
