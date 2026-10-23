# keys.rb
#
# 画面の鍵盤（UI::Keys、docs/KEYS.md）を弾く。鍵盤は picoruby-midi の
# トランスポートでもあるので、MIDI.route の送信元にも宛先にもなる。
#
#   - タッチで弾いた音: MIDI Devices 画面で選んだ OUT へ C 側から直接
#   - 選んだ IN（外部の鍵盤など）で弾いた音: 画面の鍵盤を光らせる
#     （光る鍵盤のように「いま何が押されているか」を表示）
#   - C メジャーペンタトニックの鍵だけ青く塗っておく（スケール表示）
#
# IN / OUT は MIDI Devices 画面の Prog(input) 列 / Prog(output) 行でいつでも選び直せる。
# 外部の鍵盤の音も鳴らしたいときは、同じ画面で IN → 音源をパッチする。
#
# Pads 画面の Oct- / Oct+ で音域を動かす。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in  ||= MIDIDevices.usb_midi_host
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

keys = UI::Keys.new(octaves: 2, low: 48,      # C3〜C5（25 鍵）
                    velocity: :position,      # 鍵の奥ほど弱く、手前ほど強く
                    device: md.midi_out,
                    light: :orange)           # 外から来たノートの色
keys.channel_color(9, :red)   # ch 10（ドラム）は赤く光らせる

# スケール表示: 各オクターブの C D E G A
pentatonic = [0, 2, 4, 7, 9]
(0..127).each do |n|
  keys.set_color(n, UI.rgb(40, 80, 200)) if pentatonic.include?(n % 12)
end

# IN → 画面の鍵盤（光らせる）。IN を選び直したら張り直す
lit_by = nil
follow_in = Proc.new do
  MIDI.unroute(lit_by, keys) if lit_by
  lit_by = md.midi_in
  MIDI.route(lit_by, keys) if lit_by
end
follow_in.call

md.on_change do |e|
  case e[:kind]
  when :select_in  then follow_in.call
  when :select_out then keys.device = md.midi_out
  end
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
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
