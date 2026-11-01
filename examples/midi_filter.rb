# midi_filter.rb
#
# MIDI.filter（docs/MIDI_FILTER.md）の例。鍵盤（USB-MIDI Host）を音源（AMY など）へ
# MIDI.route で転送しつつ、ノートだけ Ruby のブロックを通す。
#
#   - ノートは 1 オクターブ上げ、5 度上を重ねる（配列を返すと複数送る）
#   - ベロシティ 10 未満は捨てる（nil を返すと捨てる）
#   - クロックや CC は Ruby を通らず C から直接届く
#
# 鍵盤は Script（このスクリプト）にもパッチし、来たノートを Log に出す
# （UI::MIDIDevices.input = Script へパッチされた機器からの MIDI.on。画面で Script へ
# 別の機器をパッチすればそれも出る）。
# Pad 1（Bypass）を ON にすると filter を外し、OFF で付け直す。

require 'midi'
require 'ui'

md = UI::MIDIDevices

kb = MIDIDevices.usb_midi_host
synth = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless kb && synth
  puts "Needs the USB-MIDI host and a synth"
  return
end
md.midi_in ||= kb

def add_filter(route)
  route.filter(types: [:note_on, :note_off]) do |m|
    next nil if m.type == :note_on && m.velocity < 10
    m.note += 12
    [m, m.with(note: m.note + 7)]
  end
end

# 鍵盤 → 音源のルートと filter
route = MIDI.route(kb, synth) || nil
filter = route ? add_filter(route) : nil
bypass = false

md.input.on(:note_on) do |m|
  UI.log("note #{m.note} vel #{m.velocity} ch #{m.channel + 1}")
end

UI.pad(1, label: "Bypass", color: :orange, type: :toggle) do |on|
  bypass = on
  next unless route
  if bypass
    route.unfilter(filter)
  else
    filter = add_filter(route)
  end
end
UI.pads

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(20)      # filter はここで（メッセージが来た瞬間に）動く
end
