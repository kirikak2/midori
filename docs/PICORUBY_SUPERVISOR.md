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
│  │  - UI Mode / Script Mode / irb Mode                    │   │
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

### irb Mode
- Script Mode と同じ最小限の初期化
- `Shell#start(:irb)` でシリアルコンソール上の対話セッションを実行
  （1行ごとに PicoRuby の Sandbox でコンパイル / 実行）
- 擬似スクリプトパス `SUPERVISOR_IRB_PATH`（`":irb"`）として要求スクリプトの
  スロットを流れるため、停止・VMクリーンアップ・UI復帰の経路は Script Mode と共通
- `quit` / `exit` / Ctrl-D で終了、Supervisorに通知
- 詳細は [IRB.md](IRB.md)

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
| `mrblib/main_task_base.rb` | メインの Ruby コード（UI/Script/irb モード分岐） |
| `board_config.rb.in` | ボード設定テンプレート |

**注意**: `mrblib/main_task.rb` は CMake によって自動生成されるファイル。直接編集しないこと。

## Supervisor API (C)

```c
// 初期化
void supervisor_init(void);

// スクリプトリクエスト（NULLでUIモード）
bool supervisor_request_script(const char *script_path);

// irb セッションのリクエスト（= supervisor_request_script(SUPERVISOR_IRB_PATH)）
bool supervisor_request_irb(void);

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

# irb モードで起動されたか（get_autorun_script は irb のとき nil を返す）
sm.irb_requested?  # => true / false

# irb セッションの開始 / 終了（コンソールの受け渡し。ensure で必ず irb_end）
sm.irb_begin
sm.irb_end
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

## スクリプトの明示的停止（2026-08-02）

`supervisor_stop_script()` が停止経路。Scripts 画面の `[Stop]` ボタンと
シリアルコンソールの `stop` コマンドから呼ばれる。

```c
bool supervisor_stop_script(void);  // 実行中でなければ false
```

- `supervisor_is_script_running()` は「Script モードで走っているか」を返す
  （UI モードでも PicoRuby タスクは存在するため、`s_current_script[0]` も見る）
- `CMD_STOP_SCRIPT` ハンドラは 停止 → MIDI cleanup → `cleanup_vm()` →
  `reset_ui_state()` → `clear_script_request_flags()` → UI モード再起動、の順
- `clear_script_request_flags()` は `g_stop_requested` /
  `g_script_change_requested` / `g_requested_script` を消す。これを怠ると次に
  ロードしたスクリプトが `stop_requested?` を見て即座に終了する
  （CLAUDE.md「解決済みの問題（2026-03-15）」と同じ症状）
- `reset_ui_state()` は M5Stack ボードでのみ `ui_pad_clear_all()` と
  `ui_event_init()` を呼び、スクリプトが残したパッド設定と未処理の
  パッドイベントを捨てる

### タスクの回収

`picoruby_runner_task()` は完了を通知したあと `vTaskSuspend(NULL)` で自分を
止め、Supervisor が削除する流れになっている。以前はどの経路も
`s_picoruby_task = NULL` にするだけで `vTaskDelete()` を呼んでおらず、
スクリプト切り替えのたびに 16KB のスタックがリークしていた。現在は
`reap_picoruby_task()` に集約している。

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

## トラブルシューティング

### スクリプト例外クラッシュ問題（修正済み）

**問題**: スクリプトが例外（`NoMethodError`、`NameError`等）を発生させると、ESP32が`Guru Meditation Error: LoadProhibited`パニックでクラッシュする。

**現象**:
```
Exception(vm_id=19): in `load_file': undefined local variable or method 'hoge' for Object (NoMethodError)
Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
EXCVADDR: 0x00000004
Backtrace: 0x42028640:0x3fcb9a50 (mrbc_traverse_class_tree)
```

**根本原因**:

`picoruby-sandbox` の `c_sandbox_error()` 関数における参照カウント管理の不備。

1. サンドボックス内でスクリプト実行中に例外が発生
2. サンドボックスVMのタスクが終了し、`mrbc_run()` → `mrbc_vm_end()` が呼ばれる
3. `mrbc_vm_end()` で例外メッセージを出力後、`mrbc_decref(&vm->exception)` が呼ばれ例外オブジェクトが解放される
4. `require.rb` で `sandbox.error` が呼ばれる
5. `c_sandbox_error()` が**既に解放済み**の `sandbox_vm->exception` を `mrbc_incref` なしで返す
6. `raise err` で無効な例外オブジェクトを再raise
7. rescue での型チェック時に `mrbc_obj_is_kind_of()` → `mrbc_traverse_class_tree()` が呼ばれる
8. `cls->super` ポインタが無効なメモリを指しているため NULL アクセスでパニック

**修正内容**:

`picoruby-sandbox/src/mrubyc/sandbox.c` の `c_sandbox_error()` で、例外オブジェクトを返す前に `mrbc_incref()` を呼び、参照カウントをインクリメント：

```c
static void
c_sandbox_error(mrbc_vm *vm, mrbc_value *v, int argc)
{
  SS();
  mrbc_vm *sandbox_vm = (mrbc_vm *)&ss->tcb->vm;
  if (sandbox_vm->exception.tt == MRBC_TT_NIL) {
    SET_NIL_RETURN();
  } else {
    mrbc_value err = sandbox_vm->exception;
    mrbc_incref(&err);  // ← 追加
    SET_RETURN(err);
  }
}
```

この修正により、`mrbc_vm_end()` で `mrbc_decref()` が呼ばれても参照カウントが 1 残り、例外オブジェクトが解放されなくなる。同じファイル内の `c_sandbox_result()` と同じパターン。

**追加改善**:

スクリプトエラーを M5Stack UI の Logs パネルに表示する `ScriptManager#add_log()` メソッドを追加：

```ruby
# main_task_base.rb
rescue => e
  error_msg = "Error: #{e.message}"
  puts error_msg
  sm.add_log(error_msg)  # UIに表示
end
```

C 側実装（`picoruby_supervisor.c`）：
```c
static void c_sm_add_log(mrbc_vm *vm, mrbc_value v[], int argc)
{
  // M5Stack: ui_add_log() でUI Logsパネルに出力
  // その他: ESP_LOGI() でシリアルコンソールに出力
}
```

**関連コミット**:
- `8e398426` Fix reference count bug in Sandbox#error (picoruby submodule)
- `d7ab3d6` Add ScriptManager#add_log for UI error reporting
- `73c6c44` Update picoruby submodule
