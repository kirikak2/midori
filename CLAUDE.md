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
- `mrbgems/picoruby-midi/ports/esp32/midi.c` - スケジューラ実装
- `mrbgems/picoruby-midi/include/midi.h` - API定義

**内部動作**:
1. `MIDI_Note_trigger()` が呼ばれると即座にnote_on送信
2. スケジューラに (channel, note, off_time) を登録
3. 1msタイマーが定期的にチェック
4. off_timeを過ぎたノートはnote_offを送信

## PicoRuby関連

### midori-local mrbgem の置き場所（2026-07-27）

MIDI 系および midori 固有の mrbgem は、picoruby サブモジュールの外側、
リポジトリ直下の [mrbgems/](mrbgems/) に置く（それぞれ独立した git サブモジュール、
`picoruby-ui` のみ通常ディレクトリ）：

| gem | 内容 |
|-----|------|
| `mrbgems/picoruby-midi` | MIDI プロトコル層（パーサ / スケジューラ / クロック） |
| `mrbgems/picoruby-midi-mml` | MML パーサ + プレイヤ |
| `mrbgems/picoruby-usb_midi_host` | USB-MIDI Host トランスポート |
| `mrbgems/picoruby-usb_midi_device` | USB-MIDI Device トランスポート（TinyUSB） |
| `mrbgems/picoruby-uart_midi` | UART MIDI トランスポート |
| `mrbgems/picoruby-sam2695` | SAM2695 ラッパ（uart_midi 上の薄い層） |
| `mrbgems/picoruby-ui` | M5Stack UI |
| `mrbgems/picoruby-dfrobot_rotary_encoder` | DFRobot SEN0502 |

これらは upstream picoruby には含めない方針のため、`components/picoruby-esp32/picoruby/mrbgems/`
から移動した。ビルドへの取り込みは以下の 2 箇所：

- `components/picoruby-esp32/build_config/{xtensa,riscv}-esp.rb` …
  `conf.gem File.expand_path('../../../mrbgems/<gem>', __dir__)`
- `components/picoruby-esp32/CMakeLists.txt` … ESP32 port の C ファイル /
  include ディレクトリを `${CMAKE_SOURCE_DIR}/mrbgems/<gem>/...` で参照

**注意**: gem の `mrblib/*.rb` や C を変更したら `idf.py fullclean`
（`idf.py build` だけでは gem の .rb が再コンパイルされないことがある）。

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

#### C側実装（console_input.c / 2026-08-02 改訂）

コンソール入力は専用タスク [components/picoruby-esp32/console_input.c](components/picoruby-esp32/console_input.c)
が担当する（`console_input_start()` を app_main から呼ぶ）。データフロー：

```
[midi_device モード]
  TinyUSB task --console_cdc_rx_cb()--> StreamBuffer(1KB) --+
[host / serial モード]                                      |
  console task が getchar() をポーリング(10ms) -------------+
                                                            v
                             console task (prio 4 / Core 1)
                             エコー・行編集・コマンド解釈
                                                            |
                                          コマンドキュー <---+
                                                            |
                        ScriptManager#check_console (VM) <---+
```

**重要**: `midi_device` モードでは `picoruby-usb_midi_device` が esp_tinyusb に
`.callback_rx` を登録しており、そのハンドラが RX イベントごとに
`tinyusb_cdcacm_read()` で CDC FIFO を吸い出す。アプリ側が
`USB_MIDI_DEVICE_set_cdc_rx_callback()` を登録していないと**入力バイトはそこで
捨てられ**、CDC VFS 経由の `getchar()` には何も残らない（2026-08-02 に「キー入力が
効かない／反応が悪い」の原因として判明）。

タスクを分けている理由：
- CDC コールバックは TinyUSB タスク上で走るため、コピーして即 return する必要がある
- 行編集・エコーを優先度 4 で行い、100ms スリープする Ruby VM タスク（優先度 3）から
  切り離す。VM が寝ている間・スクリプト実行中でも取りこぼさない
- エコーの `printf()` は CDC 書き込み（`tud_cdc_n_write_flush`）になるため、
  MIDI TX タスクと同様に **tud_task と同じ Core 1 に固定**する

