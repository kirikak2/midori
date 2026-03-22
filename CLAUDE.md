# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

**Midori** - ESP32-S3向けUSB MIDIホストファームウェア。USB MIDIデバイス（例：Roland J-6）を検出し通信するUSBホスト機能を実装。FreeRTOSによるマルチタスク構成。

## ビルドコマンド

最初にESP-IDF環境をソースする必要あり：
```bash
source ~/esp-idf/export.sh
```

ビルド・書き込みコマンド：
- `idf.py build` - プロジェクトをコンパイル
- `idf.py flash` - ファームウェアをデバイスに書き込み
- `idf.py monitor` - シリアル出力をモニタ（Ctrl+]で終了）
- `idf.py flash monitor` - 書き込み後すぐにモニタ開始
- `idf.py fullclean` - ビルド成果物を全削除
- `idf.py menuconfig` - プロジェクト設定（sdkconfig）

## アーキテクチャ

### アクションベースのステートマシン

メインアプリケーション（`main/usb_midi_host.c`）は、`driver_obj.actions`にビットマスクで保留中の操作を格納し、ループで処理するパターンを使用：

```
ACTION_OPEN_DEV → ACTION_GET_DEV_INFO → ACTION_GET_DEV_DESC →
ACTION_GET_CONFIG_DESC → ACTION_GET_STR_DESC → ACTION_CHECK_MIDI →
ACTION_SETUP_MIDI → ACTION_SEND_NOTE（繰り返し）
```

切断時：`ACTION_CLOSE_DEV`でクリーンアップし、再接続に備えて状態をリセット。

### スレッドモデル

- **メインタスク**（`app_main`）：USBホストライブラリのイベントループを実行
- **クラスドライバタスク**（`class_driver_task`）：デバイス固有の処理を担当、コア0に固定

### USB MIDI検出

MIDIデバイスはインターフェースディスクリプタでAudioクラス（0x01）かつMIDI Streamingサブクラス（0x03）をチェックして検出。検出後、インターフェースをクレームしMIDI IN/OUTバルクエンドポイントを探索。

### 転送コールバック

- `midi_in_transfer_callback()`：受信MIDIデータを処理、メッセージをデコード、転送を再サブミット
- `midi_out_transfer_callback()`：MIDI送信完了を処理、遅延後に次のノートをトリガー

### ホットプラグ対応

グローバル静的変数（`g_in_transfer`、`g_midi_enabled`等）で接続/切断サイクルをまたいでデバイス状態を追跡。`reset_midi_static_vars()`でデバイス取り外し時に状態をクリーンアップ。

## 主要データ構造

```c
typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;           // 保留中アクションのビットマスク
    uint8_t midi_in_ep;         // MIDI INエンドポイントアドレス
    uint8_t midi_out_ep;        // MIDI OUTエンドポイントアドレス
    bool is_midi_device;
    uint8_t note_counter;       // C4-C5を循環するカウンタ
    uint8_t num_interfaces;
} class_driver_t;
```

## USB MIDIパケットフォーマット

4バイトパケット：`[ケーブル番号 + CIN][MIDIバイト1][MIDIバイト2][MIDIバイト3]`
- Note On CIN: 0x09
- Note Off CIN: 0x08

## PicoRuby関連

### 重要：main_task.rbは自動生成ファイル

`components/picoruby-esp32/mrblib/main_task.rb`はCMakeによって自動生成されるファイル。**直接編集しないこと。**

修正する場合は以下のソースファイルを編集：
- `components/picoruby-esp32/mrblib/main_task_base.rb` - メインのRubyコード
- `components/picoruby-esp32/board_config.rb.in` - ボード設定テンプレート

ビルド時にこれらが結合されて`main_task.rb`が生成される。

### ESP32環境でのDir操作

ESP32環境では`picoruby-dir` gemではなく`picoruby-filesystem-fat`が使用される。そのため：
- `Dir.entries(path)` は**使用不可**
- 代わりに `Dir.open(path) { |dir| dir.read }` を使用する

