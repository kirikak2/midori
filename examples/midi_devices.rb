# midi_devices.rb
#
# UI::MIDIDevices（docs/UI_MIDI_DEVICES.md）の例。MIDI Devices 画面で選んだ
# IN / OUT を使うスクリプト。どの機器で弾き、どの音源を鳴らすかは画面の
# Prog(input) 列 / Prog(output) 行でいつでも変えられ、スクリプトは書き換えなくてよい。
#
#   - UI::MIDIDevices.input: 選んだ IN に追従する入力。選び直しても
#     ハンドラはそのまま
#   - UI::MIDIDevices.output: 選んだ OUT に追従する出力。選び直しても
#     そのまま使える（midi_out はその時点の機器なので、保持せず毎回聞き直す）
#
# 弾いた音を 3 度上にハモらせて OUT へ送る。IN と OUT の間を画面でパッチ
# しておけば、元の音は C から直接、ハモりだけがこのスクリプトから届く。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 最初の選択（画面で選び直せる）。保存済みの選択があればそちらが優先
md.midi_in ||= MIDIDevices.usb_midi_host || MIDIDevices.usb_midi_device
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695

inp = md.input
out = md.output
inp.on(:note_on) do |m|
  out.note_on(m.note + 4, m.velocity, channel: m.channel)
end
inp.on(:note_off) do |m|
  out.note_off(m.note + 4, channel: m.channel)
end

md.on_change do |e|
  case e[:kind]
  when :select_in  then UI.log("IN: #{e[:device] ? e[:device].info : '-'}")
  when :select_out then UI.log("OUT: #{e[:device] ? e[:device].info : '-'}")
  when :connect    then UI.log("connected: #{e[:device].info}")
  when :disconnect then UI.log("disconnected: #{e[:device].info}")
  end
end

md.patches.each do |p|
  puts "patch #{p[:from].info} -> #{p[:to].info}#{p[:channel] ? " ch#{p[:channel] + 1}" : ''}" \
       "#{p[:active] ? '' : ' (inactive)'}"
end

md.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)
end
