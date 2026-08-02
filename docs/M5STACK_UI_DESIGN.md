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
# 基本的な使用例（triggerメソッド推奨）
# triggerは即座にreturnし、note_offはバックグラウンドで自動送信
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  device.trigger(36, 127, duration: 100)
end

# 従来の方法（ブロッキング - マルチタッチ時に音ズレが発生）
UI.pad(1, label: "Kick", color: :red, type: :trigger) do
  device.note_on(36, 127)
  MIDI.sleep_ms(100)
  device.note_off(36)
end
```

#### マルチタッチ対応

複数パッドを同時に押した場合、`trigger`メソッドを使用すると全ての音が同時に発音されます。

```ruby
# マルチタッチ対応（全て同時発音）
UI.pad(0, label: "Kick", color: :red, type: :trigger) do
  device.trigger(36, 127, duration: 100)
end

UI.pad(1, label: "Snare", color: :blue, type: :trigger) do
  device.trigger(38, 127, duration: 100)
end

UI.pad(2, label: "HiHat", color: :yellow, type: :trigger) do
  device.trigger(42, 100, duration: 50)
end
```

| メソッド | 動作 | マルチタッチ |
|----------|------|--------------|
| `trigger(note, vel, duration:)` | 即座にreturn、note_offは自動 | 同時発音 |
| `note_on` + `sleep_ms` + `note_off` | ブロッキング | 音ズレ発生 |

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
│         [ Refresh ]  [ Stop ]          │  ← ボタン行
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
| 中央ボタン [Run] | 選択中のスクリプトを実行（実行中なら切り替え） |
| [Refresh] | SDカードを再マウントしてリスト再読み込み |
| [Stop] | 実行中スクリプトを停止してUIモードへ復帰 |
| スワイプ上下 | リストスクロール |

#### スクリプトの明示的停止（2026-08-02）

`[Stop]` は `supervisor_stop_script()` を呼ぶだけで、実際の停止処理は
Supervisor タスク上で走る（UIタスクはブロックしない）：

```
[Stop] タップ
   ↓
supervisor_stop_script()          ← 実行中でなければ false を返す（ボタンはグレーアウト）
   ↓ CMD_STOP_SCRIPT をキューへ
Supervisor: stop_picoruby_task()  ← g_stop_requested を立てて最大5秒待つ
   ↓                                 応じなければ vTaskDelete で強制終了
picoruby_esp32_midi_cleanup()     ← All Sound Off / All Notes Off / MIDI Stop
   ↓
cleanup_vm() → reset_ui_state()   ← パッド設定とUIイベントキューをクリア
   ↓
clear_script_request_flags()      ← g_stop_requested 等をクリア（次のスクリプトが即終了するのを防ぐ）
   ↓
start_picoruby_task(NULL)         ← UIモードで再起動
```

`[Running]` バッジと `[Stop]` の活性状態は `ScreenScripts::syncRunningState()` が
`supervisor_get_current_script()` をポーリングして同期する。スクリプトが自然終了
した場合もバッジは自動的に消える。

**スクリプト側の作法**: 停止はまず協調的に行われるため、独自の長いループを書く
場合は `ScriptManager#stop_requested?` を見て `break` すること。`MIDI.bpm_loop` は
内部でチェック済み。チェックしないスクリプトは5秒後に強制終了される。

シリアルコンソールからは `stop` コマンドで同じ処理を実行できる（UIを持たない
ボード向け。スクリプト実行中はRuby側がコマンドキューを読まないため、C側で完結
する経路になっている）。

#### スクリプト切り替え処理

**実装方式**: Ruby側でスクリプトロードを管理し、C側はstop_requestedフラグとMIDIクリーンアップを担当。

**最新の実装状況 (2026-03-15)**:
- Ruby側ポーリングループでスクリプトリクエストを監視: ✅ 実装済み
- C側MIDI自動クリーンアップ: ✅ 実装済み
- `MIDI.bpm_loop` stop_requested検出: ✅ 実装済み
- **問題**: スクリプト切り替えが正しく動作しない（原因調査中）

詳細は `CLAUDE.md` の「スクリプト切り替え機能」セクションを参照。

#### スクリプト切り替えフロー（実装済み）

```
1. UIでスクリプト選択 → [Run]ボタン
   ↓
2. C側: picoruby_esp32_request_script_change(path)
   - g_stop_requested = true
   - g_script_change_requested = true
   - g_requested_script = path
   ↓
3. MIDI Clock タイマーが stop_requested を検出
   - picoruby_esp32_midi_cleanup() 自動実行
     * All Sound Off (CC#120) → 全チャンネル
     * All Notes Off (CC#123) → 全チャンネル
     * MIDI Stop (0xFC)
     * USB_MIDI と SAM2695 両方に送信
   - タイマー停止
   ↓
4. MIDI Input タスクが stop_requested を検出
   - タスク終了
   ↓
5. Ruby: MIDI.bpm_loop が ScriptManager#stop_requested? を検出
   - send_stop (必要に応じて)
   - loop を break
   ↓
6. main_task_base.rb のメインループに戻る
   - ScriptManager#get_requested で新パス取得
   - clear_request でフラグクリア
   - load で新スクリプト実行
   - GC.start でメモリ解放
```

