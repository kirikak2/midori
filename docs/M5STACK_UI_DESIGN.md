# M5Stack LCD UI Design Document

M5Stack CoreS3 SEのLCDを活用した、ボタン操作による複数画面UIの設計ドキュメント。

## 概要

従来のログ出力のみのLCD表示から、6つの機能画面を持つインタラクティブなUIへ拡張する。タッチスクリーンのボタンエリア（画面下部）を使用して画面を切り替える。

### 画面構成

| 画面番号 | 名称 | 概要 |
|----------|------|------|
| 1 | メイン画面 | BPMのリアルタイム表示・変更 |
| 2 | パッド画面 | Rubyから割り当て可能な6ボタン |
| 3 | MIDI情報画面 | 接続デバイス一覧（USB/DIN/BLE） |
| 4 | ログ画面 | ESP32ログ・Rubyスクリプト出力 |
| 5 | スクリプト選択画面 | SDカード内のRubyファイル選択・実行 |
| 6 | 設定画面 | BLE-MIDIペアリング、システム設定 |

## 画面レイアウト共通仕様

```
┌────────────────────────────────────────┐
│  Status Bar (20px)        [MIDI●] [📶] │  ← 全画面共通
├────────────────────────────────────────┤
│                                        │
│                                        │
│           Content Area                 │  ← 画面固有コンテンツ
│             (180px)                    │
│                                        │
│                                        │
├────────────────────────────────────────┤
│   [◀]      [Page Title]        [▶]    │  ← ナビゲーションバー (40px)
└────────────────────────────────────────┘
```

### 画面サイズ定数

```c
// M5Stack CoreS3 SE: 320x240 LCD
#define SCREEN_WIDTH         320
#define SCREEN_HEIGHT        240

#define STATUS_BAR_HEIGHT     20
#define NAV_BAR_HEIGHT        40
#define CONTENT_HEIGHT       (SCREEN_HEIGHT - STATUS_BAR_HEIGHT - NAV_BAR_HEIGHT)  // 180px
#define CONTENT_Y            STATUS_BAR_HEIGHT
```

### ステータスバー

全画面共通で表示。右側にインジケータを配置。

| 要素 | 位置 | 説明 |
|------|------|------|
| ページタイトル | 左寄せ | 現在の画面名 |
| MIDIインジケータ | 右から30px | 緑=MIDI接続中、灰=未接続 |
| 接続数バッジ | 右から60px | 接続デバイス数（将来用） |

### ナビゲーションバー

タッチ操作で画面切り替え。3分割のタッチエリア。

| エリア | 幅 | アクション |
|--------|-----|------------|
| 左 (0-106px) | 1/3 | 前の画面へ |
| 中央 (107-213px) | 1/3 | 画面固有アクション / ページ名表示 |
| 右 (214-320px) | 1/3 | 次の画面へ |

## 画面詳細設計

### 1. メイン画面（Main Screen）

BPMのリアルタイム表示と変更を行うメイン画面。

```
┌────────────────────────────────────────┐
│  Main                         [MIDI●] │
├────────────────────────────────────────┤
│                                        │
│      [-10]  [-1]  ♩=120  [+1]  [+10]   │  ← BPM表示 + 調整ボタン
│                                        │
│         External: 120.5 BPM            │  ← 外部MIDI Clock BPM
│         Sync: [OFF]                    │  ← 外部同期モード切替
│                                        │
│    Bar: 42        Beat: 3              │  ← 小節/拍カウンタ
│    ████████████░░░░░░░░░░░░░░░         │  ← ビートインジケータ
│                                        │
├────────────────────────────────────────┤
│   [◀]         [TAP]  Main      [▶]    │  ← TAP Tempo
└────────────────────────────────────────┘
```

#### 表示要素

| 要素 | 更新頻度 | データソース |
|------|----------|--------------|
| BPM値（大） | 100ms | 設定BPM / 外部同期時は外部BPM |
| 外部BPM | 100ms | MIDI Clock検出値 |
| Syncトグル | タップ時 | 外部MIDI Clockに同期するか |
| 小節カウンタ (Bar) | 各小節 | MIDI Clock / 内部タイマー |
| 拍カウンタ (Beat) | 各拍 | 1〜4の循環 |
| ビートインジケータ | 各MIDI Clock | 1拍内の進捗（プログレスバー） |

