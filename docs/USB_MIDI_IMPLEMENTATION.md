# USB MIDI Implementation Notes

このドキュメントは、ESP32-S3 USB Host MIDI実装における技術的知見をまとめたものです。

## アーキテクチャ概要

### タスク構成

| タスク名 | コア | 優先度 | スタック | 役割 |
|----------|------|--------|----------|------|
| `app_main` | Core 0 | 1 | 3584 | USB Host Libイベントループ（メインタスク） |
| `class_driver_task` | Core 0 | 1 | 4096 | USB Hostイベント処理、MIDI IN/OUT転送 |
| `sam2695_input` | Core 0 | 1 | 4096 | SAM2695 UART RX処理 |
| `picoruby_task` | Core 1 | 3 | 16384 | PicoRuby VM実行 |
| `midi_input_task` | Core 1 | 1 | 4096 | USB-MIDI RXバッファからイベントキューへの変換 |
| `Tmr Svc` | Any | 1 | 2048 | FreeRTOSタイマーサービス（システム） |

**注:** SAM2695へのMIDI送信は`uart_write_bytes`で直接行い、受信は`sam2695_input`タスクがUARTイベントを監視する。

### 優先度設計ガイドライン

ESP-IDF/FreeRTOSでは優先度0～24が使用可能（数値が大きいほど高優先度）。

#### 優先度の基本原則

1. **リアルタイム性が必要なタスクを高優先度に**
   - USBやUART等のハードウェアI/Oは、バッファオーバーフローを防ぐため比較的高い優先度が必要

2. **UIをブロックしないよう注意**
   - UI描画やユーザー入力処理は、ユーザー体験のため適度な優先度が必要
   - 低優先度タスクがUIをブロックしないよう、定期的に`vTaskDelay`や`taskYIELD`を呼ぶ

3. **CPUバウンドなタスクは低優先度に**
   - 長時間CPUを占有するタスク（スクリプト実行等）は低めに設定
   - ただしCore分離で対処する方が望ましい場合もある

4. **同一コアでの優先度競合を避ける**
   - 同じコアに割り当てられたタスク間で、高優先度タスクが低優先度タスクをスターブさせないよう注意

#### 推奨優先度レベル

| 優先度 | 用途 | 例 |
|--------|------|-----|
| 10+ | ハードウェア割り込み相当 | 緊急I/O、高頻度受信 |
| 5-9 | リアルタイムI/O | USB転送処理 |
| 2-4 | アプリケーション処理 | スクリプト実行、UI更新 |
| 1 | バックグラウンド処理 | ログ出力、統計収集 |
| 0 | アイドル相当 | 優先度最低のポーリング |

#### 現在の設計における考慮点

```
Core 0: ハードウェアI/O処理
├── class_driver_task (優先度 1) - USB enumeration、転送管理
├── sam2695_input (優先度 1) - SAM2695 UART RX処理
└── app_main (優先度 1) - USB Host Libイベントループ

Core 1: アプリケーション処理
├── picoruby_task (優先度 3) - Rubyスクリプト実行
└── midi_input_task (優先度 1) - USB-MIDIパケットパース
```

**設計理由:**
- `class_driver_task`と`sam2695_input`を`app_main`と同じ優先度1で動作（優先度を上げるとUIがフリーズする）
- `midi_input_task`はCore 1に配置し、イベント消費者（PicoRuby）と同じコアで動作させることでキャッシュ効率を向上
- Core 0はハードウェアI/O専用、Core 1はデータ処理・アプリケーション用に役割分離
- UI機能を追加する場合は、Core 1で`picoruby_task`と同等またはやや高い優先度で実行

#### 備考: SAM2695との通信

SAM2695（MIDIシンセサイザー）との通信:
- **送信**: `uart_write_bytes`で同期的に送信（`picoruby_task`から直接呼び出し）
- **受信**: `sam2695_input`タスク（Core 0、優先度1）がUARTイベントを監視

受信タスクは`class_driver_task`と同じ優先度・コアに配置。

### データフロー

