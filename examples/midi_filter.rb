# midi_filter.rb
#
# MIDI.filter（docs/MIDI_FILTER.md）の例。MIDI Devices 画面で選んだ IN（鍵盤）を
# OUT（音源）へ MIDI.route で転送しつつ、ノートだけ Ruby のブロックを通す。
# IN / OUT を画面で選び直すと、ルートと filter を張り直す（未選択なら
# USB-MIDI Host → AMY など）。
#
#   - ノートは 1 オクターブ上げ、5 度上を重ねる（配列を返すと複数送る）
#   - ベロシティ 10 未満は捨てる（nil を返すと捨てる）
#   - クロックや CC は Ruby を通らず C から直接届く
#
# 選んだ IN に来たノートを Log に出す（UI::MIDIDevices.input = IN に追従する MIDI.on）。
# Pad 1（Bypass）を ON にすると filter を外し、OFF で付け直す。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in  ||= MIDIDevices.usb_midi_host
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_in && md.midi_out
  puts "Select a MIDI IN and OUT on the MIDI Devices screen"
  return
end

def add_filter(route)
  route.filter(types: [:note_on, :note_off]) do |m|
    next nil if m.type == :note_on && m.velocity < 10
    m.note += 12
    [m, m.with(note: m.note + 7)]
  end
end

# IN → OUT のルートと filter。md.midi_in / md.midi_out はその時点の機器なので、
# 選び直したら張り直す
route = nil
filter = nil
bypass = false
connect = Proc.new do
  route.unroute if route
  route = nil
  if md.midi_in && md.midi_out
    route = MIDI.route(md.midi_in, md.midi_out) || nil
    filter = add_filter(route) if route && !bypass
  end
end
connect.call

md.on_change do |e|
  connect.call if e[:kind] == :select_in || e[:kind] == :select_out
end

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