#### 自動クリーンアップ処理（実装済み）

```c
// picoruby-esp32.c: picoruby_esp32_midi_cleanup()
void picoruby_esp32_midi_cleanup(void)
{
  // 全MIDIチャンネル(0-15)に対して送信
  for (uint8_t ch = 0; ch < 16; ch++) {
    uint8_t status_cc = 0xB0 | ch;  // Control Change

    // All Sound Off (CC #120)
    USB_MIDI_send_packet(0, 0x0B, status_cc, 120, 0);
    SAM2695_send_packet(0, 0x0B, status_cc, 120, 0);

    // All Notes Off (CC #123)
    USB_MIDI_send_packet(0, 0x0B, status_cc, 123, 0);
    SAM2695_send_packet(0, 0x0B, status_cc, 123, 0);
  }

  // Send MIDI Stop (0xFC)
  USB_MIDI_send_packet(0, 0x05, 0xFC, 0, 0);
  SAM2695_send_packet(0, 0x05, 0xFC, 0, 0);
}
```

#### Ruby側でのループ終了パターン（実装済み）

```ruby
# ScriptManager を使って stop_requested を監視
# MIDI.bpm_loop が自動的に監視する
MIDI.bpm_loop(UI.bpm, output: device) do
  # stop_requested? が true になると自動的にループを抜ける
  # C側のクリーンアップは既に完了している
end

# 手動でチェックする場合
sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  # 通常処理
end
```

#### C API（実装済み）

```c
// スクリプト変更リクエスト（UIから呼び出し）
bool picoruby_esp32_request_script_change(const char *script_path);

// 停止要求
void picoruby_esp32_request_stop(void);

// 停止要求フラグ確認
bool picoruby_esp32_stop_requested(void);

// 停止フラグクリア
void picoruby_esp32_clear_stop_flag(void);

// MIDIクリーンアップ実行
void picoruby_esp32_midi_cleanup(void);
```

#### Ruby API（実装済み）

```ruby
# ScriptManager でスクリプト切り替えを管理
sm = ScriptManager.new

# UI からリクエストされたスクリプトパスを取得
script_path = sm.get_requested  # => "/sd/app.rb" or nil

# リクエストフラグをクリア
sm.clear_request

# 停止要求フラグをチェック
if sm.stop_requested?
  # 停止処理（通常は MIDI.bpm_loop が自動処理）
end
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

## 実装状況・トラブルシューティング

### Phase 6: スクリプト選択画面 実装状況

#### 完了した実装

1. **ScreenScripts クラス** (`main/ui/screen_script.cpp`, `main/ui/screen_script.h`)
   - SDカード内の.rbファイルを一覧表示
   - タッチによるスクリプト選択
   - Refreshボタンによるリスト再読み込み
   - Runボタンによるスクリプト実行リクエスト

2. **PicoRuby → C スクリプト通知機構**
   - ESP-IDFのfatfsとPicoRubyのFatFsライブラリの競合を回避するアーキテクチャ
   - PicoRuby側でSDカードをマウントし、スクリプト一覧をC側に通知

3. **ScriptManager クラス** (`components/picoruby-esp32/picoruby-esp32.c`)
   ```c
   // C側API
   void picoruby_esp32_clear_script_list(void);
   bool picoruby_esp32_add_script(const char *filename);
   int picoruby_esp32_get_script_count(void);
   const char* picoruby_esp32_get_script_name(int index);
   bool picoruby_esp32_script_list_ready(void);
   void picoruby_esp32_set_script_list_ready(bool ready);
   ```

4. **Ruby側通知関数** (`components/picoruby-esp32/mrblib/main_task_base.rb`)
   ```ruby
   def notify_scripts_to_c
     sm = ScriptManager.new
     sm.clear
     Dir.entries("/sd").each do |entry|
       next if entry == "." || entry == ".."
       next unless entry.end_with?(".rb")
       sm.add(entry)
     end
     sm.set_ready
   end
   ```

#### 技術的な課題と解決策

##### 1. FatFsライブラリの競合問題

**問題**: ESP-IDFのfatfsコンポーネントとPicoRubyのFatFsライブラリが同じシンボル（`f_mount`, `f_open`等）を定義しており、リンク時に多重定義エラーが発生。

**解決策**: ESP-IDFのfatfsを使用せず、PicoRuby側でファイルシステムをマウントし、スクリプト一覧をC側に通知するアーキテクチャを採用。

##### 2. VFSの分離問題

**問題**: PicoRubyのVFSとESP-IDFのVFSは別システムのため、C側から`opendir("/sd")`を呼んでもPicoRubyがマウントしたSDカードにアクセスできない。

**解決策**: PicoRuby（Ruby側）でDir.entriesを実行し、結果をC関数経由でC側のグローバル配列に格納。

##### 3. main_task.rbが実行されない問題

**問題**: `picoruby_task`が`picoruby_esp32_init()`のみを呼び出し、`main_task.rb`（SDカードマウント・スクリプト通知を行う）を実行していなかった。

**修正内容**:
- `usb_midi_host.c`: `picoruby_esp32_init()` → `picoruby_esp32()` に変更
- `picoruby-esp32.c`: `picoruby_esp32()`にScriptManager登録とg_vm_initialized設定を追加

##### 4. クラスメソッド vs インスタンスメソッド

**問題**: `mrbc_define_method`はインスタンスメソッドを定義するが、Ruby側で`ScriptManager.clear`のようにクラスメソッドとして呼び出していた。

**解決策**: Ruby側を`ScriptManager.new.clear`のようにインスタンスメソッド呼び出しに変更。

#### 完了した実装（2026-03-15追記）

5. **スクリプト切り替え機能**
   - C側: `picoruby_esp32_request_script_change()` でstop_requestedフラグをセット
   - C側: MIDI Clock/Input タスクが自動的に停止を検出しクリーンアップ
   - Ruby側: `main_task_base.rb` でポーリングループ実装
   - Ruby側: `ScriptManager#get_requested` / `clear_request` / `stop_requested?` メソッド
   - Ruby側: `MIDI.bpm_loop` が `stop_requested?` をチェック
   - Ruby側: `MIDI::Clock#running?` が C側タイマー状態を確認