#### BPM調整操作

| 操作 | アクション |
|------|------------|
| [-10] タップ | BPM -10 |
| [-1] タップ | BPM -1 |
| [+1] タップ | BPM +1 |
| [+10] タップ | BPM +10 |
| BPM値を長押し | 数値入力モード（将来実装） |
| [TAP] ボタン | タップテンポ（2回以上のタップ間隔からBPM算出） |

#### BPM設定の範囲

```c
#define BPM_MIN    20.0f
#define BPM_MAX   300.0f
#define BPM_DEFAULT 120.0f
```

#### 同期モード

| モード | 動作 |
|--------|------|
| Sync: OFF | 内部BPMを使用（手動設定） |
| Sync: ON | 外部MIDI Clockに追従（BPM調整ボタン無効化） |

外部MIDI Clockが検出されていない場合、Syncボタンはグレーアウト。

#### 小節/拍カウンタ

- **Bar**: 現在の小節番号（1から開始、MIDI Start/Stopでリセット）
- **Beat**: 現在の拍番号（1〜4、4/4拍子を想定）

#### ビートインジケータ（プログレスバー形式）

- 1拍内の進捗をプログレスバーで表示
- MIDI Clockの24 PPQから進捗を算出（0〜23 → 0〜100%）
- 拍の先頭でバーがリセットされ、拍の終わりで満タンになる

```
拍の開始: ░░░░░░░░░░░░░░░░░░░░░░░░  (0%)
拍の中間: ████████████░░░░░░░░░░░░  (50%)
拍の終了: ████████████████████████  (100%)
次の拍:   ░░░░░░░░░░░░░░░░░░░░░░░░  (リセット)
```

#### TAP Tempo

- 中央ボタン [TAP] を連続タップしてBPMを設定
- 直近4回のタップ間隔の平均からBPMを算出
- 2秒以上タップがない場合、計測リセット

```c
#define TAP_TEMPO_SAMPLES   4
#define TAP_TEMPO_TIMEOUT_MS 2000
```

### 2. パッド画面（Pad Screen）

Rubyスクリプトから自由に機能を割り当てられる6つのカスタムボタンを配置。

```
┌────────────────────────────────────────┐
│  Pads                         [MIDI●] │
├────────────────────────────────────────┤
│  ┌──────────┐ ┌──────────┐ ┌──────────┐│
│  │          │ │          │ │          ││
│  │  Pad 1   │ │  Pad 2   │ │  Pad 3   ││
│  │          │ │          │ │          ││
│  └──────────┘ └──────────┘ └──────────┘│
│  ┌──────────┐ ┌──────────┐ ┌──────────┐│
│  │          │ │          │ │          ││
│  │  Pad 4   │ │  Pad 5   │ │  Pad 6   ││
│  │          │ │          │ │          ││
│  └──────────┘ └──────────┘ └──────────┘│
├────────────────────────────────────────┤
│   [◀]          Pads            [▶]    │
└────────────────────────────────────────┘
```

#### レイアウト

- 2行 × 3列 = 6ボタン
- 各ボタン: 約95×75px（マージン含む）
- コンテンツエリア180pxに収まる設計

```c
#define PAD_COUNT       6
#define PAD_COLS        3
#define PAD_ROWS        2
#define PAD_WIDTH      95
#define PAD_HEIGHT     75
#define PAD_MARGIN      8
```

#### ボタン状態

| 状態 | 表示 | 説明 |
|------|------|------|
| 未割り当て | グレー背景 + "Pad N" | Rubyから未設定 |
| 割り当て済み | カスタム色 + ラベル | Rubyから設定済み |
| 押下中 | 明るい色 | タッチ中 |
| トグルON | アクセントカラー | トグルモードでON状態 |

#### ボタンタイプ

| タイプ | 動作 |
|--------|------|
| Momentary | 押している間だけON、離すとOFF |
| Toggle | タップでON/OFF切り替え |
| Trigger | タップ時に1回だけコールバック発火 |