コマンド: `load <path>`（キュー経由で Ruby に渡す）/ `irb` / `stop` / `heap` /
`restart` / `help`。
`load` の実ロードは PicoRuby VFS が必要なため Ruby 側で行う（C の `fopen` は不可）。
`stop` と `irb` だけは C 側で完結させる（`supervisor_stop_script()` /
`supervisor_request_irb()` を直接呼ぶ）。スクリプト
実行中は Ruby 側がコマンドキューを読まないため、キューに積んでも実行中の
スクリプトには届かないから。

**ScriptManagerのメソッド**:
```ruby
sm = ScriptManager.new
console_script = sm.check_console  # => "/sd/app.rb" or nil（キューから取り出すだけ）
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

### PicoModem（Web ターミナルからのファイル転送 / 2026-08-02）

詳細は [docs/PICOMODEM.md](docs/PICOMODEM.md) を参照。

PicoRuby web terminal から SD カードへファイルを Upload / Download する機能。
R2P2 と同じ PicoModem プロトコル（RBTP）で、ブラウザ側は upstream のまま使える。

ホストが裸の `STX (0x02)` を送るとコンソールタスクが `ACK (0x06)` を返して raw
モードに入り、以降のバイトを `picorb_hal_stdin_push()` で PicoRuby の stdin へ
流し込む。プロトコル本体は Ruby 側の `PicoModem.session($stdin, ConsoleIO.new)`
が処理する（ファイル I/O は PicoRuby VFS でしか `/sd` に到達できないため）。

- `picoruby-picomodem` / `crc` / `pack` は `picoruby-shell` の依存として**既に
  組み込み済み**。gem 側の変更は無し
- 送信は VFS の `\n`→`\r\n` 変換を避けるため `ConsoleIO#write`
  （CDC は TinyUSB 直、それ以外は line-ending を一時的に LF へ）
- セッション中は `esp_log_level_set("*", ESP_LOG_NONE)` でログを抑止
- 動作確認済みは `midi_device`（CDC）モードのみ。スクリプト実行中は不可

### irb（インタラクティブモード / 2026-08-04）

詳細は [docs/IRB.md](docs/IRB.md) を参照。

コンソール（`midi_device` モードでは USB CDC）から `irb` と打つと、Supervisor が
**SD カードのスクリプトと同じ扱い**で irb セッションを起動する（専用の PicoRuby
タスク + まっさらな VM、終了したら UI モードへ復帰）。1 行ごとの評価は
`picoruby-shell` の `Shell#start(:irb)` が **PicoRuby の Sandbox** で行う
（`Kernel.load` のスクリプト実行と同じ機構）。

- 擬似スクリプトパス `SUPERVISOR_IRB_PATH`（`":irb"`）で要求スクリプトのスロットを
  流れるので、停止 / VM クリーンアップ / UI 復帰の経路は分岐なしで再利用される
- `irb` コマンドは `stop` と同様 C 側で完結（`supervisor_request_irb()`）
- セッション中はコンソールタスクが `CONSOLE_MODE_IRB` になり、行編集をやめて
  バイトを `picorb_hal_stdin_push()` へ流す（行編集は Ruby 側の `Editor::Line`）。
  host / serial モードでは picoruby-machine の `stdin_reader` と取り合いになるため
  コンソールタスクは stdin を読まない
- 端末モードは cooked のまま（Ctrl-C / Ctrl-Z がシグナルとして届くため）
- irb は `stop_requested?` を見ないので、UI の Stop は 5 秒タイムアウト後の強制停止

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

## Tombola シーケンサー（2026-08-03）

詳細は [docs/TOMBOLA.md](docs/TOMBOLA.md) を参照。

回転する多角形の中でボールが跳ね、壁との衝突でノートを鳴らす物理シミュレーション型
シーケンサー。Ruby からは `UI::Tombola` として使う（M5Stack CoreS3 / Tab5 のみ。
Freenove では no-op スタブ）。

