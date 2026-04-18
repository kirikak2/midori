# midi_to_lumi.rb
# SAM2695からのMIDI入力を受け取り、USB-MIDI接続のLUMI Keysに
# 対応するキーを光らせる信号を送るスクリプト
#
# LUMI Keys SysExプロトコルは benob/LUMI-lights により解析済み:
#   https://github.com/benob/LUMI-lights

require 'midi'
require 'ui'

sam = MIDIDevices.sam2695
usb = MIDIDevices.usb_midi_host

unless sam && usb
  puts "Error: SAM2695 and USB-MIDI host are both required"
  exit
end

lumi       = MIDI::Device.new(usb)
sam_device = MIDI::Device.new(sam)

# =============================================================================
# LUMI Keys SysEx ヘルパー
# プロトコル: F0 00 21 10 77 37 [8バイトコマンド] [チェックサム] F7
# コマンドはLSBファースト7ビットパック形式
# =============================================================================

class LumiBitArray
  def initialize
    @values = []
    @num_bits = 0
  end

  def append(value, size = 7)
    used_bits = @num_bits % 7
    packed = used_bits > 0 ? @values.pop : 0
    @num_bits += size
    while size > 0
      packed = packed | ((value << used_bits) & 0x7F)
      bits_written = 7 - used_bits
      size -= bits_written
      value = value >> bits_written
      @values << packed
      packed = 0
      used_bits = 0
    end
  end

  def get
    @values << 0 while @values.length < 8
    @values
  end
end

module Lumi
  HDR      = [0xF0, 0x00, 0x21, 0x10, 0x77, 0x37]
  END_BYTE = [0xF7]

  def self.checksum(bytes)
    c = bytes.length
    bytes.each { |b| c = (c * 3 + b) & 0xFF }
    c & 0x7F
  end

  def self.send_cmd(device, values)
    cs = checksum(values)
    device.send_sysex(HDR + values + [cs] + END_BYTE)
  end

  # LEDカラー設定: id=0 スケール音色、id=1 ルート音色 (r,g,b: 0-255)
  def self.set_color(device, id, r, g, b)
    bits = LumiBitArray.new
    bits.append(0x10)
    bits.append(0x20 + 0x10 * (id & 1))
    bits.append(0b00100, 5)
    bits.append(b & 0xFF, 8)
    bits.append(g & 0xFF, 8)
    bits.append(r & 0xFF, 8)
    bits.append(0xFF, 8)
    send_cmd(device, bits.get)
  end

  # 輝度設定 (0-100)
  def self.set_brightness(device, value)
    bits = LumiBitArray.new
    bits.append(0x10)
    bits.append(0x40)
    bits.append(0b00100, 5)
    bits.append(value & 0x7F)
    send_cmd(device, bits.get)
  end

  # カラーモード: 0=レインボー、1=スケール色、2=ピアノ(白黒)、3=ナイト
  def self.set_color_mode(device, mode)
    bits = LumiBitArray.new
    bits.append(0x10)
    bits.append(0x40)
    bits.append(0b00010, 5)
    bits.append(mode & 3, 2)
    send_cmd(device, bits.get)
  end

  SCALE_CMDS = {
    chromatic:        [0x10, 0x60, 0x42, 0x04, 0x00, 0x00, 0x00, 0x00],
    major:            [0x10, 0x60, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00],
    minor:            [0x10, 0x60, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00],
    harmonic_minor:   [0x10, 0x60, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00],
    pentatonic_major: [0x10, 0x60, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00],
    pentatonic_minor: [0x10, 0x60, 0x22, 0x01, 0x00, 0x00, 0x00, 0x00],
    blues:            [0x10, 0x60, 0x42, 0x01, 0x00, 0x00, 0x00, 0x00],
    dorian:           [0x10, 0x60, 0x62, 0x01, 0x00, 0x00, 0x00, 0x00],
    whole_tone:       [0x10, 0x60, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00],
  }

  def self.set_scale(device, name)
    cmd = SCALE_CMDS[name]
    send_cmd(device, cmd) if cmd
  end
end

# =============================================================================
# LUMI Keys 初期設定
# =============================================================================
Lumi.set_color_mode(lumi, 1)            # スケール色モード
Lumi.set_color(lumi, 0,   0, 150, 255) # 青: スケール音
Lumi.set_color(lumi, 1, 255,  80,   0) # 橙: ルート音
Lumi.set_scale(lumi, :chromatic)        # 全ノート対象
Lumi.set_brightness(lumi, 80)

puts "LUMI Keys initialized. Forwarding SAM2695 MIDI input..."

# =============================================================================
# SAM2695 MIDI入力 → LUMI Keys 転送
# =============================================================================
sam_input = MIDI::Input.new(sam_device)

sam_input.on(:note_on) do |event|
  lumi.note_on(event[:note], event[:velocity], channel: event[:channel])
end

sam_input.on(:note_off) do |event|
  lumi.note_off(event[:note], 0, channel: event[:channel])
end

# =============================================================================
# アニメーション定数
# =============================================================================
WAVE_LOW  = 48   # C2: ウェーブの最低音
WAVE_HIGH = 96   # C6: ウェーブの最高音

# Pad3 静かなウェーブのパラメータ
# 5クロック/ステップ = 104ms @120bpm → 48音で約5秒/往路
RIPPLE_STEP = 3 
RIPPLE_DUR  = 450  # note_on持続ms: 4ステップ分重なる＝柔らかいグロー
RIPPLE_VEL  = 45   # 低ベロシティ＝控えめな光

# =============================================================================
# UI パッド
# =============================================================================

# Pad1: 下から上へ高速ウェーブ (Trigger)
# 約13ms/音 × 49音 = 0.6秒のスウィープ、各音100ms点灯で重なり感
UI.pad(1, label: "Wave Up", color: :cyan, type: :trigger) do
  note = WAVE_LOW
  while note <= WAVE_HIGH
    lumi.trigger(note, 110, duration: 100)
    MIDI.sleep_ms(13)
    note += 1
  end
end

# Pad2: 上から下へ高速ウェーブ (Trigger)
UI.pad(2, label: "Wave Dn", color: :orange, type: :trigger) do
  note = WAVE_HIGH
  while note >= WAVE_LOW
    lumi.trigger(note, 110, duration: 100)
    MIDI.sleep_ms(13)
    note -= 1
  end
end

# Pad3: 静かなウェーブ (Toggle)
# bpm_loopのクロックカウンタで音域をping-pong移動
# ON時は全ノートオフしてから開始、OFF時は残音を消灯
ripple_active = false

UI.pad(3, label: "Ripple", color: :green, type: :toggle) do |state|
  ripple_active = state
  unless ripple_active
    n = WAVE_LOW
    while n <= WAVE_HIGH
      lumi.note_off(n)
      n += 1
    end
  end
end

# =============================================================================
# メインループ
# =============================================================================
# output指定なし → subdivisions: 24 で約20ms間隔ループ
# on_loop: UI.process でパッド入力を処理
on_loop = Proc.new { UI.process }

MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  next unless ripple_active

  # RIPPLE_STEPクロック毎に1音ずつ移動
  next unless (c % RIPPLE_STEP) == 0

  # ping-pong: 0→range→0→... の折り返しウェーブ
  range = WAVE_HIGH - WAVE_LOW     # 48
  step  = (c / RIPPLE_STEP) % (range * 2)
  pos   = step > range ? (range * 2 - step) : step
  note  = WAVE_LOW + pos

  lumi.trigger(note, RIPPLE_VEL, duration: RIPPLE_DUR)
end
