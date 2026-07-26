# MIDIデバイス初期化ガイド

## 概要

Midoriでは、ボードごとに利用可能なMIDIデバイスとピンアサインが異なります。
バージョン2026-04-11以降、MIDIデバイスはボード設定に基づいて自動的に初期化され、
SDカード内のスクリプトから簡単にアクセスできるようになりました。

## ボード別の利用可能デバイス

利用可能なデバイスは **ボード × USBポートモード** の組み合わせで決まります。
USBポートモードは `./switch_board.sh <board> [host|serial|midi_device]` で選択します。

| ボード名 | USBモード | SAM2695 | USB-MIDI Host | USB-MIDI Device |
|---------|----------|---------|---------------|-----------------|
| m5stack (CoreS3) | host (既定) | ○ (17,18) | ○ (USB-C) | - |
| m5stack (CoreS3) | serial | ○ (17,18) | - | - |
| m5stack (CoreS3) | midi_device | ○ (17,18) | - | ○ (USB-C) |
| freenove (ESP32-S3) | host (既定) | ○ (17,18) | ○ (USB-C) | - |
| freenove (ESP32-S3) | serial | ○ (17,18) | - | - |
| freenove (ESP32-S3) | midi_device | ○ (17,18) | - | ○ (USB-C) |
| m5stack_tab5 (P4) | midi_device (既定) | ○ (53,54 / Port A) | ○ (USB-A) | ○ (USB-C) |
| m5stack_tab5 (P4) | serial | ○ (53,54 / Port A) | ○ (USB-A) | - |

> ESP32-S3ボードはUSBコネクタが1つ、USB PHYも1つしかないため、
> Host（USB-OTG）と Device（USB-Serial/JTAG もしくは TinyUSB）は排他です。
> `serial` / `midi_device` を選ぶと `BoardConfig::HAS_USB_MIDI_HOST` が
> `false` になり、`MIDIDevices.usb_midi_host` は `nil` を返します。
> Tab5 は USB-A が常に Host なので、モードは USB-C の役割のみを決めます。

> **USB-MIDI Host と USB-MIDI Device の違い**
> - **Host**: 本機に USB MIDI 機器（シンセ・キーボード等）を接続して制御する（本機がホスト）。
> - **Device**: 本機を PC 等のホストに接続し、**本機自身が USB MIDI デバイスとして振る舞う**。
>   PC 側には `Midori MIDI`（VID:PID `303a:4009`, CDC + MIDI コンポジット）として見える。
>   Tab5 では USB-A が Host、**USB-C が Device**（TinyUSB。詳細は
>   [USB_MIDI_IMPLEMENTATION.md](USB_MIDI_IMPLEMENTATION.md) の「USB-MIDI Device (Tab5)」節）。

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

# USB-MIDI Host を使う場合（本機に接続された MIDI 機器を制御）
if usb
  usb_device = MIDI::Device.new(usb)
  # ... USB-MIDIデバイスを使用
else
  puts "USB-MIDI Host not available on this board"
end

# USB-MIDI Device を使う場合（本機を PC 等のホストへ MIDI 出力）
usb_dev = MIDIDevices.usb_midi_device
if usb_dev
  device = MIDI::Device.new(usb_dev)
  # connected? で PC 側が本機を認識したか確認できる
  UI.log(usb_dev.connected? ? "Host connected" : "Host not connected yet")

  UI.pad(1, label: "C4", color: :red, type: :trigger) do
    device.trigger(60, 110, duration: 150)  # PC の MIDI IN に Note が届く
  end
else
  puts "USB-MIDI Device not available on this board"
end
```

サンプル: [`examples/usb_midi_device_pad.rb`](../examples/usb_midi_device_pad.rb)
（Tab5 の USB-C を PC に接続し、パッドのタップを MIDI Note として送信）。
PC 側の確認例: `aseqdump -p "Midori MIDI"`（Linux）。

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
# 各デバイスは初回アクセス時に遅延初期化される（lazy init）
MIDIDevices.sam2695          # -> init_sam2695
MIDIDevices.usb_midi_host    # -> init_usb_midi_host
MIDIDevices.usb_midi_device  # -> init_usb_midi_device（require 'usb_midi_device'）
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

## USB-MIDI Device の Ruby API

`MIDIDevices.usb_midi_device` が返すオブジェクトは `MIDI::Device.new` の transport として
使えるほか、以下の低レベル API を持ちます（通常は `MIDI::Device` 経由で使えば十分）:

| メソッド | 説明 |
|---|---|
| `connected?` | PC 等のホストが本機を enumerate 済みなら true |
| `send_packet(cable, cin, m1, m2, m3)` | USB-MIDI 4バイトパケットを host へ送信（0=成功 / -1=失敗） |
| `bytes_available` | ホストから受信済み（host→device）のバイト数 |
| `read_available` | 受信済み 4バイトパケットを binary String で返す（無ければ nil） |
| `transport_id` | picoruby-midi の transport マスク識別子（USB device = `4`） |

## 今後の予定

- **動的デバイス切り替え**: スクリプト実行中のデバイス切り替え機能
