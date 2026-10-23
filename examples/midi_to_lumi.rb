# midi_to_lumi.rb
# MIDI 入力（MIDI Devices 画面で選んだ IN、未選択なら SAM2695 の MIDI IN）を
# 受け取り、LUMI Keys（選んだ OUT、未選択なら USB-MIDI Host）の対応する
# キーを光らせる信号を送るスクリプト
#
# LUMI Keys SysExプロトコルは benob/LUMI-lights により解析済み:
#   https://github.com/benob/LUMI-lights

require 'midi'
require 'ui'

md = UI::MIDIDevices

# 未選択ならここで決める（画面で選び直せる）
md.midi_in  ||= MIDIDevices.sam2695
md.midi_out ||= MIDIDevices.usb_midi_host

# LUMI Keys = OUT。md.output は選択に追従するので、実行中に OUT を選び直すと
# そちらへ送る（md.midi_out はその時点の機器のまま変わらない）
lumi = md.output
unless md.midi_in && md.midi_out
  puts "Error: select the MIDI IN and the LUMI Keys (OUT) on the MIDI Devices screen"
  return
end

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
setup_lumi = Proc.new do
  Lumi.set_color_mode(lumi, 1)            # スケール色モード
  Lumi.set_color(lumi, 0,   0, 150, 255) # 青: スケール音
  Lumi.set_color(lumi, 1, 255,  80,   0) # 橙: ルート音
  Lumi.set_scale(lumi, :chromatic)        # 全ノート対象
  Lumi.set_brightness(lumi, 80)
end
setup_lumi.call

# OUT を選び直したら、新しい LUMI Keys も設定し直す
md.on_change { |e| setup_lumi.call if e[:kind] == :select_out }

puts "LUMI Keys initialized. Forwarding #{md.midi_in.info} MIDI input..."

# =============================================================================
# MIDI入力 → LUMI Keys 転送
# md.input は選んだ IN に追従する（画面で IN を選び直してもハンドラはそのまま）
# =============================================================================
md.input.on(:note_on) do |m|
  lumi.note_on(m.note, m.velocity, channel: m.channel)
end

md.input.on(:note_off) do |m|
  lumi.note_off(m.note, 0, channel: m.channel)
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
# MIDI.start! のクロックカウンタで音域をping-pong移動
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
# output指定なし（クロックは送らない）→ subdivisions: 24 で約20ms間隔ループ
# Midori の MIDI.start! は毎回 UI.process を呼ぶのでパッド入力も処理される
MIDI.start!(bpm: 120, subdivisions: 24) do |c|
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
