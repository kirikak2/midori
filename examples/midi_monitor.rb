# midi_monitor.rb
# スクリプトの入力（MIDI Devices 画面の Script）に入ってくる MIDI 信号をキャプチャして
# UI ログに出力する汎用モニタ。見る機器は画面で Script の列へパッチすれば増やせる
# （何台でも。どの機器から来たかも出す）。
#
# 対応イベント:
#   Channel Voice   : note_on / note_off / control_change / program_change
#                     pitch_bend / poly_aftertouch / channel_pressure
#   System Real-time: start / stop / continue / system_reset
#                     (clock はログが溢れるので既定では抑制)

require 'midi'
require 'ui'

md = UI::MIDIDevices

# Script に何もパッチされていなければここでつなぐ（画面で張り替えられる）
md.midi_in ||= MIDIDevices.usb_midi_host || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device

UI.log("MIDI Monitor started")
UI.log("Board: #{BoardConfig::BOARD_NAME}")
UI.log(md.midi_in ? "IN: Script" : "IN: - (patch a device to Script on the MIDI Devices screen)")

md.on_change do |e|
  next unless e[:kind] == :patch || e[:kind] == :unpatch
  next unless MIDI.route_bit(e[:to]) == md.script_bit
  UI.log("#{e[:kind] == :patch ? '+' : '-'} IN: #{e[:from] ? e[:from].info : '?'}")
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
# md.input は Script へパッチされた機器から届く。ハンドラが受け取る m は MIDI::Message で、
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
