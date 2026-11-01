# keys.rb
#
# 画面の鍵盤（UI::Keys、docs/KEYS.md）を弾く。鍵盤は picoruby-midi の
# トランスポートでもあるので、MIDI.route の送信元にも宛先にもなる。
#
#   - タッチで弾いた音: スクリプトの出力（MIDI Devices 画面の Script）へ C 側から直接。
#     Script から何もパッチされていなければ AMY / SAM2695 / PC のどれかへつなぐ
#   - 外部の鍵盤（USB-MIDI Host）で弾いた音: 画面の鍵盤を光らせる
#     （光る鍵盤のように「いま何が押されているか」を表示）
#   - C メジャーペンタトニックの鍵だけ青く塗っておく（スケール表示）
#
# 行き先・光らせる元は MIDI Devices 画面で Script の行 / Keys の列を張り替えれば変わる。
# 外部の鍵盤の音も鳴らしたいときは、同じ画面で鍵盤 → 音源をパッチする。
#
# Pads 画面の Oct- / Oct+ で音域を動かす。

require 'midi'
require 'ui'

md = UI::MIDIDevices

# Script から何もパッチされていなければここでつなぐ（画面で張り替えられる）
md.midi_out ||= MIDIDevices.amy || MIDIDevices.sam2695 || MIDIDevices.usb_midi_device
unless md.midi_out
  puts "No MIDI output available"
  return
end

keys = UI::Keys.new(octaves: 2, low: 48,      # C3〜C5（25 鍵）
                    velocity: :position,      # 鍵の奥ほど弱く、手前ほど強く
                    device: md.output,        # Script: そのパッチの先で鳴る
                    light: :orange)           # 外から来たノートの色
keys.channel_color(9, :red)   # ch 10（ドラム）は赤く光らせる

# スケール表示: 各オクターブの C D E G A
pentatonic = [0, 2, 4, 7, 9]
(0..127).each do |n|
  keys.set_color(n, UI.rgb(40, 80, 200)) if pentatonic.include?(n % 12)
end

# 外部の鍵盤 → 画面の鍵盤（光らせる）
MIDI.route(MIDIDevices.usb_midi_host, keys) if MIDIDevices.usb_midi_host

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