#### Ruby API

詳細は「UI API（PicoRuby側）」セクションを参照。

```ruby
# 基本的な使用例
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  device.note_on(36, 127)
  MIDI.sleep_ms(100)
  device.note_off(36)
end
```

#### カラーパレット

```ruby
# 使用可能な色
:red, :green, :blue, :yellow, :cyan, :magenta, :orange, :purple, :white, :gray
```

```c
// C側の色定義
typedef enum {
    PAD_COLOR_RED     = 0xF800,
    PAD_COLOR_GREEN   = 0x07E0,
    PAD_COLOR_BLUE    = 0x001F,
    PAD_COLOR_YELLOW  = 0xFFE0,
    PAD_COLOR_CYAN    = 0x07FF,
    PAD_COLOR_MAGENTA = 0xF81F,
    PAD_COLOR_ORANGE  = 0xFD20,
    PAD_COLOR_PURPLE  = 0x8010,
    PAD_COLOR_WHITE   = 0xFFFF,
    PAD_COLOR_GRAY    = 0x8410,
} pad_color_t;
```

#### C側データ構造

```c
typedef enum {
    PAD_TYPE_TRIGGER,    // 1回発火
    PAD_TYPE_MOMENTARY,  // 押している間ON
    PAD_TYPE_TOGGLE,     // ON/OFF切り替え
} pad_type_t;

typedef struct {
    bool assigned;           // Rubyから設定済みか
    char label[16];          // 表示ラベル
    pad_color_t color;       // ボタン色
    pad_type_t type;         // ボタンタイプ
    bool state;              // 現在の状態（トグル/押下）
} pad_config_t;

extern pad_config_t g_pads[PAD_COUNT];
```

#### C API

```c
// パッド設定
void ui_pad_set(uint8_t index, const char* label, pad_color_t color, pad_type_t type);
void ui_pad_clear(uint8_t index);
void ui_pad_clear_all(void);

// 状態取得/設定
bool ui_pad_get_state(uint8_t index);
void ui_pad_set_state(uint8_t index, bool state);

// ラベル/色の動的更新
void ui_pad_set_label(uint8_t index, const char* label);
void ui_pad_set_color(uint8_t index, pad_color_t color);

// タッチイベントコールバック
typedef void (*pad_event_cb_t)(uint8_t index, bool pressed);
void ui_pad_set_callback(pad_event_cb_t cb);
```

### 3. MIDI情報画面（MIDI Info Screen）

接続中のMIDIデバイス情報を表示。

```
┌────────────────────────────────────────┐
│  MIDI Devices                 [MIDI●] │
├────────────────────────────────────────┤
│  ┌──────────────────────────────────┐  │
│  │ 🔌 USB-MIDI                      │  │
│  │   Roland J-6                     │  │
│  │   IN ● OUT ●                     │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │ 🎹 DIN-MIDI (SAM2695)           │  │
│  │   IN ○ OUT ●                     │  │
│  └──────────────────────────────────┘  │
│  ┌──────────────────────────────────┐  │
│  │ 📶 BLE-MIDI                      │  │
│  │   Not connected                  │  │
│  └──────────────────────────────────┘  │
├────────────────────────────────────────┤
│   [◀]        Devices           [▶]    │
└────────────────────────────────────────┘
```

#### デバイスカード

各MIDIインターフェースをカード形式で表示。

| フィールド | USB-MIDI | DIN-MIDI | BLE-MIDI |
|------------|----------|----------|----------|
| アイコン | 🔌 | 🎹 | 📶 |
| デバイス名 | Vendor + Product | "SAM2695" | ペア名 |
| IN状態 | ●=接続 ○=なし | ●=接続 ○=なし | ●=接続 ○=なし |
| OUT状態 | ●=接続 ○=なし | 常に● | ●=接続 ○=なし |

#### データ取得

```c
// USB-MIDIデバイス情報構造体（既存）
typedef struct {
    char vendor[64];
    char product[64];
    uint8_t midi_in_ep;   // 0 = not available
    uint8_t midi_out_ep;  // 0 = not available
} usb_midi_device_info_t;

// 将来: DIN-MIDI / BLE-MIDI用の追加構造体
```

