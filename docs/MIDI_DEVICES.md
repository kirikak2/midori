# MIDIデバイス初期化ガイド

## 概要

Midoriでは、ボードごとに利用可能なMIDIデバイスとピンアサインが異なります。
バージョン2026-04-11以降、MIDIデバイスはボード設定に基づいて自動的に初期化され、
SDカード内のスクリプトから簡単にアクセスできるようになりました。

## ボード別の利用可能デバイス

| ボード名 | SAM2695 | USB-MIDI Host | USB-MIDI Device |
|---------|---------|---------------|-----------------|
| m5stack (CoreS3) | ○ (17,18) | ○ | - |
| m5stack_with_usbserial | ○ (17,18) | - | - |
| freenove (ESP32-S3) | ○ (17,18) | ○ | - |
| m5stack_tab5 (P4) | ○ (53,54 / Port A) | ○ | ○ (未実装) |

## 推奨: MIDIDevicesモジュールを使用する方法

### メリット
- ボードごとのピンアサインを意識する必要がない
- 本体側で初期化済みのデバイスを使用するため、効率的
- ボードを変更してもスクリプトの修正が不要

### 使用例

```ruby
require 'midi'
require 'ui'

# MIDIDevicesモジュールから初期化済みのデバイスを取得
sam = MIDIDevices.sam2695
usb = MIDIDevices.usb_midi_host

# nilチェック（ボードによっては利用不可の場合がある）
if sam
  device = MIDI::Device.new(sam)
  device.program_change(1, channel: 9)

  UI.pad(1, label: "Kick", color: :red, type: :trigger) do
    device.trigger(36, 127, channel: 9, duration: 100)
  end
else
  puts "SAM2695 not available on this board"
end

# USB-MIDIを使う場合
if usb
  usb_device = MIDI::Device.new(usb)
  # ... USB-MIDIデバイスを使用
else
  puts "USB-MIDI Host not available on this board"
end
```

## 従来の方法（互換性のため残存）

従来通り、直接デバイスを初期化することも可能です。
ただし、ボードごとにピンアサインが異なるため、スクリプトの移植性が低下します。

```ruby
require 'midi'
require 'sam2695'

# M5Stack/Freenoveの場合（ピン17,18）
sam = SAM2695.new(17, 18)

# M5Stack Tab5の場合（Port Aピン: TX=53, RX=54）
# sam = SAM2695.new(53, 54)

device = MIDI::Device.new(sam)
```

## ボード情報の確認

スクリプト内でボード情報を確認する方法：

```ruby
# ボード名を取得
puts "Board: #{BoardConfig::BOARD_NAME}"

# 利用可能なMIDIデバイスを確認
puts "SAM2695: #{BoardConfig::HAS_SAM2695 ? 'available' : 'not available'}"
puts "USB-MIDI Host: #{BoardConfig::HAS_USB_MIDI_HOST ? 'available' : 'not available'}"
puts "USB-MIDI Device: #{BoardConfig::HAS_USB_MIDI_DEVICE ? 'available' : 'not available'}"

# SAM2695のピン設定を確認
if BoardConfig::HAS_SAM2695
  puts "SAM2695 TX: #{BoardConfig::SAM2695_TX_PIN}"
  puts "SAM2695 RX: #{BoardConfig::SAM2695_RX_PIN}"
end
```

## 実装の詳細

### 本体側の初期化タイミング

MIDIデバイスは以下のタイミングで初期化されます：

1. **UI Mode**: 起動時、SD カード初期化の後
2. **Script Mode**: スクリプト実行前

### 初期化コード

MIDIDevicesモジュールの初期化は `main_task_base.rb` で以下のように行われます：

```ruby
# UI Mode/Script Mode共通の初期化処理
MIDIDevices.init_sam2695
MIDIDevices.init_usb_midi_host
```

### BoardConfig の生成

ボード設定は `board_config.rb.in` テンプレートから CMake によって自動生成されます：

- **ソースファイル**: `components/picoruby-esp32/mrblib/board_config.rb.in`
- **生成ファイル**: `components/picoruby-esp32/mrblib/board_config.rb`
- **設定元**: `components/picoruby-esp32/CMakeLists.txt` (Kconfig に基づく)

## トラブルシューティング

### デバイスが nil になる

MIDIDevicesモジュールから取得したデバイスが nil の場合：

1. ボードがそのデバイスに対応しているか確認
2. `BoardConfig::HAS_SAM2695` などで利用可能性を確認
3. ログを確認（起動時に "SAM2695 initialized" などのメッセージが表示される）

### ピンアサインを変更したい

ハードウェアのピンアサインを変更する場合：

1. `components/picoruby-esp32/CMakeLists.txt` を編集
2. 該当するボードの設定セクションで `SAM2695_TX_PIN` などを変更
3. リビルド: `idf.py build`

## 今後の予定

- **USB-MIDI Device対応**: M5Stack Tab5でのUSB-MIDI Device機能実装
- **動的デバイス切り替え**: スクリプト実行中のデバイス切り替え機能
