# lumi_sysex.rb -- LUMI Keys の SysEx ヘルパー（filters/lumi.rb が load する）
#
# examples/midi_to_lumi.rb と同じもの。プロトコルは benob/LUMI-lights の解析による:
#   https://github.com/benob/LUMI-lights
# プロトコル: F0 00 21 10 77 37 [8バイトコマンド] [チェックサム] F7
# コマンドはLSBファースト7ビットパック形式

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
