# midi_devices.rb
#
# UI::MIDIDevices（docs/UI_MIDI_DEVICES.md）の例。スクリプト自身は MIDI Devices 画面の
# Script という機器で、どの機器で弾き、どの音源を鳴らすかは画面で Script の列 / 行を
# パッチして決める（何台でも）。スクリプトは書き換えなくてよい。
#
#   - UI::MIDIDevices.input: Script へパッチされた機器から届く入力
#   - UI::MIDIDevices.output: Script そのもの。送ったものは Script のパッチの先へ出ていく
#
# 弾いた音を 3 度上にハモらせて送る。鍵盤と音源の間も画面でパッチしておけば、
# 元の音は C から直接、ハモりだけがこのスクリプトから届く。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# Script に何もパッチされていなければここでつなぐ（画面で張り替えられる）。
# 保存済みのパッチがあればそちらが優先
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
  when :patch      then UI.log("patch: #{e[:from].info} > #{e[:to].info}")
  when :unpatch    then UI.log("unpatch: #{e[:from].info} > #{e[:to].info}")
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
