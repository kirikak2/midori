# USB MIDI Implementation Notes

このドキュメントは、ESP32-S3 USB Host MIDI実装における技術的知見をまとめたものです。

## アーキテクチャ概要

### タスク構成

| タスク名 | コア | 優先度 | 役割 |
|----------|------|--------|------|
| `class_driver_task` | Core 0 | 2 | USB Hostイベント処理、MIDI IN/OUT転送 |
| `midi_input_task` | Core 0 | 2 | RXバッファからイベントキューへの変換 |
| `picoruby_task` | Core 1 | 1 | mrubyc VM実行 |
| `app_main` | - | - | USB Host Libイベントループ |

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

## 関連ファイル

- `main/usb_midi_host.c`: USB Hostドライバ、転送処理
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/ports/esp32/midi.c`: MIDI入力タスク、イベントパース
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-usb_midi/ports/esp32/usb_midi.c`: USB-MIDIブリッジ（TX Queue, RX Ring Buffer）
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/mrblib/midi_input.rb`: Ruby側のMIDI入力API