**発音は C++ 側**（[main/ui/screen_tombola.cpp](main/ui/screen_tombola.cpp)）が
衝突と同じフレームで `MIDI_Note_trigger()` を呼ぶ。ノートのタイミングが
`UI.process` のポーリング間隔に左右されないようにするため。`on_hit` を登録すると
同じ衝突が UI イベントキュー経由で Ruby にも届く（`sound = false` で C++ 発音を切れる）。

物理は**正規化座標**（外接円半径 = 1.0）で計算し、ピクセル変換は描画時のみ。
CoreS3（320x240）と Tab5（1280x720）で挙動を一致させるため。

`ui_tombola_tick()` は現在の画面に関係なく `ui_update()` から呼ばれるので、
別画面を見ている間も演奏は止まらない。

```ruby
t = UI::Tombola.new(sides: 6, rotation: 12, gravity: 0.5,
                    device: MIDI::Device.new(MIDIDevices.sam2695))
t.scale = [36, 38, 42, 45, 46, 49]  # 辺 N → scale[N % size]
t.add_ball(color: :red)
t.start
UI.pad(1, label: "Faster") { t.rotation = t.rotation + 4 }
```

サンプル: [examples/tombola.rb](examples/tombola.rb)

## Tab5 の描画はキャッシュに残る（2026-08-10）

Tab5 のパネルは**フレームバッファパネル**（`Panel_DSI` → `Panel_FrameBufferBase`）。
LovyanGFX は CPU で**キャッシュされた write-back の PSRAM** に描き、MIPI-DSI は
その PSRAM を DMA で走査する（キャッシュをスヌープしない）。

`Panel_FrameBufferBase` は変更範囲を溜めて書き戻すが、**それを行うのは
`display()` だけで、`endWrite()` は呼ばない**。したがって:

```cpp
M5.Lcd.display();   // ← 描いたら必ず呼ぶ。これが無いと画面に出ない
```

呼ばないと、描画はキャッシュラインがたまたま追い出されたときにしか画面へ
届かない（順序も時刻も不定、小さい領域は永久に出ない）。症状は
**「新しい絵と古い絵が混ざって画面が大きく乱れる」**。大きな塗りつぶしは
互いを追い出すので目立たず、細い円弧や数文字の更新で一気に露呈する
（2026-08-10 に Knobs 画面で発覚）。

`UIManager::update()` の最後で 1 回呼んでいるので、通常の画面実装で個別に
気にする必要はない。**UI タスクの update() 以外の経路で描く場合は自分で呼ぶこと。**
CoreS3（SPI パネル）では `Panel_Device::display()` が空実装なので無害。

## Knobs 画面（2026-08-10）

詳細は [docs/KNOBS.md](docs/KNOBS.md) を参照。

CC などの連続値を指で回して操作するノブのグリッド。Pads がワンショット担当なのに
対し、Knobs は持続的なパラメータ担当。CoreS3 は 6 個、Tab5 は 12 個 × 各 4 バンク
（右端の A/B/C/D ストリップで切り替え）。M5Stack 専用で Freenove では no-op スタブ。

**MIDI を送るのは Ruby のブロック**（`UI.pad` と同じ役割分担）。Tombola が C++ 側で
発音するのとは逆だが、要求が違う: Tombola はリズムのジッタが直接効くのに対し、CC は
数 ms 遅れても分からない。代わりに宛先・CC 番号・カーブがスクリプト側に残る。

値の変化は**ノブごとに 1 通だけ**キューに載る（新しい値が古いものを上書きする）ので、
`UI.process` を回すのが遅いスクリプトは解像度が粗くなるだけで詰まらない。

タッチはノブ中心まわりの**外積**（Tombola のドラッグ回転と同じ
`dθ = (px·dy − py·dx)/r²`）。既定の `sensitivity 1.0` では指の位置とゲージ先端が
厳密に一致する。

```ruby
UI.knob(1, label: "Cutoff", color: :cyan, value: 64) do |v|
  device.control_change(74, v.to_i)
end
UI.knob(4, bank: 2, label: "Pan", origin: :center, value: 64) { |v| ... }
UI.knob_send_all(bank: :all)   # 定義だけでは送られない。初期送信は明示的に
UI.knobs                       # Knobs 画面へ
```

