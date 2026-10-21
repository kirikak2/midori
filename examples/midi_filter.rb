# midi_filter.rb
#
# MIDI.filter（docs/MIDI_FILTER.md）の例。USB-MIDI Host に繋いだ鍵盤を音源へ
# MIDI.route で転送しつつ、ノートだけ Ruby のブロックを通す。
#
#   - ノートは 1 オクターブ上げ、5 度上を重ねる（配列を返すと複数送る）
#   - ベロシティ 10 未満は捨てる（nil を返すと捨てる）
#   - クロックや CC は Ruby を通らず C から直接届く
#
# MIDI.on でノートを Log に出す（MIDI::Input#on の後継）。
# Pad 1（Bypass）を ON にすると filter を外し、OFF で付け直す。

require 'midi'
require 'ui'

kb = MIDIDevices.usb_midi_host
out = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless kb && out
  puts "Needs the USB-MIDI host and a MIDI output"
  return
end

route = MIDI.route(kb, out)

def add_filter(route)
  route.filter(types: [:note_on, :note_off]) do |m|
    next nil if m.type == :note_on && m.velocity < 10
    m.note += 12
    [m, m.with(note: m.note + 7)]
  end
end

filter = add_filter(route)

MIDI.on(kb, :note_on) do |m|
  UI.log("note #{m.note} vel #{m.velocity} ch #{m.channel + 1}")
end

UI.pad(1, label: "Bypass", color: :orange, type: :toggle) do |bypass|
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