```
[USB MIDI Device]
       |
       v
[USB Host Library] -- usb_host_client_handle_events()
       |
       v
[midi_in_transfer_callback] -- g_in_transfer（継続的にresubmit）
       |
       v
[USB_MIDI_push_rx_data] -- Ring Buffer (256 bytes)
       |
       v
[midi_input_task] -- パケットパース
       |
       v
[g_event_queue] -- FreeRTOS Queue (256 events)
       |
       v
[PicoRuby MIDI::Input] -- イベントハンドラ呼び出し
```

## 重要な発見と解決策

### 問題1: OUT転送がIN転送をブロックする

#### 症状
- PicoRubyのapp.rbループでMIDI OUT（note_on/note_off）を送信中
- MIDI IN callbackが一切呼ばれない（`IN_callbacks=0`）
- デバイス再接続後（ループ停止）はMIDI INが正常動作

#### 原因
`process_midi_tx_queue()`が1回の呼び出しで最大16個のOUT転送を連続submitしていた。これによりUSB Hostライブラリが飽和し、IN転送のcallbackが処理されなくなった。

#### 解決策
OUT転送を**1パケットずつ**に制限：

```c
static volatile bool g_tx_pending = false;

static void process_midi_tx_queue(class_driver_t *driver_obj)
{
    // 既にOUT転送が処理中なら何もしない
    if (g_tx_pending) {
        return;
    }

    uint8_t packet[4];
    if (USB_MIDI_pop_tx_packet(packet)) {
        // ... transfer setup ...
        g_tx_pending = true;
        usb_host_transfer_submit(transfer);
    }
}

static void midi_out_transfer_callback(usb_transfer_t *transfer)
{
    g_tx_pending = false;  // 次のOUT転送を許可
    usb_host_transfer_free(transfer);
}
```

### 問題2: MIDI Clockによるキュー溢れ

#### 症状
- MIDI Clockメッセージ（0xF8）が四分音符あたり24回送信される
- テンポ120 BPMで毎秒48メッセージ
- イベントキューがすぐに埋まる

#### 解決策
MIDI ClockとActive Sensingを無視：

```c
case 0x0F: /* Single Byte (Realtime) */
    switch (midi1) {
        case 0xF8:
            /* Ignore MIDI clock - too frequent */
            return false;
        case 0xFE:
            /* Ignore Active Sensing - sent every ~300ms */
            return false;
        // Start/Stop/Continueは処理する
    }
```

### 問題3: デバイス再接続時の自動復旧

#### 症状
- デバイス切断→再接続後、MIDI Inputタスクが自動再開しない
- 手動でアプリを再起動する必要がある

#### 解決策
`g_input_was_started`フラグで初回起動を記録し、再接続時に自動再開：

```c
// midi.c
static volatile bool g_input_was_started = false;

int MIDI_Input_start(void)
{
    // ... task creation ...
    g_input_was_started = true;
    return 0;
}

// usb_midi.c
void USB_MIDI_notify_connected(const usb_midi_device_info_t *info)
{
    // ... connection handling ...

    if (MIDI_Input_was_started() && !MIDI_Input_is_running()) {
        ESP_LOGI(TAG, "Auto-restarting MIDI input task on reconnect");
        MIDI_Input_start();
    }
}
```

## USB-MIDIパケットフォーマット

USB-MIDIは4バイトパケット形式を使用：

```
Byte 0: [Cable Number (4bit)][Code Index Number (4bit)]
Byte 1: MIDI Status byte
Byte 2: MIDI Data byte 1
Byte 3: MIDI Data byte 2
```

### Code Index Number (CIN)

| CIN | メッセージタイプ |
|-----|------------------|
| 0x08 | Note Off |
| 0x09 | Note On |
| 0x0A | Poly Aftertouch |
| 0x0B | Control Change |
| 0x0C | Program Change |
| 0x0D | Channel Pressure |
| 0x0E | Pitch Bend |
| 0x0F | Single Byte (System Realtime) |

## デバッグ用ログ

### ステータスモニタリング

5秒ごとに以下のログが出力される：

```
I (xxxxx) USB_HOST_SAMPLE: Status: in_transfer=0x3c1b15d0, TX_total=142, IN_callbacks=500 (delta=100)
```