### 4. ログ画面（Log Screen）

ESP32のログ出力とRubyスクリプトの`puts`出力を表示。現在の`lcd_console`の機能を継承。

```
┌────────────────────────────────────────┐
│  Logs                         [MIDI●] │
├────────────────────────────────────────┤
│I (12345) USB_HOST: Device connected   │
│I (12350) MIDI: IN endpoint found      │
│Hello from Ruby!                        │
│I (12400) APP: BPM set to 120          │
│Note on: C4 velocity=100                │
│Note off: C4                            │
│I (12500) USB_HOST: Transfer complete  │
│                                        │
│                                        │
├────────────────────────────────────────┤
│   [◀]    [Clear]    Logs       [▶]    │
└────────────────────────────────────────┘
```

#### 機能

| 機能 | 説明 |
|------|------|
| 自動スクロール | 新しいログが追加されると自動的に下へスクロール |
| ログバッファ | 直近N行（デフォルト100行）を保持 |
| クリア | 中央ボタンでログをクリア |

#### ログ分類（将来拡張）

```c
typedef enum {
    LOG_SOURCE_ESP,      // ESP-IDF ESP_LOGx
    LOG_SOURCE_RUBY,     // PicoRuby puts/print
    LOG_SOURCE_MIDI,     // MIDI メッセージ
} log_source_t;
```

### 5. スクリプト選択画面（Script Screen）

SDカード内のRubyスクリプトファイルを一覧表示し、選択実行する。

```
┌────────────────────────────────────────┐
│  Scripts                      [MIDI●] │
├────────────────────────────────────────┤
│  ▶ app.rb                    [Running]│  ← 現在実行中
│    arpeggio.rb                         │
│    chord_pad.rb                        │
│    sequencer.rb                        │
│    > test/                             │  ← サブディレクトリ
│                                        │
│                                        │
│                                        │
│                                        │
├────────────────────────────────────────┤
│   [◀]       [Run]    Scripts   [▶]    │
└────────────────────────────────────────┘
```

#### ファイルリスト

| 要素 | 説明 |
|------|------|
| ▶ マーク | 現在実行中のスクリプト |
| ファイル名 | `.rb`拡張子のファイル |
| [Running] | 実行状態バッジ |
| > フォルダ名 | サブディレクトリ（タップで移動） |

#### 操作

| 操作 | アクション |
|------|------------|
| ファイルタップ | 選択（ハイライト） |
| 中央ボタン [Run] | 選択中のスクリプトを実行 |
| スワイプ上下 | リストスクロール |

#### スクリプト切り替え処理

スクリプト切り替え時のクリーンアップはC側で自動的に行う。Rubyスクリプト側での処理漏れを防ぐため、`at_exit`等のユーザー実装には依存しない。

```c
// スクリプト切り替えのフロー
1. 現在実行中のスクリプトに終了シグナルを送信
2. PicoRuby VMの終了処理を待機（タイムアウト付き）
3. **自動クリーンアップ処理（C側で実行）**
4. VMをリセット
5. 新しいスクリプトファイルをロード
6. VM実行を開始
```

#### 自動クリーンアップ処理（C側実装）

スクリプト終了時に以下のMIDIメッセージを全接続デバイスに自動送信:

```c
// クリーンアップで送信するMIDIメッセージ
typedef struct {
    bool send_all_notes_off;     // CC #123 (All Notes Off)
    bool send_all_sound_off;     // CC #120 (All Sound Off)
    bool send_reset_all_ctrl;    // CC #121 (Reset All Controllers)
    bool send_stop;              // 0xFC (MIDI Stop)
} midi_cleanup_config_t;

// デフォルト設定
static const midi_cleanup_config_t DEFAULT_CLEANUP = {
    .send_all_notes_off = true,
    .send_all_sound_off = true,
    .send_reset_all_ctrl = false,  // 必要に応じて有効化
    .send_stop = true,
};

// クリーンアップ実行
void midi_cleanup_on_script_change(void)
{
    // 全MIDIチャンネル(1-16)に対して送信
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (g_cleanup_config.send_all_notes_off) {
            midi_send_cc(ch, 123, 0);  // All Notes Off
        }
        if (g_cleanup_config.send_all_sound_off) {
            midi_send_cc(ch, 120, 0);  // All Sound Off
        }
        if (g_cleanup_config.send_reset_all_ctrl) {
            midi_send_cc(ch, 121, 0);  // Reset All Controllers
        }
    }

    if (g_cleanup_config.send_stop) {
        midi_send_realtime(0xFC);  // MIDI Stop
    }

    // UIパッドの状態もリセット
    ui_pad_clear_all();
}
```

