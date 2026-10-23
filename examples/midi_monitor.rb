# midi_monitor.rb
# MIDI Devices 画面で選んだ IN に入ってくる MIDI 信号をキャプチャして
# UI ログに出力する汎用モニタ。見る機器は画面の Prog(input) 列でいつでも選び直せる。
#
# 対応イベント:
#   Channel Voice   : note_on / note_off / control_change / program_change
#                     pitch_bend / poly_aftertouch / channel_pressure
#   System Real-time: start / stop / continue / system_reset
#                     (clock はログが溢れるので既定では抑制)

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in ||= MIDIDevices.usb_midi_host || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device

UI.log("MIDI Monitor started")
UI.log("Board: #{BoardConfig::BOARD_NAME}")
UI.log("IN: #{md.midi_in ? md.midi_in.info : '- (select one on the MIDI Devices screen)'}")

md.on_change do |e|
  UI.log("IN: #{e[:device] ? e[:device].info : '-'}") if e[:kind] == :select_in
end

# ---- 表示ヘルパ --------------------------------------------------------

NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B]

def note_label(n)
  "#{NOTE_NAMES[n % 12]}#{(n / 12) - 1}(#{n})"
end

# ---- ログ用タグ設定 ----------------------------------------------------
#
# LOG_CLOCK を true にすると大量の 0xF8 までログに出る（デバッグ用）。
LOG_CLOCK = false

# ---- ハンドラ ----------------------------------------------------------
#
# md.input は選んだ IN に追従する。ハンドラが受け取る m は MIDI::Message で、
# m.source が受信元（:usb_host / :uart / :usb_dev など）。

inp = md.input

inp.on(:note_on) do |m|
  UI.log("#{m.source} NoteOn  ch=#{m.channel} #{note_label(m.note)} vel=#{m.velocity}")
end

inp.on(:note_off) do |m|
  UI.log("#{m.source} NoteOff ch=#{m.channel} #{note_label(m.note)} vel=#{m.velocity}")
end

inp.on(:control_change) do |m|
  UI.log("#{m.source} CC      ch=#{m.channel} cc=#{m.cc} val=#{m.value}")
end

inp.on(:program_change) do |m|
  UI.log("#{m.source} PC      ch=#{m.channel} program=#{m.program}")
end

inp.on(:pitch_bend) do |m|
  UI.log("#{m.source} PB      ch=#{m.channel} value=#{m.value}")
end

inp.on(:poly_aftertouch) do |m|
  UI.log("#{m.source} PAT     ch=#{m.channel} #{note_label(m.note)} pressure=#{m.value}")
end

inp.on(:channel_pressure) do |m|
  UI.log("#{m.source} CPress  ch=#{m.channel} pressure=#{m.value}")
end

# System Real-time は名前を指定したときだけ届く
inp.on(:start)        { |m| UI.log("#{m.source} >> Start")       }
inp.on(:stop)         { |m| UI.log("#{m.source} >> Stop")        }
inp.on(:continue)     { |m| UI.log("#{m.source} >> Continue")    }
inp.on(:system_reset) { |m| UI.log("#{m.source} >> SystemReset") }

if LOG_CLOCK
  inp.on(:clock) { |m| UI.log("#{m.source} .clock") }
end

# ---- UI --------------------------------------------------------------

UI.pad(1, label: "Clear", color: :red, type: :trigger) do
  UI.clear_log if UI.respond_to?(:clear_log)
  UI.log("-- cleared --")
end

# ---- メインループ ----------------------------------------------------

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)    # ハンドラはここ（と UI.process）で呼ばれる
end