- `in_transfer`: MIDI IN転送ハンドル（NULLなら転送未設定）
- `TX_total`: 累計OUT転送数
- `IN_callbacks`: 累計IN callback呼び出し回数
- `delta`: 前回ログからのIN callback増分

### 正常動作の指標

- `delta > 0`: MIDI INが正常に受信されている
- `TX_total`増加中に`delta > 0`: IN/OUT同時動作OK

## PicoRuby APIリファレンス

### MIDI.bpm_loop(bpm, output:, subdivisions:, send_start:, sync:, input:)

BPMに基づいたタイミングでループを実行。`loop do end` の代わりに使用することで、FreeRTOSのタスク切り替えを確保。

`output`を指定すると、MIDI Clock（24 PPQ）を自動送信。
`sync: true`と`input:`を指定すると、外部MIDI Clockに同期。

**パラメータ:**
- `bpm` - BPM（デフォルト: 120）
- `output:` - MIDI出力デバイス（Clock送信用、省略可）
- `subdivisions:` - ブロック実行間隔（1=四分音符、2=8分音符、4=16分音符）
- `send_start:` - ループ開始時にMIDI Startを送信（デフォルト: true）
- `sync:` - 外部MIDI Clockに同期（デフォルト: false）
- `input:` - 同期用のMIDI入力（sync: true時に必要）

```ruby
# 120 BPMでMIDI Clock送信しつつループ
MIDI.bpm_loop(120, output: device) do
  device.note_on(60, 100)
  MIDI.sleep_ms(100)
  device.note_off(60)
end

# MIDI Clockなし（タスク切り替えのみ）
MIDI.bpm_loop(120) do
  # 処理
end

# 8分音符ごとにブロック実行（Clockは24 PPQで送信）
MIDI.bpm_loop(120, output: device, subdivisions: 2) do
  # 8分音符ごとに実行
end

# 外部MIDI Clockに同期（BPMは自動追従）
MIDI.bpm_loop(120, output: device, sync: true, input: input) do
  # 外部ClockのBPMに追従
end
```

### MIDI::Input#external_bpm

外部MIDI Clockから検出したBPMを取得。24クロック（1拍分）の平均間隔から計算。

```ruby
input = MIDI::Input.new(device)
bpm = input.external_bpm  # 0.0 if not enough data
```

### MIDI.external_bpm

最初のアクティブな入力からの外部BPMを取得（簡易アクセス）。

```ruby
bpm = MIDI.external_bpm
```

### MIDI.run_for(duration_ms, interval_ms: 10)

指定した時間だけループを実行。

```ruby
# 5秒間、10msごとにループ
MIDI.run_for(5000) do
  # 処理
end
```

### MIDI.sleep_ms(ms)

タスク切り替えを発生させつつスリープ。MIDIイベントも自動処理。

```ruby
MIDI.sleep_ms(500)  # 500ms待機、MIDI入力も処理
```

**重要**: PicoRuby内で `loop do end` を使用すると、FreeRTOSのタスク切り替えが発生せず、Core 0のUSBタスクに影響を与える可能性があります。代わりに `MIDI.bpm_loop` または `MIDI.sleep_ms` を使用してください。

## 既知の制限事項

1. **MIDI Clockイベントは非キュー**: 頻度が高いためイベントキューには送られないが、BPM検出には使用される
2. **Active Sensing非対応**: 無視
3. **SysEx非対応**: 現在の実装では未対応
4. **複数デバイス非対応**: 1デバイスのみサポート

## USB-MIDI Device (Tab5 / CoreS3 / Freenove)

上記の USB **Host** とは別に、本機自身が USB **Device** として振る舞える。
どのポートがどの役割になるかは USBポートモード（`./switch_board.sh <board> midi_device`、
Kconfig の `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE`）で決まる。

| ボード | Device ポート | 同時に Host |
|-------|--------------|------------|
| M5Stack Tab5 (ESP32-P4) | USB-C | ○（USB-A / 別 PHY） |
| M5Stack CoreS3 / Freenove (ESP32-S3) | USB-C（唯一のコネクタ） | ✕（PHY を共有） |