#### クリーンアップのタイミング

| タイミング | 処理 |
|------------|------|
| スクリプト切り替え時 | 自動クリーンアップ実行 |
| USBデバイス切断時 | 不要（デバイス側で処理） |
| システム再起動時 | 自動クリーンアップ実行 |
| Rubyスクリプトからの明示的な終了 | 自動クリーンアップ実行 |

#### Ruby側でのループ終了パターン

```ruby
# MIDI.stop_requested?でループを抜けるだけでOK
# クリーンアップはC側で自動実行される
MIDI.bpm_loop(UI.bpm, output: device) do
  break if MIDI.stop_requested?
  # 通常処理
end
# ← ここでC側の自動クリーンアップが実行される
```

#### C API

```c
// クリーンアップ設定の変更（必要に応じて）
void midi_set_cleanup_config(const midi_cleanup_config_t* config);

// 手動でクリーンアップを実行（通常は不要）
void midi_cleanup_now(void);

// クリーンアップの有効/無効切り替え
void midi_set_auto_cleanup(bool enabled);
```

### 6. 設定画面（Settings Screen）

BLE-MIDIペアリングやシステム設定を行う画面。

```
┌────────────────────────────────────────┐
│  Settings                     [MIDI●] │
├────────────────────────────────────────┤
│                                        │
│  ┌─ BLE-MIDI ──────────────────────┐   │
│  │  Status: Scanning...            │   │
│  │  ┌────────────────────────────┐ │   │
│  │  │ ○ KORG nanoKEY Studio      │ │   │
│  │  │ ● iPhone (Connected)       │ │   │
│  │  │ ○ Bluetooth MIDI Device    │ │   │
│  │  └────────────────────────────┘ │   │
│  └─────────────────────────────────┘   │
│                                        │
│  Backlight: ████████░░ 80%             │
│  Version: v1.0.0                       │
│                                        │
├────────────────────────────────────────┤
│   [◀]       [Scan]   Settings   [▶]   │
└────────────────────────────────────────┘
```

#### BLE-MIDIセクション

| 要素 | 説明 |
|------|------|
| Status | Idle / Scanning / Pairing / Connected |
| デバイスリスト | 検出されたBLE-MIDIデバイス一覧 |
| ○ / ● マーカー | ○=未接続、●=接続中 |

#### BLE-MIDI操作フロー

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│    Idle     │────→│  Scanning   │────→│  Pairing    │
│             │     │  (10sec)    │     │             │
└─────────────┘     └─────────────┘     └─────────────┘
       ↑                   │                   │
       │                   │ timeout           │ success/fail
       │                   ↓                   ↓
       │            ┌─────────────┐     ┌─────────────┐
       └────────────│    Idle     │←────│  Connected  │
                    └─────────────┘     └─────────────┘
```

#### 操作

| 操作 | アクション |
|------|------------|
| [Scan] ボタン | BLEスキャン開始（10秒間） |
| デバイスタップ | ペアリング開始 / 接続解除 |
| 接続中デバイスタップ | 切断確認ダイアログ |

#### システム設定項目

| 項目 | 範囲 | 説明 |
|------|------|------|
| Backlight | 0-100% | LCDバックライト輝度 |
| Version | - | ファームウェアバージョン（読み取り専用） |

#### BLE-MIDI データ構造

```c
typedef enum {
    BLE_MIDI_STATE_IDLE,
    BLE_MIDI_STATE_SCANNING,
    BLE_MIDI_STATE_PAIRING,
    BLE_MIDI_STATE_CONNECTED,
    BLE_MIDI_STATE_ERROR,
} ble_midi_state_t;