サンプル: [examples/knobs.rb](examples/knobs.rb)

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

## メモリ確保ガイドライン (ESP32-P4 + PSRAM)

詳細は [docs/MEMORY_ALLOCATION.md](docs/MEMORY_ALLOCATION.md) を参照。

**重要な落とし穴**: static 変数に 数百 byte 以上の inline buffer を持たせると、
内部 DRAM の .bss レイアウトが変わり、最悪「LCD が真っ暗・シリアルは正常」という
症状で起動時に破綻する (2026-04-19 に SysEx 実装で実際に遭遇)。
溜め込み系バッファは必要時だけ malloc してください。

## PicoRuby Supervisor Task Architecture

詳細は [docs/PICORUBY_SUPERVISOR.md](docs/PICORUBY_SUPERVISOR.md) を参照。

ESP32リスタートなしでRubyスクリプトを動的に切り替えるための、FreeRTOSベースのSupervisorタスクアーキテクチャ。

**主要コンポーネント**:
- **Supervisor Task** (Core 1, Priority 4): PicoRubyタスクのライフサイクル管理
- **PicoRuby Task** (Core 1, Priority 3): main_task.rbを実行（UIモードまたはスクリプトモード）

**主要ファイル**:
- `components/picoruby-esp32/picoruby_supervisor.c` - Supervisor実装
- `components/picoruby-esp32/mrblib/main_task_base.rb` - メインRubyコード

**スクリプトの明示的停止（2026-08-02）**:
Scripts 画面の `[Stop]` ボタン、またはシリアルコンソールの `stop` コマンドから
`supervisor_stop_script()` を呼ぶと、実行中スクリプトを止めて UI モードに戻る。
協調停止（`stop_requested?`）を5秒待ち、応じないスクリプトは強制終了される。
独自の長いループを書くスクリプトは `ScriptManager#stop_requested?` を見て
`break` すること（`MIDI.bpm_loop` はチェック済み）。

## ボード別MIDIデバイス設定（2026-04-11）

### 概要

ボードごとに利用可能なMIDIデバイスとピンアサインが異なるため、本体のmain_task.rbで
ボード設定に基づいてデバイスを初期化し、SDカード内のスクリプトから利用できるようにした。

詳細は [docs/MIDI_DEVICES.md](docs/MIDI_DEVICES.md) を参照。

### ボード別利用可能デバイス

利用可能なデバイスは **ボード × USBポートモード** で決まる（USBポートモードは
`./switch_board.sh <board> [host|serial|midi_device]` で選択。後述の
「USBポートモード（2026-07-25）」を参照）。

| ボード | USBモード | SAM2695 | USB-MIDI Host | USB-MIDI Device |
|-------|----------|---------|---------------|-----------------|
| m5stack | host (既定) | ○ (17,18) | ○ (USB-C) | - |
| m5stack | serial | ○ (17,18) | - | - |
| m5stack | midi_device | ○ (17,18) | - | ○ (USB-C / TinyUSB) |
| freenove | host (既定) | ○ (17,18) | ○ (USB-C) | - |
| freenove | serial | ○ (17,18) | - | - |
| freenove | midi_device | ○ (17,18) | - | ○ (USB-C / TinyUSB) |
| m5stack_tab5 | midi_device (既定) | ○ (53,54 / Port A) | ○ (USB-A) | ○ (USB-C / TinyUSB) |
| m5stack_tab5 | serial | ○ (53,54 / Port A) | ○ (USB-A) | - |

ESP32-S3ボード（m5stack / freenove）はUSBコネクタが1つ・USB PHYも1つなので、
Host（USB-OTG）とDevice（USB-Serial/JTAG または TinyUSB）は**排他**。
Tab5はUSB-Aが常にHostなので、モードはUSB-Cポートの役割のみを決める。

### アーキテクチャ

**設定ファイル**:
- `components/picoruby-esp32/mrblib/board_config.rb.in` - ボード設定テンプレート
- `components/picoruby-esp32/mrblib/board_config.rb` - CMakeで自動生成（DO NOT EDIT）
- `components/picoruby-esp32/CMakeLists.txt` - ボード別設定の定義