### 構成

- **TinyUSB** による **CDC + MIDI コンポジットデバイス**。PC 側には
  `Midori MIDI`（VID:PID `303a:4009`）として列挙される（CDC はシリアルコンソール兼用）。
- **ESP32-P4** は USB PHY を 2 つ持つ。既定では USB-Serial-JTAG が USB-C 側 PHY に繋がっているため、
  起動時に PHY の mux を切り替えて USB-OTG(FS) を USB-C 側へ割り当てている
  （`usb_serial_jtag_ll_phy_select(1)`。このコードは `CONFIG_IDF_TARGET_ESP32P4` 限定）。
- **ESP32-S3** は USB-Serial-JTAG と USB-OTG が PHY を 1 つ共有する。PHY の引き渡しは
  ESP-IDF の `usb_phy` ドライバ（`tinyusb_driver_install()` 内）が行うので追加コードは不要。
  ただし USB-OTG を Device として使うため、**USB-MIDI Host は同時に使えない**
  （`CONFIG_USB_MIDI_HOST_ENABLED` が n、`BoardConfig::HAS_USB_MIDI_HOST` が false になる）。
- いずれの場合も、この時点で USB-Serial-JTAG は切断される（書き込みは BOOT + RESET で
  ダウンロードモードに入る必要があり、JTAG デバッグは使えない）。コンソール出力は
  `tinyusb_console_init()` で CDC 側へリダイレクトされる。
- ボード設定 `HAS_USB_MIDI_DEVICE`（= `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE`）で
  ビルドに含まれる。

### TX（device → host）の直列化 — 重要

`tud_midi_packet_write()` は TinyUSB device task（`tud_task`, Core 1）と**同一コアで**呼ばないと
usbd/FIFO 状態を壊す（別コア並列で VM ヒープ破壊クラッシュを観測）。MIDI 送信は
複数の文脈（VM task=Core 1、esp_timer のノートスケジューラ/クロック=Core 0）から要求されるため、
**全送信を FreeRTOS キューへ積み、Core 1 に固定した専用タスク 1 本だけが
`tud_midi_packet_write()` を呼ぶ**構成にしてある。これにより要求元のコアに依らず tud_task と直列化される。

### RX（host → device）

`tud_midi_rx_cb()`（TinyUSB task）が 4 バイトパケットを SPSC リングバッファへ積み、
Ruby task が `USB_MIDI_DEVICE_read_packet()` で取り出す。

### Ruby からの利用

`MIDIDevices.usb_midi_device`（遅延初期化で `require 'usb_midi_device'`）→ `MIDI::Device.new` の
transport として使う。picoruby-midi の transport-mask では USB device = `0x04`。
API 一覧は [MIDI_DEVICES.md](MIDI_DEVICES.md) を参照。

### 関連ファイル（Device 側）

- `mrbgems/picoruby-usb_midi_device/ports/esp32/usb_midi_device.c`:
  TinyUSB 初期化、TX キュー + Core-1 送信タスク、RX リングバッファ
- `mrbgems/picoruby-usb_midi_device/src/mrubyc/usb_midi_device.c`:
  mruby/c バインディング（`USB_MIDI_DEVICE` クラス）
- `mrbgems/picoruby-usb_midi_device/mrblib/usb_midi_device.rb`: Ruby API
- `examples/usb_midi_device_pad.rb`: 動作確認サンプル

## 関連ファイル

- `main/usb_midi_host.c`: USB Hostドライバ、転送処理
- `mrbgems/picoruby-midi/ports/esp32/midi.c`: MIDI入力タスク、イベントパース
- `mrbgems/picoruby-usb_midi_host/ports/esp32/usb_midi_host.c`: USB-MIDIブリッジ（TX Queue, RX Ring Buffer）
- `mrbgems/picoruby-uart_midi/ports/esp32/uart_midi.c`: UART MIDI（SAM2695）通信、入力タスク
- `mrbgems/picoruby-midi/mrblib/midi_input.rb`: Ruby側のMIDI入力API