typedef struct {
    char name[32];
    uint8_t addr[6];         // BLE MAC address
    int8_t rssi;             // Signal strength
    bool is_connected;
    bool is_paired;          // Previously paired device
} ble_midi_device_t;

#define BLE_MIDI_MAX_DEVICES 8

typedef struct {
    ble_midi_state_t state;
    ble_midi_device_t devices[BLE_MIDI_MAX_DEVICES];
    uint8_t device_count;
    uint8_t connected_index;  // 0xFF = none connected
} ble_midi_status_t;
```

#### ペアリング処理

```c
// ペアリング開始
esp_err_t ble_midi_start_scan(uint32_t duration_ms);

// スキャン停止
esp_err_t ble_midi_stop_scan(void);

// デバイスに接続
esp_err_t ble_midi_connect(const uint8_t* addr);

// 接続解除
esp_err_t ble_midi_disconnect(void);

// 状態取得
const ble_midi_status_t* ble_midi_get_status(void);

// コールバック登録
typedef void (*ble_midi_event_cb_t)(ble_midi_state_t state, const ble_midi_device_t* device);
void ble_midi_set_callback(ble_midi_event_cb_t cb);
```

#### 設定の永続化

```c
// NVS（Non-Volatile Storage）に保存
typedef struct {
    uint8_t backlight_level;     // 0-255
    uint8_t last_ble_addr[6];    // 最後に接続したBLEデバイス
    bool auto_reconnect;         // 起動時に自動再接続
    float last_bpm;              // 最後に設定したBPM
    bool sync_mode;              // 外部同期モード
} system_settings_t;

esp_err_t settings_save(const system_settings_t* settings);
esp_err_t settings_load(system_settings_t* settings);
```

## コンポーネント設計

### ファイル構成

```
main/
├── ui/
│   ├── ui_manager.h           # UI管理（画面切り替え）
│   ├── ui_manager.cpp
│   ├── ui_common.h            # 共通定数・描画関数
│   ├── ui_common.cpp
│   ├── screen_main.h          # メイン画面
│   ├── screen_main.cpp
│   ├── screen_pad.h           # パッド画面
│   ├── screen_pad.cpp
│   ├── screen_midi_info.h     # MIDI情報画面
│   ├── screen_midi_info.cpp
│   ├── screen_log.h           # ログ画面
│   ├── screen_log.cpp
│   ├── screen_script.h        # スクリプト選択画面
│   ├── screen_script.cpp
│   ├── screen_settings.h      # 設定画面
│   └── screen_settings.cpp
├── ble_midi/                  # BLE-MIDI機能
│   ├── ble_midi.h
│   └── ble_midi.cpp
├── settings/                  # 設定管理
│   ├── settings.h
│   └── settings.cpp
└── lcd_console/               # 既存（ログ画面に統合予定）
    ├── lcd_console.h
    └── lcd_console.cpp
```

### クラス設計

```cpp
// 画面基底クラス
class Screen {
public:
    virtual void enter() = 0;           // 画面に入るとき
    virtual void leave() = 0;           // 画面を離れるとき
    virtual void update() = 0;          // 定期更新
    virtual void draw() = 0;            // 描画
    virtual void onTouch(int x, int y) = 0;  // タッチイベント
    virtual const char* getTitle() = 0;
};

// UI管理クラス
class UIManager {
public:
    void init();
    void update();                      // M5.update() + 画面更新
    void nextScreen();
    void prevScreen();
    void setScreen(int index);
    Screen* getCurrentScreen();

private:
    static constexpr int SCREEN_COUNT = 6;
    Screen* screens[SCREEN_COUNT];
    int currentScreenIndex;
    void drawStatusBar();
    void drawNavBar();
    void handleTouch();
};
```

### 画面遷移

```
    [◀]                                                                    [▶]
┌────────┐   ┌────────┐   ┌────────┐   ┌────────┐   ┌─────────┐   ┌──────────┐
│  Main  │ → │  Pads  │ → │  MIDI  │ → │  Logs  │ → │ Scripts │ → │ Settings │
│ (BPM)  │   │        │   │  Info  │   │        │   │         │   │  (BLE)   │
└────────┘   └────────┘   └────────┘   └────────┘   └─────────┘   └──────────┘
     ↑                                                                   ↑
     └─────────────────────────── wrap around ───────────────────────────┘