**初期化コード** (main_task_base.rb):
```ruby
module MIDIDevices
  @sam2695 = nil
  @usb_midi_host = nil

  def self.sam2695
    @sam2695
  end

  def self.usb_midi_host
    @usb_midi_host
  end

  def self.init_sam2695
    if BoardConfig::HAS_SAM2695
      require 'sam2695'
      @sam2695 = SAM2695.new(BoardConfig::SAM2695_TX_PIN, BoardConfig::SAM2695_RX_PIN)
    end
  end

  def self.init_usb_midi_host
    if BoardConfig::HAS_USB_MIDI_HOST
      require 'usb_midi'
      @usb_midi_host = USB_MIDI.instance
    end
  end
end
```

**初期化タイミング**:
- UI Mode: 起動時、SDカード初期化の後
- Script Mode: スクリプト実行前

### SDカード内スクリプトでの使用方法

**推奨**: MIDIDevicesモジュールを使用（ボード間の移植性が高い）

```ruby
require 'midi'
require 'ui'

# ボード設定に基づいて初期化済みのデバイスを取得
sam = MIDIDevices.sam2695
usb = MIDIDevices.usb_midi_host

if sam
  device = MIDI::Device.new(sam)
  # ... SAM2695を使用
end

if usb
  usb_device = MIDI::Device.new(usb)
  # ... USB-MIDIを使用
end
```

**従来の方法**（互換性のため残存、非推奨）:
```ruby
require 'sam2695'
sam = SAM2695.new(17, 18)  # ピン番号をハードコード（ボード依存）
device = MIDI::Device.new(sam)
```

### ボード設定の追加・変更

新しいボードを追加する場合、または既存ボードの設定を変更する場合：

1. `components/picoruby-esp32/CMakeLists.txt` を編集
2. 該当するボード設定セクション（`if(CONFIG_USB_MIDI_BOARD_*)`）で以下を設定：
   - `SAM2695_TX_PIN` / `SAM2695_RX_PIN`
   - `HAS_SAM2695` (true/false)
3. リビルド: `idf.py build`

`HAS_USB_MIDI_HOST` / `HAS_USB_MIDI_DEVICE` はボード分岐では設定しない。
USBポートモード（後述）から自動導出される。

**例**: M5Stack Tab5の設定
```cmake
if(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
  set(BOARD_NAME "M5Stack Tab5 (${USB_MODE_NAME})")
  # ... SD card settings ...
  # MIDI devices (Port A / UART)
  set(SAM2695_TX_PIN 53)  # Port A pin2 (SDA)
  set(SAM2695_RX_PIN 54)  # Port A pin1 (SCL)
  set(HAS_SAM2695 true)
endif()
```

## USBポートモード（2026-07-25）

### 概要

USBコネクタ（device 側になり得るポート）の役割を、ボードとは独立した設定として
選べるようにした。ESP32-S3 ボードでも Tab5 と同様に TinyUSB で USB-MIDI デバイス
として動作させられる。

| モード | USBポートの役割 | コンソール | USB-MIDI Host |
|-------|----------------|-----------|---------------|
| `host` | USB-OTG ホスト | UART | ○ |
| `serial` | USB-Serial/JTAG（書き込み・JTAG も可） | USB | S3: ✕ / Tab5: USB-A のみ |
| `midi_device` | TinyUSB CDC + MIDI デバイス | USB CDC | S3: ✕ / Tab5: USB-A のみ |

ESP32-S3（m5stack / freenove）は USB コネクタ 1 つ・USB PHY 1 つを USB-OTG と
USB-Serial/JTAG で共有するため、Host と Device は排他。Tab5 は USB-A が常に Host。

### 切り替え方法

```bash
./switch_board.sh <board> [host|serial|midi_device]

./switch_board.sh m5stack midi_device   # CoreS3 を USB-MIDI デバイスに
./switch_board.sh m5stack serial        # CoreS3 を USB シリアルコンソールに（開発用）
./switch_board.sh m5stack_tab5          # Tab5（既定は midi_device）
```