6. **MIDI自動クリーンアップ**
   - `picoruby_esp32_midi_cleanup()` 実装
   - All Sound Off (CC#120) を全チャンネルに送信
   - All Notes Off (CC#123) を全チャンネルに送信
   - MIDI Stop (0xFC) を送信
   - USB_MIDI と SAM2695 両方に対応

7. **Dir操作の修正**
   - `Dir.entries` は ESP32 環境で使用不可（picoruby-filesystem-fat）
   - `Dir.open` + `read` パターンに変更

#### 現在の問題（未解決）

**症状**: スクリプト実行中に別のスクリプトを選択して [Run] を押しても、スクリプトが切り替わらない。

**ログ出力**:
```
PICORUBY: Script change requested /sd/app.rb
SCREEN_SCRIPT: Script change request sent /sd/app.rb
```
- ログには表示されるが、実際のスクリプト切り替えが発生しない
- All Notes Off などのクリーンアップも実行されていない様子

**調査ポイント**:
1. `ScriptManager#stop_requested?` が正しく `true` を返すか
   - C側で `g_stop_requested = true` がセットされているか確認
2. `MIDI.bpm_loop` のループが実際に `break` するか
   - 毎イテレーションで `stop_requested?` をチェックしているか
3. `main_task_base.rb` のメインループが `get_requested` を正しくポーリングしているか
   - 100ms間隔でポーリングしているが、スクリプト実行中は `load` がブロックする
4. `picoruby_esp32_clear_stop_flag()` の呼び出しタイミング
   - クリア後に再度フラグがセットされない可能性

**デバッグ方法**:
```bash
idf.py flash monitor
```
ログで以下を確認:
- `Script change requested: /sd/xxx.rb` - リクエスト受信
- `Stop requested, performing cleanup...` - MIDIクリーンアップ実行
- `Stop requested, exiting input task` - Input タスク終了
- `Loading script: /sd/xxx.rb` - 新スクリプトロード開始

#### ファイル変更一覧

| ファイル | 変更内容 |
|----------|----------|
| `main/CMakeLists.txt` | fatfs依存を削除、sd_card.cを除外 |
| `main/Kconfig.projbuild` | マウントポイントを`/sdcard`→`/sd`に変更 |
| `main/usb_midi_host.c` | picoruby_taskで`picoruby_esp32()`を呼び出すよう変更 |
| `main/ui/screen_script.cpp` | PicoRubyのスクリプトリストAPIを使用 |
| `main/ui/screen_script.h` | refreshFromSD()メソッド追加 |
| `components/picoruby-esp32/picoruby-esp32.c` | ScriptManager登録、スクリプトリスト管理API追加 |
| `components/picoruby-esp32/picoruby-esp32.h` | スクリプトリスト管理API宣言追加 |
| `components/picoruby-esp32/mrblib/main_task_base.rb` | `notify_scripts_to_c`関数追加 |

#### 次のステップ

1. シリアルモニタでログを確認し、どの段階で処理が止まっているか特定
2. `Dir.entries`がPicoRubyで正しく動作するか確認
3. ScriptManagerのインスタンスメソッド呼び出しが正しく機能するか確認
4. 必要に応じてScriptManagerをモジュールとして再実装（クラスメソッド対応）
