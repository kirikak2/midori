# keys.rb
#
# 画面の鍵盤（UI::Keys、docs/KEYS.md）を弾く。鍵盤は picoruby-midi の
# トランスポートでもあるので、MIDI.route の送信元にも宛先にもなる。
#
#   - タッチで弾いた音: device:（AMY / SAM2695 / USB-MIDI Device）へ C 側から直接
#   - USB-MIDI Host に繋いだ鍵盤で弾いた音: 同じ音源へ転送しつつ、画面の鍵盤を光らせる
#     （光る鍵盤のように「いま何が押されているか」を表示）
#   - C メジャーペンタトニックの鍵だけ青く塗っておく（スケール表示）
#
# Home の「Keys」タイルで表示。Pads 画面の Oct- / Oct+ で音域を動かす。

require 'midi'
require 'ui'

out = MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless out
  puts "No MIDI output available"
  return
end

keys = UI::Keys.new(octaves: 2, low: 48,      # C3〜C5（25 鍵）
                    velocity: :position,      # 鍵の奥ほど弱く、手前ほど強く
                    device: out,
                    light: :orange)           # 外から来たノートの色

# スケール表示: 各オクターブの C D E G A
pentatonic = [0, 2, 4, 7, 9]
(0..127).each do |n|
  keys.set_color(n, UI.rgb(40, 80, 200)) if pentatonic.include?(n % 12)
end

# 外部の鍵盤 → 音源 と 外部の鍵盤 → 画面の鍵盤（光らせる）
host = MIDIDevices.usb_midi_host
if host
  MIDI.route(host, out)
  MIDI.route(host, keys)
  keys.channel_color(9, :red)   # ch 10（ドラム）は赤く光らせる
end

UI.pad(1, label: "Oct-", color: :blue) { keys.octave_down; UI.log("low = #{keys.low}") }
UI.pad(2, label: "Oct+", color: :blue) { keys.octave_up;   UI.log("low = #{keys.low}") }
UI.pad(3, label: "Clear", color: :gray) { keys.clear_lights }

# 弾いたノートを Ruby でも見たいとき（発音には不要）
keys.on_note do |e|
  UI.log("key #{e[:note]} #{e[:state] ? "on v#{e[:velocity]}" : 'off'}")
end

keys.show

sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  UI.process
  sleep_ms 10
end
