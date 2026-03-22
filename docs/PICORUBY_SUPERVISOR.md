# PicoRuby Supervisor Task Architecture

ESP32リスタートなしでRubyスクリプトを動的に切り替えるための、FreeRTOSベースのSupervisorタスクアーキテクチャ。

## 目的

- NVS + ESP32リスタート方式を置き換え
- `mrbc_cleanup()` によるVM完全リセットでメモリクリーン化
- UIモードとスクリプトモード間の高速切り替え

## タスク構成

```
┌─────────────────────────────────────────────────────────────┐
│                        Core 1                                │
│  ┌──────────────────────────────────────────────────────┐   │
│  │           Supervisor Task (Priority 4)                │   │
│  │  - FreeRTOSキューでコマンド受信                        │   │
│  │  - PicoRuby Taskのライフサイクル管理                   │   │
│  │  - VMクリーンアップ (mrbc_cleanup())                   │   │
│  └────────────────────┬─────────────────────────────────┘   │
│                       │ 生成/削除                            │
│                       ▼                                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │           PicoRuby Task (Priority 3)                  │   │
│  │  - main_task.rb を実行                                 │   │
│  │  - UI Mode または Script Mode                          │   │
│  │  - 完了後 Supervisor に通知                            │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                        Core 0                                │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              USB Host Task                            │   │
│  │  - USBホストライブラリのイベントループ                  │   │
│  │  - MIDIデバイスの検出・通信                            │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 実行モード

### UI Mode
- FLASHとSDカードの初期化
- スクリプトリストの表示（M5Stack）
- ユーザー入力待ち（タッチまたはシリアルコンソール）

### Script Mode
- 最小限の初期化（machine/watchdog/shell）
- 指定されたスクリプトを実行
- 完了後、Supervisorに通知

## 実行フロー

```
[ESP32起動]
     │
     ▼
supervisor_init()
     │
     ├─→ Supervisor Task 生成 (Core 1, Priority 4)
     │
     ▼
start_picoruby_task(NULL)  ←─── UI Mode で開始
     │
     ▼
┌────────────────────────────────────────┐
│  PicoRuby Task: UI Mode                │
│  - FLASH/SD 初期化                      │
│  - スクリプトリスト表示                  │
│  - ユーザー入力待ちループ                │
└────────────────────────────────────────┘
     │
     │ [ユーザーがスクリプト選択]
     │ sm.request_script("/sd/app.rb")
     │ または UIタッチ / コンソール load コマンド
     ▼
supervisor_ruby_request_script()
     │
     ▼
PicoRuby Task 終了
     │
     ▼
┌────────────────────────────────────────┐
│  Supervisor: cleanup_vm()              │
│  - picoruby_esp32_midi_cleanup()       │
│  - mrbc_cleanup()                      │
│  - prebuilt_gems[].required リセット   │
│  - mrbc_sandbox_cleanup()              │
└────────────────────────────────────────┘
     │
     ▼
start_picoruby_task("/sd/app.rb")  ←─── Script Mode で開始
     │
     ▼
┌────────────────────────────────────────┐
│  PicoRuby Task: Script Mode            │
│  - 最小限の初期化                       │
│  - スクリプト実行                       │
└────────────────────────────────────────┘
     │
     │ [スクリプト終了]
     ▼
Supervisor に通知 (EVT_TASK_COMPLETED)
     │
     ▼
┌────────────────────────────────────────┐
│  Supervisor: 次のアクション決定         │
│  - g_script_change_requested? → 別スクリプト実行
│  - s_ruby_script_requested?   → 要求されたスクリプト実行
│  - それ以外                   → UI Mode に復帰
└────────────────────────────────────────┘
```

## 主要ファイル

### C側

| ファイル | 説明 |
|---------|------|
| `picoruby_supervisor.h` | Supervisor API 定義 |
| `picoruby_supervisor.c` | Supervisor タスク実装、ScriptManager クラス登録 |
| `picoruby-esp32.c` | グローバル変数、MIDI 関連関数 |

### Ruby側

| ファイル | 説明 |
|---------|------|
| `mrblib/main_task_base.rb` | メインの Ruby コード（UI/Script モード分岐） |
| `board_config.rb.in` | ボード設定テンプレート |

**注意**: `mrblib/main_task.rb` は CMake によって自動生成されるファイル。直接編集しないこと。

## Supervisor API (C)

```c
// 初期化
void supervisor_init(void);

// スクリプトリクエスト（NULLでUIモード）
bool supervisor_request_script(const char *script_path);

// スクリプト停止
bool supervisor_stop_script(void);

// 状態取得
bool supervisor_is_script_running(void);
supervisor_state_t supervisor_get_state(void);
const char* supervisor_get_current_script(void);

// 結果取得
bool supervisor_get_last_result(supervisor_script_result_t *result);

// ログ出力（プラットフォーム依存）
void supervisor_log(const char *format, ...);
```

## ScriptManager API (Ruby)

```ruby
sm = ScriptManager.new

# Supervisor から渡されたスクリプトパスを取得
script = sm.get_autorun_script  # => "/sd/app.rb" or nil

# Supervisor にスクリプト実行をリクエスト
sm.request_script("/sd/app.rb")

# UI からのスクリプトリクエストを取得
script = sm.get_requested  # => "/sd/app.rb" or nil

# リクエストフラグをクリア
sm.clear_request

# 停止要求をチェック（スクリプト内で使用）
if sm.stop_requested?
  # 停止処理
end

# MIDI cleanup
sm.cleanup_midi

# ヒープサイズ取得
sm.free_heap  # => バイト数

# スクリプトリスト管理（UI用）
sm.clear
sm.add("app.rb")
sm.set_ready

# シリアルコンソール入力チェック
script = sm.check_console  # => "/sd/app.rb" or nil
```

## VMクリーンアップ処理

`cleanup_vm()` 関数は以下の処理を行う：

1. **mrbc_cleanup()** - mruby/c VM のメモリアロケータをクリア
2. **prebuilt_gems[].required リセット** - gem の require フラグをリセット
3. **mrbc_sandbox_cleanup()** - Sandbox の静的変数（g_suspend_vm_code）をリセット

```c
static void cleanup_vm(void)
{
    mrbc_cleanup();

    // Reset require flags
    for (int i = 0; prebuilt_gems[i].name != NULL; i++) {
        prebuilt_gems[i].required = false;
    }

    // Reset sandbox static state
    mrbc_sandbox_cleanup();
}
```

## スクリプト切り替えのトリガー

### 1. Ruby側からのリクエスト

```ruby
sm.request_script("/sd/app.rb")
```

- `supervisor_ruby_request_script()` を呼び出し
- `s_ruby_script_requested = true` をセット
- Supervisor がタスク完了後に処理

### 2. UI（C側）からのリクエスト

```c
picoruby_esp32_request_script_change("/sd/app.rb");
```

- `g_script_change_requested = true` をセット
- `g_stop_requested = true` をセット
- 実行中スクリプトが `stop_requested?` で検出して終了
- Supervisor がタスク完了後に処理

## ヒープサイズ設定

ESP32-S3 + PSRAM 環境では 2MB のヒープを使用：

```c
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
#define HEAP_SIZE (1024 * 1024 * 2)
#else
#define HEAP_SIZE (1024 * 180)
#endif
```

## メリット

1. **高速切り替え**: ESP32 リスタート不要（数秒 → 数十ミリ秒）
2. **クリーンな状態**: `mrbc_cleanup()` で完全リセット
3. **柔軟性**: UI モードとスクリプトモード間の自由な切り替え