```

## API設計

### C API（ESP-IDF/FreeRTOS側）

```c
// UI初期化
esp_err_t ui_init(void);

// UI更新（メインループから呼び出し）
void ui_update(void);

// BPM値の表示更新
void ui_set_bpm(float external_bpm, float internal_bpm);

// 内部BPMの設定（UIから変更された時に呼ばれるコールバック登録）
typedef void (*bpm_change_cb_t)(float new_bpm);
void ui_set_bpm_change_callback(bpm_change_cb_t cb);

// 外部同期モードの設定
void ui_set_sync_mode(bool enabled);
bool ui_get_sync_mode(void);

// 現在の設定BPMを取得
float ui_get_internal_bpm(void);

// 小節/拍カウンタの更新
void ui_set_bar_beat(uint32_t bar, uint8_t beat);

// ビートインジケータ（1拍内の進捗）の更新
void ui_set_beat_progress(uint8_t progress);  // 0-23 (MIDI Clock ticks)

// MIDIデバイス情報の更新
void ui_update_midi_device(midi_interface_t type, const char* name, bool in_connected, bool out_connected);

// ログの追加
void ui_add_log(log_source_t source, const char* message);

// スクリプト一覧の更新
void ui_refresh_script_list(void);

// 現在のスクリプト状態の更新
void ui_set_script_status(const char* filename, bool running);

// BLE-MIDI状態の更新
void ui_update_ble_status(const ble_midi_status_t* status);

// バックライト設定
void ui_set_backlight(uint8_t level);
```

### Ruby API（PicoRuby側）

```ruby
# 終了リクエストの確認
MIDI.stop_requested?  # => true/false

# UIへのログ出力（puts/printは自動的にリダイレクト）
puts "Hello"
```

### UI API（PicoRuby側）

```ruby
# === BPM関連 ===

# UIで設定されたBPMを取得
bpm = UI.bpm  # => 120.0

# BPMを設定（UIにも反映）
UI.bpm = 140.0

# BPMを動的に使用（UIの変更に追従）
MIDI.bpm_loop(UI.bpm, output: device) do
  # UIでBPMが変更されると次のループから反映
end

# BPM変更時のコールバック
UI.on_bpm_change do |new_bpm|
  puts "BPM changed to #{new_bpm}"
end

# 外部同期モード
UI.sync_mode        # => true / false
UI.sync_mode = true

# === パッド関連 ===

# パッドの設定
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  device.note_on(36, 127)
  MIDI.sleep_ms(100)
  device.note_off(36)
end

UI.pad(2, label: "HiHat", color: :yellow, type: :momentary) do |pressed|
  if pressed
    device.note_on(42, 100)
  else
    device.note_off(42)
  end
end

UI.pad(3, label: "Mute", color: :blue, type: :toggle) do |on|
  @muted = on
end

# パッドの状態を取得
UI.pad_state(3)  # => true / false

# パッドのラベル/色を動的に更新
UI.pad_label(1, "Bass")
UI.pad_color(2, :green)

# パッドをクリア
UI.pad_clear(1)
UI.pad_clear_all

# === 画面制御 ===

# 現在の画面を取得
UI.current_screen  # => :main, :pads, :midi_info, :logs, :scripts, :settings

# 画面を切り替え
UI.screen = :pads

# === その他 ===

# バックライト
UI.backlight        # => 0-100
UI.backlight = 80
```

## データフロー

### BPM・小節/拍カウンタの更新フロー

```
[External MIDI Clock (0xF8)] ─→ [midi_input_task]
        │                              │
        │                              ├─→ [BPM計算] ─→ [ui_set_bpm()]
        │                              │
        │                              ├─→ [tick count] ─→ [ui_set_beat_progress()]
        │                              │      (0-23)
        │                              │
        │                              └─→ [24 ticks = 1拍] ─→ [ui_set_bar_beat()]
        │                                                            │
        ↓                                                            │