```ruby
# NG: Dir.entries("/sd")
# OK:
Dir.open("/sd") do |dir|
  while entry = dir.read
    # 処理
  end
end
```

### スクリプト切り替え機能

UI（またはC側）からのスクリプト変更リクエストを処理し、実行中のスクリプトを停止して新しいスクリプトをロードする機能。

#### アーキテクチャ

**C側（picoruby-esp32.c）:**
- `g_stop_requested` フラグ：スクリプト停止要求を示す（volatile bool）
- `g_script_change_requested` フラグ：新規スクリプトロード要求を示す
- `g_requested_script[256]` バッファ：リクエストされたスクリプトのパス

**Ruby側（main_task_base.rb）:**
- `ScriptManager` クラスを通じてC側と通信
- メインループで `get_requested` をポーリング
- スクリプトロード後に `clear_request` でフラグをクリア

#### スクリプト切り替えフロー

1. **停止リクエスト**
   - UI（またはC側）が `picoruby_esp32_request_script_change(path)` を呼び出し
   - `g_stop_requested = true` と `g_script_change_requested = true` がセット

2. **MIDI クリーンアップ（C側自動）**
   - MIDI Clock タイマーコールバック（`clock_timer_callback`）が `stop_requested` を検出
   - `picoruby_esp32_midi_cleanup()` を自動実行：
     - 全チャンネル（0-15）に All Sound Off (CC#120) 送信
     - 全チャンネル（0-15）に All Notes Off (CC#123) 送信
     - MIDI Stop (0xFC) 送信
     - USB_MIDI と SAM2695 両方に送信
   - タイマー停止、`g_running = false`

3. **MIDI Input タスク停止（C側自動）**
   - `midi_input_task` ループが `stop_requested` を検出
   - タスク終了

4. **Ruby スクリプト終了**
   - `MIDI.bpm_loop` が各イテレーションで `ScriptManager#stop_requested?` をチェック
   - `true` の場合、`send_stop` を送信（必要に応じて）してループを `break`
   - `MIDI::Clock#running?` は C側の `_timer_running?` を確認（自動同期）

5. **新スクリプトロード**
   - main_task_base.rb のメインループに制御が戻る
   - `ScriptManager#get_requested` が新しいパスを返す
   - `clear_request` でフラグをクリア
   - `load` で新スクリプト実行
   - `GC.start` でメモリ解放

#### ScriptManager Ruby API

```ruby
sm = ScriptManager.new

# C側から要求されたスクリプトパスを取得（要求がなければnil）
script_path = sm.get_requested

# 要求フラグをクリア
sm.clear_request

# stop_requested フラグをチェック（スクリプト実行中に使用）
if sm.stop_requested?
  # 停止処理
end
```

#### ファイルシステムの注意点

**スクリプトロードは必ずRuby側で実行すること。**

- C側の `fopen()` は ESP-IDF VFS を使用（PicoRuby VFS とは別物）
- Ruby側の `File.open()` / `load` は PicoRuby VFS を使用
- SDカード上のスクリプトは Ruby VFS でアクセスする必要がある

```ruby
# OK: Ruby VFS 経由でロード
if File.exist?(script_path)
  load script_path
end

# NG: C側で fopen(script_path, "r") - ESP-IDF VFS を使うため失敗する
```

#### 解決済みの問題（2026-03-15）

**問題**: スクリプト切り替えが動作しなかった

**根本原因**:
`c_script_manager_clear_request()` が `g_stop_requested` フラグをクリアしていなかった。

**動作フロー（修正前）**:
1. UI からスクリプト変更リクエスト → `g_stop_requested = true`, `g_script_change_requested = true`
2. `bpm_loop` が `stop_requested?` で `true` を検出 → ループから `break`
3. スクリプト終了、main_task_base.rb のメインループに制御が戻る
4. `sm.clear_request` を呼ぶ → **`g_stop_requested` がそのまま `true`**
5. 新しいスクリプトをロード
6. 新しいスクリプトが `bpm_loop` を開始
7. **即座に `stop_requested?` が `true` を返す → すぐにループから抜ける**
8. スクリプトがすぐに終了してしまう

**修正内容（picoruby-esp32.c:430）**:
```c
static void
c_script_manager_clear_request(mrbc_vm *vm, mrbc_value v[], int argc)
{
  (void)vm; (void)v; (void)argc;
  ESP_LOGI(TAG, "clear_request called - clearing all stop flags");
  g_script_change_requested = false;
  g_stop_requested = false;  // IMPORTANT: Clear stop flag too!
  g_requested_script[0] = '\0';
  SET_NIL_RETURN();
}
```

**結果**: `clear_request` が両方のフラグをクリアするようになり、スクリプト切り替えが正常に動作するようになった。

### シリアルコンソールでのスクリプト実行

M5Stack以外のボード（Freenove等）では、USB-MIDIを使用するためシリアルコンソールが利用できる。
このため、シリアルコンソールからスクリプトを実行できる機能を追加した。

#### C側実装（picoruby-esp32.c）

**バッファ管理**:
```c
static char s_console_buffer[256];
static int s_console_buffer_pos = 0;
```

**`c_script_manager_check_console()` 関数**:
- `getchar()` で非ブロッキングでシリアル入力をチェック
- エコーバック機能（入力した文字を即座に表示）
- バックスペース処理（0x08/0x7F）：`printf("\b \b")` で画面上の文字を削除
- `load /sd/app.rb` 形式のコマンドを検出
- `picoruby_esp32_request_script_change()` を呼び出し
- 未知のコマンドにはヘルプを表示
- プロンプト `> ` を自動表示

**ScriptManagerに追加されたメソッド**:
```ruby
sm = ScriptManager.new
console_script = sm.check_console  # => "/sd/app.rb" or nil
```

#### Ruby側実装（main_task_base.rb）

```ruby
# ヘルパー関数
def load_script(script_path)
  puts "Loading script: #{script_path}"
  begin
    if File.exist?(script_path)
      load script_path
      puts "Script finished: #{script_path}"
    else
      puts "Script not found: #{script_path}"
    end
  rescue => e
    puts "Script error: #{e.message}"
  end
  GC.start
end

# メインループ
print "> "  # 初期プロンプト表示
sm = ScriptManager.new
loop do
  # シリアルコンソール入力をチェック
  console_script = sm.check_console
  if console_script
    puts "Loading: #{console_script}"
    sm.clear_request
    load_script(console_script)
  end

  # UI からのリクエストをチェック
  script_path = sm.get_requested
  if script_path
    puts "UI request: #{script_path}"
    sm.clear_request
    load_script(script_path)
    print "> "
  end

  sleep_ms 100
end
```

#### 使用例

```bash
source ~/esp-idf/export.sh
idf.py build flash monitor
```

**シリアルコンソール**:
```
Initialization complete.
Available commands:
  load /sd/app.rb  - Load and run a script from SD card
  (or select script from M5Stack UI)
> load /sd/app1.rb
Loading: /sd/app1.rb
Loading script: /sd/app1.rb
[音が鳴る]
Script finished: /sd/app1.rb
> load /sd/app2.rb
Loading: /sd/app2.rb
Loading script: /sd/app2.rb
[別の音が鳴る]
Script finished: /sd/app2.rb
> help
Unknown command: help
Available commands:
  load /sd/app.rb  - Load and run a script
>
```

**機能**:
- エコーバック：入力した文字がリアルタイムで表示
- バックスペース：文字削除が可能
- プロンプト表示：コマンド実行後に自動的に `> ` を表示
- エラーハンドリング：未知のコマンド入力時にヘルプ表示
- バッファオーバーフロー検出：コマンドが長すぎる場合の処理

### スクリプトロードとESP32リスタート

#### 問題の背景

mruby/cはグローバルなシンボルテーブルとクラスレジストリを持っている。同一VMプロセス内で`load`を複数回呼び出すと：

1. 前のスクリプトの状態が残りメモリ破損が発生
2. Cache error / MMU entry fault などの致命的エラー
3. Sandboxを使っても同様の問題（irep参照の問題）

#### 解決策: NVSを使ったリスタート方式

スクリプトロード時にESP32をリスタートし、常にクリーンな状態から実行する。

**フロー**:
1. `load /sd/app.rb` コマンドを入力
2. スクリプトパスをNVS（不揮発性ストレージ）に保存
3. ESP32をリスタート
4. 起動時にNVSからスクリプトパスを取得
5. スクリプトを実行
6. 実行後、NVSからスクリプトパスを削除

**ScriptManagerメソッド**:
```ruby
sm = ScriptManager.new

# Autorun管理
sm.set_autorun("/sd/app.rb")  # NVSにパスを保存
path = sm.get_autorun          # NVSからパスを取得（nilも可）
sm.clear_autorun               # NVSからパスを削除

# ユーティリティ
sm.free_heap                   # 残りヒープサイズ（バイト）
sm.esp_restart                 # ESP32をリスタート（戻らない）
```

**コンソールコマンド**:
```
> load /sd/app.rb
Scheduling: /sd/app.rb
Restarting ESP32...
（リスタート後、スクリプトが自動実行される）

> heap
Free heap: 2048000 bytes

> restart
Restarting ESP32...
```

#### main_task_base.rbの実装

```ruby
# 起動時にautorunスクリプトをチェック
sm = ScriptManager.new
autorun_script = sm.get_autorun
if autorun_script
  sm.clear_autorun  # クラッシュ時のループ防止
  run_autorun_script(autorun_script)
end

# loadコマンド処理
def request_load_script(script_path)
  sm = ScriptManager.new
  sm.set_autorun(script_path)
  sm.esp_restart
  # 戻らない
end
```

#### ヒープサイズ設定

ESP32-S3 + PSRAM環境では、ヒープサイズを**2MB**に設定:
```c
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
#define HEAP_SIZE (1024 * 1024 * 2)
```

#### メリット

1. **常にクリーンな状態**: 毎回リスタートするためメモリ破損なし
2. **信頼性**: Cache error や MMU fault が発生しない
3. **シンプル**: 複雑なメモリ管理が不要

## 既知の課題

### USB MIDIデバイスの電源ON順序問題（2026-03-18）

**症状**:
USB MIDIデバイスからのMIDI IN信号を受信できない場合がある。

**影響を受けるデバイス（確認済み）**:
- Roland J-6
- Teenage Engineering OP-1 field

※特定のデバイス固有の問題ではなく、USB MIDIデバイス全般に影響する可能性が高い

**原因**:
MIDIデバイスの電源ON/USB接続のタイミングに依存する問題。

**動作パターン**:
| パターン | 手順 | MIDI IN受信 |
|---------|------|-------------|
| 1 | MIDIデバイス電源ON → USB接続 → ESP32起動 | ❌ 受信不可 |
| 2 | ESP32起動 → MIDIデバイス電源ON状態でUSB接続 | ❌ 受信不可 |
| 3 | ESP32起動 → USB接続 → MIDIデバイス電源ON | ✅ 受信可能 |

**技術的詳細**:
- パターン1,2では `midi_in_transfer_callback` が呼び出されない
- USB IN転送はサブミットされているが、コールバックが発火しない
- デバイスが既に電源ONの状態でUSB接続されると、USB初期化シーケンスが正しく完了しない可能性

**現状の対処法**:
- **運用で回避**: MIDIデバイスはUSB接続後に電源を入れる

**将来の検討事項**:
- ESP-IDF内部APIを使用したUSBポートリセットの実装
- MIDI INデータ未受信時の自動再接続機構
- デバイス固有のワークアラウンド（要調査）

## PicoRuby Supervisor Task Architecture（2026-03-20実装中）

### 概要

ESP32リスタートなしでRubyスクリプトを動的に切り替えるための、FreeRTOSベースのSupervisorタスクアーキテクチャ。

**目的**:
- NVS + ESP32リスタート方式を置き換え
- `mrbc_cleanup()` によるVM完全リセットでメモリクリーン化
- UIモードとスクリプトモード間の高速切り替え

### アーキテクチャ

#### タスク構成

```
Supervisor Task (Core 1) ─┬─ PicoRuby Task (動的生成/削除)
                           │   └─ main_task.rb
                           │       ├─ UI Mode
                           │       └─ Script Mode
                           │
USB Host Task (Core 0) ────┘
```

**Supervisor Task**:
- FreeRTOSキューでスクリプトリクエストを受信
- PicoRuby Taskを動的に生成/削除
- `mrbc_cleanup()` でVM状態を完全リセット
- `prebuilt_gems[].required` フラグをリセット

**PicoRuby Task**:
- 毎回新しいタスクとして生成される
- main_task.rbを実行（UIモードまたはスクリプトモード）
- 完了後、Supervisorに通知して終了

#### 実行フロー

```
[起動]
  ↓
Supervisor起動 → PicoRuby Task生成（UIモード）
  ↓
main_task.rb: UI Mode
  - FLASH/SD初期化
  - スクリプトリスト表示
  - ユーザー入力待ち
  ↓
[スクリプト選択: load /sd/app.rb]
  ↓
sm.request_script("/sd/app.rb") → Supervisorにリクエスト
  ↓
PicoRuby Task終了
  ↓
Supervisor: mrbc_cleanup() 実行
  - メモリアロケータクリア
  - VM状態クリア
  - シンボルテーブルクリア
  - prebuilt_gems[].required リセット
  ↓
PicoRuby Task再生成（スクリプトモード）
  ↓
main_task.rb: Script Mode
  - 最小限の初期化（require machine/watchdog/shell）
  - STDIN/STDOUT初期化
  - スクリプト実行
  ↓
スクリプト終了 → Supervisorに通知
  ↓
Supervisor: mrbc_cleanup() 実行
  ↓
PicoRuby Task再生成（UIモード） → ループ
```

### 主要ファイル

#### C側

**picoruby_supervisor.h** (新規):
- Supervisor API定義
- `supervisor_init()`: Supervisor初期化
- `supervisor_request_script()`: C側からのスクリプトリクエスト

**picoruby_supervisor.c** (新規):
- Supervisorタスク実装
- PicoRubyタスクのライフサイクル管理
- `cleanup_vm()`: `mrbc_cleanup()` + gem flagsリセット
- ScriptManagerクラス登録（Ruby側API）

**picoruby-esp32.c** (変更):
- グローバル変数を非static化（Supervisor共有用）
- `extern const uint8_t main_task[]` 宣言に変更

**usb_midi_host.c** (変更):
- `picoruby_task` 生成を `supervisor_init()` に置き換え

#### Ruby側

**main_task_base.rb** (大幅変更):

```ruby
sm = ScriptManager.new
script_to_run = sm.get_autorun_script

if script_to_run
  # ========== Script Mode ==========
  require 'machine'
  require "watchdog"
  Watchdog.disable
  require "shell"
  
  STDIN = IO.new
  STDOUT = IO.new
  
  # SDカード状態チェック（再初期化は条件付き）
  $sd_available = VFS.volume_index("/sd") ? true : false
  if !$sd_available
    $sd_available = try_init_sd_card
  end
  
  run_script(script_to_run)
else
  # ========== UI Mode ==========
  # 完全な初期化（FLASH、SD、スクリプトリスト）
  # UIループ（コンソール入力、UIタッチ処理）
end
```

**ScriptManager Ruby API**:
```ruby
sm = ScriptManager.new

# Supervisorから渡されたスクリプトパスを取得
script = sm.get_autorun_script  # => "/sd/app.rb" or nil

# Supervisorにスクリプト実行をリクエスト
sm.request_script("/sd/app.rb")

# MIDI cleanup（Supervisor経由でC側実行）
sm.cleanup_midi
```

### 現在の状態と既知の問題

#### 動作状況

| 動作 | 状態 |
|------|------|
| 初回起動（UIモード） | ✅ 正常動作 |
| 1回目スクリプト実行 | ✅ 正常動作 |
| UIモードへ復帰 | ✅ 正常動作 |
| 2回目スクリプト実行 | ✅ 正常動作 |
| constant警告 | ⚠️ 表示される |

#### 解決済みの問題（2026-03-22）

**問題: 2回目のスクリプト実行でIllegal bytecodeエラー**

**症状**:
```
Running: /sd/app.rb
[c_sandbox_new] Created tcb=$3c18b5d8
RITE header check failed (memcmp=-81)
Expected: 0x52 0x49 0x54 0x45 (RITE)
Actual:   0x01 0x00 0x00 0x00
Exception(vm_id=19): Illegal bytecode (Exception)
```

**根本原因**:
`c_sandbox_new`（sandbox.c）内の`static uint8_t *suspend_vm_code`が問題だった。

1. 1回目のSandbox作成時に`suspend_vm_code`（"Task.current.suspend"のコンパイル結果）を生成してstatic変数に保存
2. `mrbc_cleanup()`でメモリアロケータがクリアされ、`suspend_vm_code`が指すメモリが無効化
3. 2回目のSandbox作成時、`suspend_vm_code != NULL`なので再利用を試みる
4. `mrbc_create_task(suspend_vm_code, ...)`で**無効なメモリ**をロードしようとする
5. `load_header`で無効なデータ（`0x01 0x00 0x00 0x00`）を読んでエラー

**修正内容**:

1. **sandbox.c**: `suspend_vm_code`を関数内staticからfile scope static変数`g_suspend_vm_code`に移動

```c
// Global suspend_vm_code that needs to be reset on mrbc_cleanup()
static uint8_t *g_suspend_vm_code = NULL;
```

2. **sandbox.c**: `mrbc_sandbox_cleanup()`関数を追加

```c
void mrbc_sandbox_cleanup(void)
{
  g_suspend_vm_code = NULL;
}
```

3. **picoruby_supervisor.c**: `cleanup_vm()`で`mrbc_sandbox_cleanup()`を呼び出し

```c
static void cleanup_vm(void)
{
    mrbc_cleanup();
    // ... require flags reset ...
    mrbc_sandbox_cleanup();  // Reset g_suspend_vm_code
}
```

**結果**: 2回目のSandbox作成時も`g_suspend_vm_code == NULL`なので新規作成され、正常動作するようになった。

#### 既知の問題（2026-03-20）

**問題: constant警告**

**症状**:
```
warning: already initialized constant.
warning: already initialized constant.
```

**原因**:
- `mrbc_cleanup()` が定数テーブル（`handle_const`）をクリアしない
- 次の `mrbc_init()` で `mrbc_init_global()` が呼ばれるが、既存の定数が残っている

**影響**:
- 機能的には問題ない（警告のみ）
- ログが煩雑になる

### 次のステップ

1. **constant警告の解決**
   - `mrbc_cleanup()` で定数テーブルをクリアする方法を検討
   - または `mrbc_init_global()` が既存定数を許容するように修正

2. **安定性の検証**
   - 複数回のスクリプト切り替えテスト
   - メモリリーク確認
   - エラーハンドリングの強化

### メリット（期待）

1. **高速切り替え**: ESP32リスタート不要（数秒 → 数十ミリ秒）
2. **クリーンな状態**: `mrbc_cleanup()` で完全リセット
3. **柔軟性**: UIモードとスクリプトモード間の自由な切り替え

### 技術的課題

1. **VFS状態の管理**: Ruby側とC側の状態同期
2. **定数テーブルのクリア**: `mrbc_cleanup()` の限界
3. **メモリ管理**: 動的タスク生成/削除の安全性
