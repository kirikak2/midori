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

## ノートスケジューラ（マルチタッチ対応）

### 概要

マルチタッチパッドで複数の音を同時に発音するためのノートスケジューラ機能。`trigger`メソッドは即座にnote_onを送信し、指定時間後にnote_offを自動送信する。

### 問題と解決

**従来の問題**:
```ruby
# このコードはブロッキングするため、マルチタッチ時に音ズレが発生
UI.pad(0) do
  device.note_on(36, 127)
  MIDI.sleep_ms(100)  # ← ここで100ms待機、他のパッドは待たされる
  device.note_off(36)
end
```

**解決策**:
```ruby
# triggerは即座にreturnし、note_offはC側タイマーが自動送信
UI.pad(0) do
  device.trigger(36, 127, duration: 100)  # ← 即座にreturn
end
```

### Ruby API

```ruby
# MIDI::Device#trigger
# @param note [Integer] ノート番号 (0-127)
# @param velocity [Integer] ベロシティ (0-127, default: 127)
# @param duration [Integer] ノートオフまでのms (default: 100)
# @param channel [Integer] MIDIチャンネル (0-15, default: 0)
device.trigger(60, 100, duration: 200, channel: 0)
```

### C側実装

**主要ファイル**:
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/ports/esp32/midi.c` - スケジューラ実装
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/include/midi.h` - API定義

**内部動作**:
1. `MIDI_Note_trigger()` が呼ばれると即座にnote_on送信
2. スケジューラに (channel, note, off_time) を登録
3. 1msタイマーが定期的にチェック
4. off_timeを過ぎたノートはnote_offを送信

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

## PicoRuby Supervisor Task Architecture

詳細は [docs/PICORUBY_SUPERVISOR.md](docs/PICORUBY_SUPERVISOR.md) を参照。

ESP32リスタートなしでRubyスクリプトを動的に切り替えるための、FreeRTOSベースのSupervisorタスクアーキテクチャ。

**主要コンポーネント**:
- **Supervisor Task** (Core 1, Priority 4): PicoRubyタスクのライフサイクル管理
- **PicoRuby Task** (Core 1, Priority 3): main_task.rbを実行（UIモードまたはスクリプトモード）

**主要ファイル**:
- `components/picoruby-esp32/picoruby_supervisor.c` - Supervisor実装
- `components/picoruby-esp32/mrblib/main_task_base.rb` - メインRubyコード