[MIDI Start (0xFA)] ─→ [bar=1, beat=1にリセット]                    │
[MIDI Stop (0xFC)]  ─→ [カウント停止]                               │
                                                                     ↓
                                                              [Main Screen]
```

### ログ表示のフロー

```
[ESP_LOGx()] ──→ [lcd_console_vprintf()] ──→ [Log Ring Buffer]
                                                    │
[Ruby puts] ───→ [VFS stdout] ────────────────→ [Log Ring Buffer]
                                                    │
                                                    ↓
                                             [Log Screen draw()]
```

### スクリプト切り替えフロー

```
[Script Screen] ─→ [ユーザー選択] ─→ [Run ボタン]
        │
        ↓
[ui_request_script_change(filename)]
        │
        ↓
[picoruby_task] ←── 終了シグナル ──→ [MIDI.stop_requested? = true]
        │
        ↓
[Rubyスクリプト終了] ─→ [MIDI.bpm_loopからbreak]
        │
        ↓
[midi_cleanup_on_script_change()] ──→ [All Notes Off / All Sound Off / MIDI Stop]
        │                                      │
        │                                      ↓
        │                              [全MIDIデバイスに送信]
        ↓
[ui_pad_clear_all()] ──→ [パッド状態リセット]
        │
        ↓
[VM reset & reload]
        │
        ↓
[New script execution]
```

### BLE-MIDIペアリングフロー

```
[Settings Screen] ─→ [Scan ボタン]
        │
        ↓
[ble_midi_start_scan()] ─→ [BLE GAP Scan]
        │
        ↓
[BLE_MIDI_STATE_SCANNING] ─→ [デバイス検出コールバック]
        │                            │
        │                            ↓
        │                     [デバイスリスト更新]
        │                            │
        ↓ (10秒後 or 手動停止)       │
[BLE_MIDI_STATE_IDLE]               │
        │                            │
        ↓ (デバイスタップ)           │
[ble_midi_connect(addr)] ←──────────┘
        │
        ↓
[BLE_MIDI_STATE_PAIRING] ─→ [BLE GATT接続]
        │
        ├─→ 成功 ─→ [BLE_MIDI_STATE_CONNECTED]
        │                    │
        │                    ↓
        │             [MIDI情報画面に反映]
        │
        └─→ 失敗 ─→ [BLE_MIDI_STATE_ERROR] ─→ [エラー表示]
```

## 実装フェーズ

### Phase 1: UI基盤

- UIManager実装
- 共通描画関数
- タッチイベント処理
- 画面切り替え

### Phase 2: ログ画面

- 既存lcd_consoleの統合
- リングバッファでのログ保持
- スクロール表示

### Phase 3: メイン画面

- BPM表示・変更
- TAP Tempo
- 外部同期モード
- ビートインジケータ
- `MIDI.external_bpm`との連携

### Phase 4: パッド画面

- 6ボタンレイアウト
- タッチイベント処理
- Ruby APIバインディング（UI.pad）
- ボタンタイプ（Trigger/Momentary/Toggle）

### Phase 5: MIDI情報画面

- USB-MIDIデバイス情報表示
- デバイスカードUI
- （将来）DIN/BLE-MIDI対応

### Phase 6: スクリプト選択画面

- SDカードファイル列挙
- スクリプト選択UI
- スクリプト切り替え処理
- グレースフルシャットダウン

### Phase 7: 設定画面

- BLE-MIDIスキャン機能
- BLE-MIDIペアリング・接続管理
- バックライト調整
- 設定のNVS永続化

## 将来の拡張

### DIN-MIDI対応（SAM2695）

```c
typedef struct {
    bool connected;
    bool in_enabled;
    bool out_enabled;
} din_midi_status_t;
```

### 追加設定項目（将来追加）

- WiFi設定（OTA更新用）
- MIDIチャンネルフィルタ
- 自動再接続設定の詳細オプション

## 関連ファイル

- `main/platform/platform_m5stack.cpp`: M5Stack初期化
- `main/lcd_console/lcd_console.cpp`: 現在のログ出力実装
- `main/usb_midi_host.c`: USB MIDIデバイス情報取得
- `components/picoruby-esp32/`: PicoRuby統合