`sdkconfig.defaults` は「ボード設定 + モード設定」の連結で生成される：
- `sdkconfig.defaults.<board>` … ボード固有（ターゲット、PSRAM、SDピン、flashサイズ等）
- `sdkconfig.defaults.usbmode.<mode>` … USBモード固有（コンソール経路、TinyUSB設定）

旧 `m5stack_with_usbserial` は `m5stack serial` に置き換えた（旧名でも動くよう
switch_board.sh 側でエイリアスしている）。

**注意**: `midi_device` モードでは USB-Serial/JTAG が切断されるため、書き込み時は
BOOT を押しながら RESET でダウンロードモードに入る必要がある。`idf.py monitor` は
TinyUSB CDC 経由で使える。

### Kconfig / 実装

- `main/Kconfig.projbuild`
  - `choice USB_MIDI_USB_MODE` … `USB_MIDI_USB_MODE_HOST` /
    `USB_MIDI_USB_MODE_SERIAL` / `USB_MIDI_USB_MODE_MIDI_DEVICE`
  - `CONFIG_USB_MIDI_HOST_ENABLED` … 導出値（非表示）。Tab5 は常に y、
    S3 は host モードのみ y
- `main/usb_midi_host.c` … `CONFIG_USB_MIDI_HOST_ENABLED` /
  `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE` で起動するドライバを切り替え
- `components/picoruby-esp32/CMakeLists.txt` … 上記から `HAS_USB_MIDI_HOST` /
  `HAS_USB_MIDI_DEVICE` を導出して board_config.rb を生成。加えて
  `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE=y` のとき
  `target_compile_definitions` で gem に USB アイデンティティを注入する
  （後述）
- `picoruby-usb_midi_device/ports/esp32/*.c` … gem 自身は Midori の Kconfig を
  参照せず `USB_MIDI_DEVICE_ENABLED` / `USB_MIDI_DEVICE_WITH_CDC` でガード
  （未定義時はスタブ / MIDI 単機能）。P4 の PHY mux 切り替えは
  `CONFIG_IDF_TARGET_ESP32P4` 限定で、S3 では ESP-IDF の `usb_phy` ドライバが
  PHY を USB-OTG に引き渡す
- `components/picoruby-esp32/build_config/xtensa-esp.rb` … S3 でも
  `picoruby-usb_midi_device` gem をビルドに含める

### picoruby-usb_midi_device への設定注入（2026-08-02）

gem は製品名を持たない汎用 USB-MIDI デバイス（既定は MIDI 単機能・
"PicoRuby MIDI"）で、Midori 固有の値はすべてビルド定義で外から与える。
一覧と既定値は
[mrbgems/picoruby-usb_midi_device/include/usb_midi_device_config.h](mrbgems/picoruby-usb_midi_device/include/usb_midi_device_config.h)。

`components/picoruby-esp32/CMakeLists.txt`（ボード分岐の直後）:

| 定義 | 値 |
|------|-----|
| `USB_MIDI_DEVICE_ENABLED` | 1（`CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE` のとき） |
| `USB_MIDI_DEVICE_WITH_CDC` | 1（コンソール / `idf.py monitor` を同じ USB-C に載せるため） |
| `USB_MIDI_DEVICE_MANUFACTURER` | `"Midori"` |
| `USB_MIDI_DEVICE_PRODUCT` | `"Midori Tab5"` / `"Midori CoreS3"` / `"Midori Freenove"` |
| `USB_MIDI_DEVICE_SERIAL` | `"MIDORI-<BOARD>-001"` |

CDC を有効にすると IAD 複合デバイスになり PID とエンドポイント番号が
変わる（`CONFIG_TINYUSB_CDC_ENABLED=y` が必要）。CDC の受信データは
gem 側では解釈せず、アプリが登録したコールバックへ渡す：

```c
#include "usb_midi_device.h"
static void my_cdc_rx(const uint8_t *data, size_t len, void *arg) { /* TinyUSB task */ }
USB_MIDI_DEVICE_set_cdc_rx_callback(my_cdc_rx, NULL);
```

（Midori のシリアルコンソールは `tinyusb_console_init()` が stdin/stdout を
CDC に張るので従来どおり `getchar()` ベースのままでよい。）
