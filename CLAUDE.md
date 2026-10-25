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

### midori-runtime コンポーネント（2026-10-11）

PicoRuby を組み込む IDF コンポーネントは [components/midori-runtime/](components/midori-runtime/)
（MIDoRI リポジトリで直接管理。サブモジュールではない）。以前は kirikak2/picoruby-esp32 の
`selfbuild` ブランチをサブモジュールにしていたが、upstream の picoruby-esp32 は 2026-05 に
アーカイブされ（picoruby/R2P2-ESP32 に統合）、差分もほぼすべて MIDoRI 固有だったので引き取った。

- 中身: `CMakeLists.txt`（ボード設定・gem の ports）、`build_config/`、`mrblib/`
  （`main_task_base.rb` / `board_config.rb.in`）、Supervisor、コンソール入力、
  `picoruby-esp32.c`（スクリプト一覧・停止要求などの C API。名前は互換のため据え置き）
- `components/midori-runtime/picoruby/` が picoruby のサブモジュール。**upstream
  picoruby/picoruby の `master` を直接参照する**（2026-10-21 から。それまでは kirikak2/picoruby の
  `midori` ブランチ = master + 2 本のパッチだったが、picoruby-gpio の `mrb_int` 修正と
  picoruby-machine の ESP32 HAL を 1 回だけ初期化する修正はどちらも upstream に取り込まれた）。
  更新は `git submodule update --remote components/midori-runtime/picoruby`。
  また独自パッチが必要になったら fork にブランチを切って `.gitmodules` を戻す
- ビルド設定の名前は upstream に合わせている（`build_config/{xtensa,riscv}-esp-picoruby.rb`、
  libmruby は `picoruby/build/esp32-picoruby/`）。`main_task.rb` をコンパイルする mrbc は
  クロスビルドが自分用に作る `picoruby/build/mrbc/default/bin/mrbc`
- `global_mrb` は picoruby のコンパイラ（`mruby-compiler/src/ccontext.c`）が定義する。
  Supervisor が実行中の VM を指すように更新している
- upstream の変化（ports の追加、define、rake の呼び方）は R2P2-ESP32 の
  `components/picoruby-esp32/CMakeLists.txt` を参照して追う

### ファイルシステム: `/` は littlefs、`/sd` は FAT（2026-10-11）

- 内蔵フラッシュの `/` は upstream の **picoruby-littlefs**（`ports/esp32/flash_hal.c` が
  固定アドレス 0x210000〜0x310000 を直接使う。`partitions.csv` の `picoruby_root` は予約のみ）。
  以前は同じ領域が FAT だったので、切り替え後の初回起動でフォーマットされる
- SD カードの `/sd` は **mrbgems/picoruby-filesystem-fat**（upstream picoruby では 2026-07 に
  削除済み。MIDoRI 側で別 gem kirikak2/picoruby-filesystem-fat として保守）。
  SD 専用・ESP32 専用で、フラッシュ disk（littlefs と同じ固定領域を使っていた）と rp2040 port は外した。
  mruby / mruby/c 両方のバインディングを持つ
- 両 gem が同じ名前で定義していた C シンボルは FAT 側を `mrb_fat__*` / `c_fat__*` /
  `mrb_fat_vfs_methods_type` に改名して共存させている
- マウント: `/` は `Shell.setup_root_volume(:flash, ...)`（upstream のまま）、`/sd` は
  `main_task_base.rb` の `mount_sd_card`（`Shell.setup_sdcard` は SD にも littlefs を使うので使わない）
- SDMMC のスロット / クロックは `FAT.new(:sd, driver: sdmmc, sdmmc_slot:, sdmmc_freq_khz:)` で渡す
  （`SDMMC` クラスの拡張に依存しない）
- FAT の port（`ports/esp32/sd_disk.c`）は IDF 側でコンパイルする（`CMakeLists.txt`）。
  有効化の define は `USE_FAT_SD_DISK`（libmruby と IDF の両方）

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
| `mrbgems/picoruby-filesystem-fat` | FAT（SD カード `/sd` 用。upstream で削除された gem を引き取ったもの） |
| `mrbgems/picoruby-amy` | AMY ソフトシンセ（P4 ボードのみ。中で `lib/amy`＝shorepine/amy をサブモジュールにしている） |

これらは upstream picoruby には含めない方針のため、`components/midori-runtime/picoruby/mrbgems/`
から移動した。ビルドへの取り込みは以下の 2 箇所：

- `components/midori-runtime/build_config/picoruby_gems.rb` … `conf.gem` で gem を追加
  （P4 専用の gem は `riscv-esp-picoruby.rb` 側。例: picoruby-amy）
- `components/midori-runtime/CMakeLists.txt` … ESP32 port の C ファイル /
  include ディレクトリを `${CMAKE_SOURCE_DIR}/mrbgems/<gem>/...` で参照

**注意**: gem の `mrblib/*.rb` や `src/`（VM バインディング）を変更したら
`rm -rf components/midori-runtime/picoruby/build/esp32-picoruby` してからビルドする。
CMake は rake（PicoRuby のビルド）を「`libmruby.a` が無いとき」にしか呼ばないので、
`idf.py build` はもちろん `idf.py fullclean`（`build/` を消すだけ）でも gem の変更は
反映されない（2026-10-04 に確認）。`ports/` の C は IDF 側のビルドなので通常の
`idf.py build` で反映される。

### Ruby VM は mruby のみ（2026-10-07 に mruby/c から移行、2026-10-11 に mruby/c ビルドを撤去）

MIDoRI 本体（midori-runtime）は mruby VM だけをビルドする。`PICORB_VM` キャッシュ変数と
mruby/c 用のビルド設定・分岐は削除した。ただし `mrbgems/` の汎用 gem（picoruby-amy /
-midi / -midi-mml / -sam2695 / -uart_midi など）は**引き続き mruby/c バインディング
（`src/mrubyc`）を保守する**。MIDoRI のビルドでは mruby/c 側はコンパイルされないので、
そちらを変えたときは別途確認すること。
2026-10-07 時点で mruby 版は Tab5 / CoreS3 のビルドとホスト（Linux）上のテストのみで、
**実機では未確認**。

- ビルド設定は `build_config/{xtensa,riscv}-esp-picoruby.rb`、gem 一覧は
  `build_config/picoruby_gems.rb`（mruby 標準の `*-ext` 系 gem も入れている。
  mruby/c が組み込みで持っていたメソッドを補うため）。libmruby は
  `picoruby/build/esp32-picoruby/`
- **mrb_state / mrb_value のレイアウトを変える define は libmruby と IDF 側で一致させる**
  （`CMakeLists.txt` の `ADDITIONAL_DEFINITIONS`）。ずれてもリンクは通り、メモリが静かに壊れる。
  `MRB_UTF8_STRING` は picoruby-mruby も追加するが、mruby コアの設定後に足すのでコアだけ
  抜けてリンクエラーになる。上流 R2P2 と同様にビルド設定で明示している
- Supervisor（`picoruby_supervisor.c`）はスクリプトごとに `mrb_open_with_custom_alloc()`
  で新しい VM を開き、終われば実行タスク上で `mrb_close()` する（gem の final が走り、
  FAT のボリュームも GC で unmount される）。5 秒タイムアウトで強制終了したときは VM を
  **閉じずに捨てる**（途中状態のヒープは触らない）。次の open がヒーププールを丸ごと初期化し、
  プール外の状態（tick タイマの VM ポインタ、FatFs[]）だけ `cleanup_vm()` で外す
- mruby では全 gem がオープン時に初期化される（mrblib もその場で実行）ので `require` は
  実質何もしない。**どれかの gem の mrblib がロード時に例外を出すと `mrb_open()` はそこで
  止まり、以降の gem は未初期化のまま VM が返る**。Supervisor はこれを検出して
  「Gem initialization failed: ...」を出す。mruby/c では require されるまで実行されない
  コードなので、移行で初めて表に出る（`picoruby-rapicco` が先頭で存在しない
  `karmatic_arcade` を require しており、`MIDI.start!` が無いというエラーで発覚。
  2026-10-07。mruby 版では shell gembox を展開して rapicco を外している）。
  同様に **mrblib がロード時に別 gem の定数を参照するなら、その gem を先に初期化させる**。
  初期化順は依存関係を満たしたうえで `conf.gem` した順なので、picoruby-network（shell の依存）が
  読む `ESP32::WiFi` のために `picoruby_gems.rb` で picoruby-esp32 を shell より前に置いている
  （upstream R2P2-ESP32 と同じ。以前はフォークで network に依存を足していた）
- C バインディングの `mrb_get_args` で `"i"` は `mrb_int`（MRB_INT64 なので 8 バイト）を書く。
  `int` 変数を渡すとスタックを 4 バイト壊す（picoruby-gpio の `GPIO.new(pin, dir)` 系で発見・修正）
- Midori 側 gem の `*_gem_final` は何もしない（USB host / UART / MIDI 入力タスク /
  スケジューラ / AMY はスクリプトをまたいで生きているプロセス全体の C 状態。強制終了時は
  final 自体が走らないので、停止は従来どおり `picoruby_esp32_midi_cleanup()` 任せ）
- picoruby-machine の ESP32 HAL（`ports/esp32/machine.c`）は tick タイマと stdin タスクを
  1 回だけ作り、`picorb_hal_final` で VM ポインタを外す（mruby/c は rrt0.c が init を
  1 回に絞っていた）
- PicoRuby タスクの C スタックは mruby では 32KB（mruby/c は 16KB）
- **mruby/c との違いで踏みやすいもの**: mruby/c はクラスメソッドとインスタンスメソッドを
  区別しない。C で `define_method` したメソッドを `Klass.foo` と呼んでいると mruby では
  NoMethodError（picoruby-filesystem-fat の `FAT.init_spi` / `init_sdmmc` がこれだった）。
  モジュールに生やす C 関数は `mrb_define_module_function_id` で定義する

### 重要：main_task.rbは自動生成ファイル

`components/midori-runtime/mrblib/main_task.rb`はCMakeによって自動生成されるファイル。**直接編集しないこと。**

修正する場合は以下のソースファイルを編集：
- `components/midori-runtime/mrblib/main_task_base.rb` - メインのRubyコード
- `components/midori-runtime/board_config.rb.in` - ボード設定テンプレート

ビルド時にこれらが結合されて`main_task.rb`が生成される。

### ESP32環境でのDir操作

ESP32環境では`picoruby-dir` gemではなく VFS（`/` は littlefs、`/sd` は `picoruby-filesystem-fat`）が使用される。そのため：
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
   - Supervisor がスクリプトの終了時に `picoruby_esp32_midi_cleanup()` を実行
     （以前は MIDI Clock のタイマーコールバックも `stop_requested` を見て呼んでいたが、
     2026-10-23 にクロックを `MIDI::Clock` に一本化したときに外した）：
     - 全チャンネル（0-15）に All Sound Off (CC#120) 送信
     - 全チャンネル（0-15）に All Notes Off (CC#123) 送信
     - MIDI Stop (0xFC) 送信（USB Device 以外の登録済みトランスポートすべて）
     - `MIDI_Clock_reset()` でクロックを停止状態に戻す（F8 は止めない）

3. **MIDI Input タスク停止（C側自動）**
   - `midi_input_task` ループが `stop_requested` を検出
   - タスク終了

4. **Ruby スクリプト終了**
   - `MIDI.bpm_loop` が各イテレーションで `ScriptManager#stop_requested?` をチェック
   - `true` の場合、`send_stop` を送信（必要に応じて）してループを `break`
   - `MIDI::Clock.running?` は C 側の Start / Stop の状態（クリーンアップで停止に戻る）

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

コンソール入力は専用タスク [components/midori-runtime/console_input.c](components/midori-runtime/console_input.c)
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

## UI のオフスクリーン描画は共有ストリップ（2026-10-12）

XYPad / Tombola / Main 上部 / Log は、画面ごとのスプライトではなく UI 全体で 1 本の
**帯（ストリップ）バッファ**を通して描く（[main/ui/ui_strip.h](main/ui/ui_strip.h)）。
以前は Tab5 で計 3.2MB（XYPad 1280x620 = 1.5MB など）のスプライトを持っていたが、
どれも内部 RAM に入らず PSRAM に落ちて描画が遅かった。

- サイズは `UI_SCREEN_WIDTH × 64 行 × 16bpp`（Tab5 160KB / CrowPanel 128KB / CoreS3 40KB）。
  `UIManager::init()` の先頭（= app_main の最初の `platform_init()` の中、ヒープが断片化する前）で
  内部 RAM に確保する。入らなければ 48 → 32 行、最後に PSRAM。起動ログ `UI_STRIP` に
  内部 RAM の空きと最大ブロック、確保結果が出る
- `ui_strip::paint(x, y, w, h, bg, draw)` が矩形を帯に分け、帯ごとに `draw(g, ox, oy)` を呼んで
  転送する（Tab5 / CrowPanel は `ui_fb` の CPU 直接コピー、それ以外は `pushSprite`）。幅が狭い矩形ほど 1 帯の行数が
  増える（Tombola の 620px 幅は 132 行ずつ）
- `draw` は画面座標から `(ox, oy)` を引いて描くこと。帯ごとに全体を描き直し、はみ出しは
  LovyanGFX がクリップする。モデルのスナップショットは帯ごとではなく 1 フレームに 1 回取る
  （帯の境界でずれないように）
- 帯の中身はフレームをまたいで残らないので、差分消去はできない（毎回全面を描く）。
  スプライトのテキスト状態は帯ごとに既定値へ戻している（画面間で持ち越さないため）
- 新しい画面でオフスクリーン描画が要るときも、自前のスプライトを確保せずこれを使う
- **全画面の再描画（画面切り替え・`ui_request_redraw()`）も `ui_strip::compose()` で帯ごとに
  合成して置き換える**（2026-10-12。以前は `enter()` がコンテンツ領域を黒で塗ってから描いて
  いたので切り替えのたびにちらついた）。帯スプライトはバッファの先頭を帯の行ぶん手前に
  ずらした「全幅・画面座標のキャンバス」で、クリップ矩形で帯の行だけに絞ってあるので、
  画面のコードは**絶対座標のまま** `ui_gfx()` に描けば帯に入る
- そのため `main/ui` の描画は `M5.Lcd.` ではなく **`ui_gfx().`** に書く（合成中は帯、
  それ以外は M5.Lcd を返す）。パネル自体の設定（`setBrightness`）と、他タスクから呼ばれ得る
  `ui_set_midi_indicator()` だけは M5.Lcd のまま
- 画面の `enter()` では描かない（状態の初期化だけ）。直後に UIManager が `draw()` を合成で
  呼ぶ。`draw()` は帯ごとに呼ばれるので、毎回同じ絵を全部描くこと（「前回と同じなら描かない」
  キャッシュは `draw()` の先頭で無効化する。例: `ScreenMain::draw()` の `m_lastSyncButtonState`）。
  合成中の `ui_strip::paint()` は帯を使わず合成中のキャンバスへ直接描く
- 全画面再描画が保留中の周回では `screen->update()` を呼ばない（差分描画が旧画面の上に出るため）
- 画面が `update()` の中でコンテンツ領域を丸ごと描き直すとき（Scripts のスクロール、Knobs の
  バンク切り替えなど）は `ui_strip::repaint_content([&]{ ... })` を使う。コンテンツ領域だけを
  同じ仕組みで合成して置き換える。`ui_clear_content_area()` してから直接描くとちらつく

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

## XYPad 画面（2026-08-15）

詳細は [docs/XYPAD.md](docs/XYPAD.md) を参照。

タッチ位置から音程（X）と CC（Y）を連続的に送る演奏面。Pads がワンショット、
Knobs が持続パラメータ、Tombola が自動発音だとすると、XYPad は**指でその場で
弾く**担当。M5Stack 専用（Freenove は no-op スタブ）。

最大 5 本の同時タッチはそれぞれ独立した**スロット**を持つ。X の意味づけ
（スケールへスナップ + グライドか、Y と同じ生の CC か）・CC 番号・チャンネル・
Hold・送信先まで、**指ごとに丸ごと別の設定**にできる。C++ 側（`ui_common.cpp`
の `ui_xypad_*`）はタッチ追跡とスケールへのスナップ・グライド計算だけを持ち、
MIDI を送るのは Ruby（`UI::XYPad` 内蔵ハンドラ、または `on_touch` で自前実装）。

```ruby
pad = UI::XYPad.new(scale: [36, 38, 40, 41, 43, 45, 47, 48],
                    glide_range: 2, y_cc: 74, device: dev)
pad.slot(5, x_mode: :cc, x_cc: 70, y_cc: 71, note: 60, device: fx)  # 5本目だけ別挙動
pad.hold = true   # 全スロット一括ラッチ（pad.slot(n, hold:) で個別も可）
pad.show
```

サンプル: [examples/xypad.rb](examples/xypad.rb)

## Keys 画面（画面の鍵盤 / 2026-10-18）

詳細は [docs/KEYS.md](docs/KEYS.md) を参照。

1〜4 オクターブ（13〜49 鍵）の鍵盤を指（最大 5 本）で弾く。`UI::Keys` は
**picoruby-midi のトランスポートでもあり**、`MIDI.route` の送信元にも宛先にもなる。
宛先として受けたノートは鍵を光らせる（光る MIDI 鍵盤のように押下状態を表示）。
レイアウトでは `keys`（1 レイアウトに 1 つ）。Tab5 のビルドのみ確認、**実機未確認**。

- **発音は C++ 側**（[main/ui/ui_keys.cpp](main/ui/ui_keys.cpp)、UI タスク）。送信先は
  `device:`（`out_mask`）と、鍵盤のビットから張られたルートの宛先（`MIDI_route_targets()`）の OR を
  1 回の `MIDI_transport_send()` で。note off は note on を送ったときの送信先・チャンネルへ
- 送信元にするため picoruby-midi に `MIDI_route_set_source()` / `MIDI_route_sources()` /
  `MIDI_route_targets()` を追加した。`MIDI.route` は送信元を `MIDI._route_sources` で検査する
  （従来は USB Host / UART 固定の `ROUTE_SOURCES`）
- モデルは初回使用時に calloc（約 1KB、.bss に置かない）し、そのときトランスポート登録表に載る
- 色の優先順位は 押下 > 光（チャンネル色）> 鍵ごとの色 > 白鍵 / 黒鍵の地の色。
  色は Symbol / RGB565 / `[r, g, b]`、`UI.rgb(r, g, b)` を追加
- 画面は `UI_SCREEN_KEYS`（= 11、末尾に追加）、Ruby は `:keys` / `UI.keys` / `UI::SCREEN_KEYS`

```ruby
keys = UI::Keys.new(octaves: 2, low: 48, velocity: :position, device: MIDIDevices.amy)
MIDI.route(keys, MIDIDevices.sam2695)          # 弾いた音を SAM2695 にも
MIDI.route(MIDIDevices.usb_midi_host, keys)    # 外部鍵盤の演奏で鍵を光らせる
MIDI.route(MIDIDevices.usb_midi_device, keys)  # PC から来たノートで鍵を光らせる
keys.set_color([48, 52, 55], :blue)            # スケール / 和音の表示
keys.show
```

サンプル: [examples/keys.rb](examples/keys.rb) / [examples/keys_layout.rb](examples/keys_layout.rb) /
[examples/keys_amy.rb](examples/keys_amy.rb)（AMY を鍵盤 + レイアウト + Knobs で）

## Sequencer（UI::Sequencer / 2026-10-25）

詳細は [docs/SEQUENCER.md](docs/SEQUENCER.md)（設計と段階）。段階 1〜4（エンジン・パターン・チェーン・Ruby API・
保存と自動保存・画面 [main/ui/screen_sequencer.cpp](main/ui/screen_sequencer.cpp)・レイアウトの `sequencer`）と段階 5（リアルタイム録音）まで実装。
録音（REC で realtime のラインを arm、`Line#arm`。overdub / replace / quantize）はビルドとホストテストのみで実機未確認。
保存（`.mseq`、`/sd/patterns/`、自動保存 `_autosave.mseq`）はビルドとホストテストのみで実機未確認。
Tab5 の実機で画面表示と Play / Stop による再生を確認（2026-10-25）。外部クロック同期・CoreS3 / CrowPanel は未確認。

- ソング（ライン最大 16 本 × パターン 16 個 + チェーン）と再生は **ESP-IDF 非依存の
  [main/ui/seq_engine.c](main/ui/seq_engine.c)**（ホストテスト `make -C main/ui/host_test`）。
  [main/ui/ui_sequencer.cpp](main/ui/ui_sequencer.cpp) がトランスポート・専用タスク（Core 1 / 優先度 5）・`ui_seq_*` を持つ
- **宛先として受けた MIDI クロックで進む**（F8 / Start / Continue / Stop / SPP）。Sequencer 自身は BPM も再生ボタンも持たない。
  `UI::Sequencer.new` は既定で `UI::Tempo` → Sequencer にクロックだけのルートを張る。受けたノートは「最後に入力された和音」になる
- `send_packet` は時刻を付けてキューに積むだけ（ルートの中からルートを呼ばないため）。F8 の間の 3 tick（内部は 96 PPQN）は
  計測した F8 周期から esp_timer の one-shot で刻む（`CONFIG_FREERTOS_HZ=100` では刻めない）。次の F8 を追い越さない
- ノートは (pattern, line) ごとの**ソート済み配列**（`seq_note_t` 8 byte、grid はステップ単位・realtime は tick 単位）。
  配列ごと 512 / ソング全体 8192 ノート、PSRAM。エンジンは非スレッドセーフで、`ui_sequencer.cpp` が再帰ミューテックスで包む
- ラインの設定（チャンネル・方式・ミュートなど）はソング共通、steps / rate / bars・音色（Program Change / Bank Select）・
  ノートはパターンごと。音色はそのパターンが始まるとき（Start / Continue / 切り替え）にノートより先に送り、ループでは送らない
- スクリプトが終わってもソングは残る（停止と `device:` の解除だけ）。Ruby: `UI::Sequencer.current` で取り直す。
  `UI::Sequencer.new` のソングは一時的（自動保存しない）で、UI モードに戻ると自動保存のソングに戻る
- 保存のファイル I/O は Ruby（C の `fopen` は `/sd` に届かない）。C は `.mseq` への直列化・検証だけ
  （`seq_dump_*` / `seq_load_*`、書き出しはソングのコピーから）。UI モードはスクリプトのたびに起動し直すので、
  自動保存を読むのは電源投入後の最初（C の `restored`）と一時的なソングの後だけ（`main_task_base.rb` の `setup_sequencer`）
- Ruby のホストテスト: `ruby test/sequencer_test.rb`（picoruby-ui ディレクトリで）
- 画面はエンジンを直接読まない。`ui_seq::snapshot()`（[main/ui/ui_sequencer.h](main/ui/ui_sequencer.h)）で見えている範囲を
  ヒープの View に写してから描く（描画中にロックを持つとシーケンサータスクが止まり、拍が遅れる）

## Tempo（UI::Tempo / 2026-10-22）

詳細は [docs/TEMPO.md](docs/TEMPO.md)。**実機未確認**。

- Tempo は **MIDI クロックを送るデバイス**。クロック本体は picoruby-midi の **`MIDI::Clock`**
  （プロセスに 1 つ、gem の port の esp_timer。2026-10-23 に Tempo 自前のタイマから一本化）で、
  [main/ui/ui_tempo.cpp](main/ui/ui_tempo.cpp) は UI とのつなぎだけ。`MIDI.route(UI::Tempo, dev)` =
  `MIDI.route(MIDI::Clock, dev)` / MIDI Devices 画面（IN の行のみ、`UI_MIDIDEV_NO_OUT`）で任意のデバイスへ送る。
  F8 はクロックからのルートがある間だけ送られ、Start / Stop（画面の Play / Stop、`UI::Tempo.start` / `.stop`
  = `MIDI::Clock.start` / `.stop`）とは独立
- テンポは UI の内部 BPM = `MIDI::Clock` のテンポ。スクリプトが `MIDI::Clock.bpm=` で変えると UI タスクが拾う。
  Sync 中は `MIDI_Clock_set_follow_bpm()` で外部クロックに従う
- `MIDI.bpm_loop` / `MIDI.start!` は、port があって `output:` が登録済みトランスポートなら自分で F8 を送らず、
  ループの間だけ `MIDI::Clock` を `output` へルートしてクロックの tick に合わせてブロックを呼ぶ（既存のパッチは残す）。
  `bpm` を省略すると現在のクロックのテンポに従い、指定するとクロックのテンポを設定する。`output:` なしは従来どおり
  Ruby の計時（`60000/bpm/subdivisions`）でブロックを呼ぶ（テンポの変化には追従）。ホストテスト:
  `ruby host_test/clock_test.rb`（picoruby-midi ディレクトリで）
- 画面の -10 / -1 / +1 / +10 ボタンと Bar / Beat 表示・拍のバーは削除。BPM は BPM 表示のスワイプ（1 BPM 刻み、
  2026-10-24）か `UI::Tempo.bpm` / `bpm=`（ノブ・パッドからはスクリプトでつなぐ）。TAP は BPM 表示のタップかナビバー
  （押下で TAP を数え、スワイプになったらその TAP を取り消す）
- 画面は `setArea()` の矩形に収まるよう配置を計算し、レイアウト DSL の `tempo`（`UI_WIDGET_TEMPO`）でセルに置ける

## MIDI Devices 画面（UI::MIDIDevices / 2026-10-22）

詳細は [docs/UI_MIDI_DEVICES.md](docs/UI_MIDI_DEVICES.md)。旧 MIDI Info 画面（`UI_SCREEN_MIDI_INFO` = 2 を
流用、Ruby は `:midi_devices`）。**実機未確認**。

- 行 = MIDI IN（ルートの送信元）、列 = MIDI OUT のクロスバー。セル = `MIDI.route` のルート
  （画面はルート表をそのまま読む）。最後の列 **Prog(input)** / 行 **Prog(output)** でプログラムの IN / OUT を選ぶ
  （`UI::MIDIDevices.midi_out` / `.midi_in`、選択に追従する入力は `.input`）
- モデルは [main/ui/ui_midi_devices.cpp](main/ui/ui_midi_devices.cpp)（デバイス一覧・選択・NVS `midi_devs`）。
  内蔵デバイスは**どのモードでも起動時に** `main_task_base.rb` の `setup_midi_devices` が登録し、
  保存済みパッチを張り直す（スーパーバイザが切り替えのたびにルートを消すため）。NVS のキーは
  ビットではなく `usb_host` / `uart` / `usb_dev` / `amy` / `keys`（AMY と Keys のビットは起動ごとに変わる）
- **AMY は standby**（ビット 0）で登録し、保存済みパッチ・選択・画面で必要になったとき初めて起動する
  （起動するとオーディオ・I2S・Tab5 の USB パッドに触るため）。画面の鍵盤は C（`ui_keys.cpp`）が自分で登録
- `midi_out` / `midi_in` が返すのは**その時点で選ばれている機器**。変数やプレイヤに渡すと選び直しに
  追従しないので、保持するなら `UI::MIDIDevices.output`（OUT に追従する `MIDI::Device`、切り替え時に前の OUT へ
  All Notes Off）/ `.input` を使う。C から送るもの（`UI::Keys` / `UI::Tombola` の `device:`、`MIDI.route`、
  `MIDI.start!(output:)` のクロック）はビットを覚えるので `on_change` の `:select_out` で渡し直す
- 画面で変えたパッチ（両端が内蔵）と選択は即 NVS に保存。スクリプトの `patch` / `midi_in=` は
  スクリプト限り（`UI::MIDIDevices.save` で保存）
- Ruby のホストテスト: `ruby test/midi_devices_test.rb`（picoruby-ui ディレクトリで）

## Home メニューと画面切り替え（2026-10-12）

詳細は [docs/UI_NAVIGATION.md](docs/UI_NAVIGATION.md) を参照。

ナビバーの左右は **Home / Back**（以前は `<` `>` で全画面を巡回）。各画面へは Home
（[main/ui/screen_home.cpp](main/ui/screen_home.cpp)）のタイルから行く。起動時も Home。
Back は Home を除いた直前の画面で、押すたびに 2 画面を往復する。旧 Main 画面の
タイトルは **Tempo**（`UI_SCREEN_MAIN` / `:main` は互換のため残している）。

- **画面の切り替えは UI タスクだけが行う**。他タスク（Ruby の `UI.show` など）は
  `UIManager::requestScreen()` で 1 件分の保留に積み、`update()` の先頭で反映される。
  以前の `UI.set_screen` は PicoRuby タスクから `setScreen()` を直接呼んでいて、
  `leave()` / `enter()` が UI タスクの描画と並行していた
- 画面切り替えのたびに `:screen_change`（`screen:` / `from:` / `source:` / `layout:`）を発行する
- Ruby: `UI.show(:knobs, bank: 2)`、短縮形 `UI.knobs` / `UI.xypad` / `UI.tempo` …、
  `UI.screen` / `UI.back` / `UI.next_screen` / `UI.prev_screen`、`UI.menu(hide:/only:)`。
  移動できないときは例外ではなく false（MIDI 入力のハンドラから気軽に呼べるように）
- `UI.menu` の設定とレイアウトはスクリプト終了時に `picoruby_esp32_midi_cleanup()` →
  `picoruby_ui_cleanup()` で元に戻る
- 画面を足すときは `ui_screen_index_t` の**末尾**に追加し（Ruby の `UI::SCREEN_*` と
  `SCREEN_NAMES` も）、`ui_manager.cpp` の `MENU_ORDER` に載せる。Home のタイルに状態を
  出すなら `Screen::getTileStatus()` を実装する

## レイアウト DSL（2026-10-12）

詳細は [docs/UI_LAYOUT_DSL.md](docs/UI_LAYOUT_DSL.md) を参照。

pad / knob / XYPad / Tombola / Keys / Tempo を 1 画面にグリッドで並べる。レイアウトの数に上限はなく
（2026-10-24 まで 4）、それぞれが Home のタイル（多ければ Home を縦にスクロール）。**列・行は 0 始まり**、範囲は Range（`col: 0..2`）。

```ruby
UI.layout("Perform", cols: 4, rows: 3) do
  areas <<~GRID
    xy xy xy k
    xy xy xy k
    p  p  p  p
  GRID
  xypad area: :xy, scale: [48, 50, 52], device: dev
  knob area: :k, label: "Cutoff" do |v| dev.control_change(74, v.to_i) end
  grid(area: :p, cols: 4) { 4.times { |i| pad col: i do dev.trigger(36 + i) end } }
end.show
```

- **グリッドの解決（トラック・areas・入れ子・検証）は Ruby**
  （[mrblib/ui_layout.rb](mrbgems/picoruby-ui/mrblib/ui_layout.rb)）。C++ には画素の矩形だけが
  渡る。ピュア Ruby なのでホストでテストできる:
  `ruby test/layout_test.rb` / `cat mrblib/ui_layout.rb test/layout_test.rb | mruby`
  （picoruby-ui ディレクトリで。mruby 版は `RBENV_VERSION=mruby-3.2.0`）
- **mruby は末尾のキーワード引数を位置引数の Hash にまとめない**（CRuby はまとめる）。
  `def add(kind, params)` に `add(:pad, label: "x")` と渡すと ArgumentError。
  `{ label: "x" }` と明示する（ホストの mruby テストで発覚）
- C 側はレイアウトを丸ごと組み立ててから差し替える（`ui_layout_begin` / `add_*` /
  `commit`）。保持は [main/ui/ui_layout.cpp](main/ui/ui_layout.cpp)（再帰ミューテックス、
  ヒープ確保）、画面は [main/ui/screen_layout.cpp](main/ui/screen_layout.cpp)
- pad / knob の描画は [main/ui/ui_widgets.cpp](main/ui/ui_widgets.cpp) で Pads / Knobs 画面と共有
- XYPad / Tombola はそれぞれの画面オブジェクトを、表示中だけ部品のセルに狭めて借りる
  （`setArea()` / `resetArea()`）。モデルが 1 つなので 1 レイアウトに 1 つまで
- 全画面合成中の `ui_strip::paint()` は自分の矩形にもクリップする（隣の部品へのはみ出し防止）
- 新しい部品（Sequencer など）は `ui_widget_kind_t` に足し、`ScreenLayout` に描画とタッチを、
  `ui_layout.rb` の `Builder` にメソッドを足す

サンプル: [examples/layout.rb](examples/layout.rb)

## Elecrow CrowPanel Advanced 7inch 対応（2026-08-28）

詳細は [docs/CROWPANEL.md](docs/CROWPANEL.md) を参照。

ESP32-P4 + 1024x600 MIPI-DSI (EK79007) + GT911。SoC は Tab5 と同じ
ESP32-P4NRW32 なので PSRAM / Flash / チップリビジョンの設定は Tab5 と同じ。
`./switch_board.sh crowpanel`（既定 `host`）。

**M5GFX はこのボードを自動判別できない**ので、
[main/platform/crowpanel_display.cpp](main/platform/crowpanel_display.cpp) が
LovyanGFX のパーツ（`Bus_DSI` + 自前の `Panel_EK79007` + `Touch_GT911` +
`Light_PWM`）を組み立て、`M5GFX::init(Panel_Device*)` で `M5.Lcd` に差し込む。
このメソッドは自動判別を丸ごと飛ばすので、**main/ui 以下は無改造で動く**。
`M5.begin()` は呼ばない（このボードに無い PMIC / IMU / IO エキスパンダを
探しに行くだけ）。M5Unified から使うのはタッチだけなので
`M5.Touch.begin(&M5.Display)` を手で呼び、`platform_update()` では
`M5.update()` ではなく `M5.Touch.update()` を叩く。

画面レイアウトの分岐は `CONFIG_USB_MIDI_BOARD_M5STACK_TAB5` ではなく
[ui_common.h](main/ui/ui_common.h) の `UI_LAYOUT_LARGE` を見るように変えた
（Tab5 と CrowPanel が共有する「広い画面向けレイアウト」）。ジオメトリ定数
（UI_SCREEN_*, UI_PAD_*, UI_KNOB_*）だけがボードごとに分かれる。

UI をビルドするかどうかも派生シンボル `CONFIG_USB_MIDI_UI_ENABLED`
（CoreS3 / Tab5 / CrowPanel で y）に集約した。

**SD カードは SDMMC スロット 0**（2026-08-28）:
CLK 43 / CMD 44 / D0 39 は ESP32-P4 の**スロット 0 専用 IOMUX ピン**そのもので、
P4 のスロット 0 は GPIO マトリクスを通せない。つまりピンを正しく設定しても
スロット 1 のままではカードは見つからない。`picoruby-filesystem-fat` の ESP32
ポートは `SDMMC_HOST_SLOT_1` 決め打ちだったので、`BoardConfig::SD_SLOT` /
`SD_FREQ_KHZ` → `FAT.init_sdmmc` と渡せるようにした（2026-10-11 からは
`FAT.new(:sd, sdmmc_slot:, sdmmc_freq_khz:)` 経由）。他ボードは `-1`（自動判定 → 従来どおりスロット 1）。

**"USB 2.0" コネクタは HS ポート**（2026-08-29）:
ESP32-P4 のデバイス側ポートは 2 つ（FS = OTG1.1 / HS = OTG2.0）あり、
CrowPanel の "USB 2.0" は **HS** 側。`picoruby-usb_midi_device` は
`TINYUSB_PORT_FULL_SPEED_0` 決め打ちだったので、繋がっていないパッドを
叩いていた（install は成功しログも出るが、ホストからは何も見えない）。
gem に `USB_MIDI_DEVICE_HIGH_SPEED`（既定 0）を追加し、CrowPanel だけ 1 を注入する。
HS では **HS 用コンフィグディスクリプタ**（バルク EP が 64→512 byte）と
**Device Qualifier** が必須なので gem 側に追加した。P4 の FSLS PHY mux 切り替え
（`USB_MIDI_DEVICE_P4_PHY_SWAP`、Tab5 用）は HS では無関係なので既定 0 になる。

**Tab5 のビルドを焼くと「起動しない」ように見える**（2026-08-28）:
ログが `TinyUSB Driver installed on port 0` で止まって無音になる。ハングではなく、
その直後の `tinyusb_console_init()` が stdout を誰も見ていない USB-C へ
張り替えるだけ（`CONFIG_USB_MIDI_CONSOLE_ON_CDC`）。CrowPanel の `midi_device`
モードでもコンソールは "USB 2.0" 側の CDC に移る。書き込みは常に CH343 UART
ブリッジなので、UART でログを見たいときは `host` モードでビルドする。

## AMY シンセサイザー（2026-10-03）

詳細は [docs/AMY_SYNTH.md](docs/AMY_SYNTH.md)、API は
[mrbgems/picoruby-amy/README.md](mrbgems/picoruby-amy/README.md) を参照。

Tab5 / CrowPanel の内蔵スピーカーから [AMY](https://github.com/shorepine/amy) で
音を出す。CoreS3 / Freenove は対象外。**Tab5 では内蔵スピーカーから正しく鳴ることを確認済み**
（2026-10-04）。CrowPanel はビルドのみで実機は未確認。

- gem `picoruby-amy`（`require 'amy'`）が AMY エンジン・I2S・オーディオタスクを持ち、
  picoruby-midi のトランスポートとして振る舞う：`MIDI::Device.new(MIDIDevices.amy)`
- AMY エンジンは IDF コンポーネント `components/amy`（ソースは gem の `lib/amy`）
- ボード依存（Tab5 の ES8388 + SPK_EN、CrowPanel の GPIO 30 アンプ電源）は
  [main/platform/board_audio.cpp](main/platform/board_audio.cpp) が電源コールバックとして注入。
  ピン等は `components/midori-runtime/CMakeLists.txt` から `AMY_GEM_*` 定義で注入
- **オーディオタスクは Core 0 / 優先度 3 の 1 本だけ**（描画 + I2S 書き込み）。
  AMY 内部のレンダタスクは使わない。Core 1 に置くと Ruby VM（優先度 3）に止められる
- **Tab5 の I2S（DOUT=26 / BCLK=27）は USB PHY1 のパッドを使う**。ESP-IDF の
  `gpio_ll_func_sel()` は 24〜27 を別機能にするとき「PHY0 = USB-Serial/JTAG、PHY1 = OTG1.1」
  という既定の mux を前提に USB パッドを切るが、Tab5 の midi_device モードは
  OTG1.1 を PHY0（USB-C）へ付け替えている。そのため I2S の起動で **USB-C の
  TinyUSB（CDC コンソール + USB-MIDI デバイス）が切れ**、PHY1 を実際に持つ USJ の
  パッドは I2S の下で生きたまま残っていた（2026-10-04、irb の `AMY.start` で
  コンソールが止まり、パッドも無音）。gem の port（`p4_usb_pads_fixup()`）が I2S 設定の
  前後でパッド有効フラグを保存・復元し、mux の実状に合わせて I2S のピン側の
  コントローラだけを切る
- オーディオタスク（Core 0）はコンソールに書かない（stdout / stderr を `/dev/null` に。
  起動ログは `AMY_GEM_start()` を呼んだ側で出す）。`AMY::FM` / `AMY::Synth` は synth フラグ 8
  （`SYNTH_FLAGS_NO_NOTE_WARNINGS`）を立て、esp_timer から届く自動ノートオフで
  AMY が警告を出さないようにしている。いずれも CDC コンソールへの他コアからの書き込みを
  避けるための予防
- **マスターボリュームは Settings 画面**（スピーカー / ヘッドホン、NVS に保存。2026-10-12）。
  Tab5 はヘッドホンジャックがスピーカーアンプと同じ ES8388 出力にぶら下がっているので、
  出力音量（reg 46〜49）を挿入中（0x43 エキスパンダ P7 = HP_DET）はヘッドホンの値にし、
  スピーカーは SPK_EN だけで切る（**コーデック出力を切るとイヤホンも鳴らなくなる**）。
  CrowPanel はスピーカーのみで、gem の `AMY_GEM_set_output_gain()` でサンプルを減衰させる。
  詳細は docs/AMY_SYNTH.md の「マスターボリュームとヘッドホン」
- Tab5 の MCLK は 128 × fs（ES8388 reg24 = 0x00 と対）。片方だけ変えるとピッチが 2 倍ずれる
- スクリプト停止時に `picoruby_esp32_midi_cleanup()` が `AMY_GEM_reset()` を呼び、
  synth・パッチ・CC マッピングを消す（オーディオは止めない）
- 音源クラスは 2 つ：`AMY::FM`（DX7 プリセット 128〜255）と `AMY::Synth`
  （オシレータ最大 4 + LFO 1 + フィルタ + ADSR × 2。Juno パッチ 0〜127 と同じ osc 配置で、
  読み込んだプリセットを `AMY._synth_state` で読み戻してノブに反映する。2026-10-04、実機未確認）。
  トランスポートは `AMY::Transport`（旧名 `AMY::Synth`、`fm.transport` / `syn.transport`）。
  サンプル: [examples/amy_fm.rb](examples/amy_fm.rb) / [examples/amy_synth.rb](examples/amy_synth.rb)
- エコーは有効（`features.echo = 1`）。ディレイライン（約 512 KB、PSRAM）はレベルを
  初めて 0 より上げたときに確保される
- アプリが 2 MB を超えたので `partitions.csv` の factory を 0x3F0000 に拡張し、
  **0x310000 に移した**。PicoRuby のフラッシュボリューム（当時は FAT の `flash_disk.c`、
  2026-10-11 からは littlefs の `flash_hal.c`）は**固定アドレス
  0x210000〜0x310000** を直接使う（パーティション表を見ない）ので、ここに重ねると
  起動時のフォーマットでアプリが壊れて画面が真っ暗になる（2026-10-04 に発生）

### picoruby-midi のトランスポート登録表（2026-10-03）

`MIDI_Note_trigger()` / ノートスケジューラ / スクリプト停止時のクリーンアップは、
送信先を `MIDI_transport_send(mask, ...)` で登録表から引く。USB Host 0x01 / UART 0x02 /
USB Device 0x04 は互換のため固定ビット（`ports/esp32/midi.c` のコンストラクタで登録）、
新しいトランスポート gem は `MIDI_transport_register()` で 0x08 以降のビットを得て、
それを Ruby の `transport_id` として返す。picoruby-midi 側の改修は不要。

### MIDI.route（MIDI Thru / 2026-10-04）

`MIDI.route(from, to, channel: nil)` で、受信をトランスポート間で **C の入力タスクが直接転送**する
（Ruby を通らない）。受信元は USB-MIDI Host と UART と USB-MIDI Device（デバイスから張られたルートがある間だけ入力タスクが読む。
その間 `read_available` とは併用しない）（と、自分で転送する送信元として宣言したトランスポート。例: `UI::Keys`）、送信先は登録表の任意のトランスポート。
`MIDI.unroute(from, to = nil)` / `MIDI.unroute_all`。スクリプト停止時に
`picoruby_esp32_midi_cleanup()` が `MIDI_route_clear()` で全消去する。

- 入力タスクは「`MIDI::Input` が読んでいる」か「ルートがある」間動き、Ruby 用キューへの積み込みは
  前者のときだけ（`MIDI_Input_set_queueing()`）
- 入力タスクの優先度は 4（Core 1、Ruby VM の 3 より上）。ルーティングを VM に止めさせないため。
  ループの待ちは**最低 1 tick**（`CONFIG_FREERTOS_HZ=100` では `pdMS_TO_TICKS(5)` が 0 になり
  `vTaskDelay(0)` は待たない）。0 tick のまま優先度を上げると Core 1 の VM が止まり、
  UI 入力が一切効かなくなる（2026-10-04 に発生）
- 詳細は [docs/AMY_SYNTH.md](docs/AMY_SYNTH.md) の「他デバイスからの入力を AMY へ繋ぐ」
- `MIDI.route` は `MIDI::Route` を返す（失敗時は false）。`channel: :clock`（C では `MIDI_ROUTE_CLOCK_ONLY` = -2）で
  クロック・Start / Continue / Stop・ソングポジションだけを通す（MIDI Devices の長押しメニューの「Clock」）
- ルートが通すものは内部では**集合**（`MIDI_ROUTE_SEL_*`: ビット 0〜15 = チャンネル、`SEL_CLOCK`、`SEL_ALL`。
  ALL は単独）。`channel: [0, 9, :clock]` / `MIDI_route_add_selection()` で 1 件のルートに複数のチャンネルと
  クロックを載せる。`MIDI_route_get()` の channel は複数なら `MIDI_ROUTE_SEVERAL`（-3）、集合は
  `MIDI_route_get_selection()`。MIDI Devices の長押しメニューは All / Off 以外を複数選べる（2026-10-24）

### MIDI.filter / MIDI.on（2026-10-22）

詳細は [docs/MIDI_FILTER.md](docs/MIDI_FILTER.md)。ルートに Ruby のブロックを差し込む。
`MIDI::Input#on` の後継（`MIDI::Input` は非推奨、初回の new で警告）。

```ruby
MIDI.route(kb, syn).filter(types: [:note_on, :note_off]) { |m| m.note += 12; m }  # Message / Array / nil
MIDI.on(kb, :note_on) { |m| puts m.note }   # 宛先のない filter（USB Device / UI::Keys からも受けられる）
MIDI.bpm_loop(120, sync_from: kb) { |c| ... }
```

- **C で絞ってから Ruby に回す**: `types:` / `channel:` に合うパケットの、filter 付きの宛先の分だけ
  キューへ。それ以外（クロックなど）と他の宛先は従来どおり C から直接。filter 表はルート表とは別
  （`MIDI_FILTER_MAX` 16 件、(送信元, 宛先ビット) ごと）。listener の宛先は `MIDI_ROUTE_DST_RUBY`（0x100）
- ブロックは `MIDI.run_filters` で走る（`MIDI.sleep_ms` / クロックループ / `UI.process`）。filter が
  あるときの待ちはキューを待ちながら寝る（`MIDI_Filter_wait`）。素の `sleep_ms` では走らない
- 自分で送る送信元（`UI::Keys`）は `MIDI_route_packet()` ではなく `MIDI_route_dispatch()` を呼ぶこと
  （呼ばないと filter / listener に届かない）
- ホストテスト: `make -C mrbgems/picoruby-midi/host_test`（C）、`ruby host_test/filter_test.rb`（Ruby、gem ディレクトリで）
- 入力タスクが USB Device / UART を読むのはルート・filter・**watch**（`MIDI_route_set_watch`）のある間だけ。
  外部クロックの BPM もそのときしか測れない。Tempo 画面は表示中か Sync 中に使える送信元を watch し
  （`UIManager::updateClockWatch`、送信元ボタンは USB-H / USB-D / DIN / BLE）、`sync_from:` も指定元を watch する

### 名前付き filter（MIDI::Filter / register_filter / /sd/filters / 2026-10-24）

詳細は [docs/MIDI_FILTER_REGISTRY.md](docs/MIDI_FILTER_REGISTRY.md)。ホストのテストのみで**実機未確認**。

- `MIDI::Filter` の派生クラスで書き、インスタンスを `MIDI.register_filter(:name, Klass.new)` で登録する
  （`unregister_filter`）。登録したインスタンスはプロトタイプで、ルートに付けるときは
  **ルートごとに `instantiate` した別のインスタンス**（状態・パラメータはパッチごと）
- 宣言は `label` / `types`（`types []` で何も Ruby に回さない）/ `channel` / `param name, kind, ...`
  （`:int` `:enum` `:bool` `:note` `:channel`、8 個まで）/ `clock :internal`（`MIDI::Clock` の F8 で `on_clock`）。
  インスタンスでは `setup` / `call(m)` / `teardown` / `param_changed` / `layout(ui)` / `emit` / `every(ms)` / `output`
- チェーン: `MIDI.route(a, b).chain(:chord, [:arp, { rate: :"1/8" }])` / `MIDI.chain(a, b, ...)`。選んだ順に通る。
  C の filter 表は 1 件（チェーン全体の types の和集合）。チェーンは出口で鳴らしたノートを覚え、外すときに止める
- 実装は picoruby-midi `mrblib/midi_filter_class.rb`（**mruby 専用**。mrbgem.rake が mruby/c のビルドから外す）。
  ホストテスト: `ruby host_test/filter_class_test.rb`（gem ディレクトリで。mruby は cat で連結、ファイル先頭参照）
- `/sd/filters/*.rb` を**どのモードでも起動時に** `load_filters`（`main_task_base.rb`）が名前順に読む
  （`_` で始まるファイルとサブディレクトリは読まない）。`setup_midi_devices` の前なので保存済みチェーンが見つかる
- MIDI Devices 画面の長押しメニューに **Ch / Filters タブ**。Filters で選んだ順にチェーン、選んだものを長押しで
  パラメータ画面。セルには `f2`（チェーンの長さ）。モデルは [main/ui/ui_filters.cpp](main/ui/ui_filters.cpp)
  （登録済み filter の宣言とパッチごとのチェーン。Ruby とは `name(param:value,...)` のテキストでやりとり）、
  Ruby 側の同期は picoruby-ui `mrblib/ui_midi_filters.rb`（`UI::MIDIDevices._poll` で画面の変更を適用。
  ホストテスト `ruby test/midi_filters_test.rb`）。両端が内蔵デバイスのチェーンは NVS `midi_devs/chains`
- **UI タスク（= main タスク）のスタックは 3.5KB**。チェーン（約 530 byte）や filter の宣言（約 800 byte）を
  UI タスクのローカルに置かない（画面はヒープの Scratch を使う。`ui_filters.cpp` の UI タスクから呼ばれる関数も同様）
- filter の `layout(ui)` は**チェーンで使われている間だけ**のレイアウト（Home のタイル）。外れたら消す
  （表示中なら MIDI Devices 画面へ戻ってから）。UI モードのループは `UI.process` + `MIDI.sleep_ms(100)` に変えた
  （以前の素の `sleep_ms` では filter もパネルも動かない）
- サンプル: [examples/filters/](examples/filters/)（chord / arp / channel（チャンネル変換、`:chmap`）/ lumi。`/sd/filters/` にコピー）

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

### Tab5 の Quick Charge が USB-C のデータを壊す（2026-10-24）

**症状**: Tab5（`midi_device` モード）を Ableton Push 3 standalone の USB-A につなぐと、
MIDI 機器として一瞬認識されてはすぐ外れる、を繰り返す。Mac / PC では起きない。
MIDI Devices 画面の USB Device 詳細で `rst`（バスリセット）が増え続け、
`setup / rst` ≈ 8（= 列挙は SET_CONFIGURATION まで通っている）。CDC を外しても変わらない。

**原因**: M5Unified の Tab5 初期化が IO エキスパンダ 0x44 の `nCHG_QC_EN`（P5）を
Low = **Quick Charge 有効**のままにしている。充電対応の USB ホストポートにつなぐと、
充電器の QC ハンドシェイクが D+/D- に乗ってホストがポートをリセットする
（充電を止めずに QC だけ止めた状態でリセットが止まることを確認済み）。

**対処**: `platform_m5stack.cpp` で起動時に P5 を High にして QC を止める（充電は 5V で続く）。
Tab5 の USB-C はどのモードでもデータ用なので常に止めている。
`M5.Power.setUsbOutput()` は Tab5 では何もしない（CoreS3 専用）。

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
- `components/midori-runtime/picoruby_supervisor.c` - Supervisor実装
- `components/midori-runtime/mrblib/main_task_base.rb` - メインRubyコード

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
| crowpanel | host (既定) | ○ (47,48 / UART1) | ○ (USB-C 2.0) | - |
| crowpanel | midi_device | ○ (47,48 / UART1) | - | ○ (USB-C 2.0 / TinyUSB) |

CrowPanel の USB-C は 2 つ。`UART`（CH343 ブリッジ）は**常に**書き込み用で、
`USB 2.0` は ESP32-P4 の**高速 (HS) ポート**に繋がっている。`serial` モードは
USB-Serial/JTAG がコネクタに出ていないため選べない。
詳細は [docs/CROWPANEL.md](docs/CROWPANEL.md)。

ESP32-S3ボード（m5stack / freenove）はUSBコネクタが1つ・USB PHYも1つなので、
Host（USB-OTG）とDevice（USB-Serial/JTAG または TinyUSB）は**排他**。
Tab5はUSB-Aが常にHostなので、モードはUSB-Cポートの役割のみを決める。

### アーキテクチャ

**設定ファイル**:
- `components/midori-runtime/mrblib/board_config.rb.in` - ボード設定テンプレート
- `components/midori-runtime/mrblib/board_config.rb` - CMakeで自動生成（DO NOT EDIT）
- `components/midori-runtime/CMakeLists.txt` - ボード別設定の定義

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

1. `components/midori-runtime/CMakeLists.txt` を編集
2. 該当するボード設定セクション（`if(CONFIG_USB_MIDI_BOARD_*)`）で以下を設定：
   - `SAM2695_TX_PIN` / `SAM2695_RX_PIN`
   - `HAS_SAM2695` (true/false)
3. リビルド: `idf.py build`

`HAS_USB_MIDI_HOST` / `HAS_USB_MIDI_DEVICE` はボード分岐では設定しない。
USBポートモード（後述）から自動導出される。

画面付きの新ボードを足すときは、これに加えて：

4. `main/Kconfig.projbuild` にボードを追加し、派生シンボル
   `USB_MIDI_UI_ENABLED`（UI をビルドするか）と
   `USB_MIDI_CONSOLE_ON_CDC`（コンソールが CDC に乗るか）の既定に足す
5. `main/ui/ui_common.h` にジオメトリブロックを追加。広い画面なら
   `UI_LAYOUT_LARGE` の条件にも足す（各 screen_*.cpp はこれだけを見る）
6. `main/CMakeLists.txt` にプラットフォーム実装を追加
7. `sdkconfig.defaults.<board>` と `switch_board.sh` のボード一覧

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

**コンソールがどこに出るかは別の派生シンボルが決める**：
`CONFIG_USB_MIDI_CONSOLE_ON_CDC`（`midi_device` モードで y）。`console_input.c` と
`USB_MIDI_DEVICE_WITH_CDC` の注入はこのシンボルを見る。

**TinyUSB がどちらのポートに載るかも板ごとに違う**：ESP32-P4 は FS (OTG1.1) と
HS (OTG2.0) を持ち、コネクタはどちらか一方にしか繋がっていない。
`USB_MIDI_DEVICE_HIGH_SPEED` を CMakeLists から注入する（Tab5 = FS / CrowPanel = HS）。

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
- `components/midori-runtime/CMakeLists.txt` … 上記から `HAS_USB_MIDI_HOST` /
  `HAS_USB_MIDI_DEVICE` を導出して board_config.rb を生成。加えて
  `CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE=y` のとき
  `target_compile_definitions` で gem に USB アイデンティティを注入する
  （後述）
- `picoruby-usb_midi_device/ports/esp32/*.c` … gem 自身は Midori の Kconfig を
  参照せず `USB_MIDI_DEVICE_ENABLED` / `USB_MIDI_DEVICE_WITH_CDC` でガード
  （未定義時はスタブ / MIDI 単機能）。P4 の PHY mux 切り替えは
  `CONFIG_IDF_TARGET_ESP32P4` 限定で、S3 では ESP-IDF の `usb_phy` ドライバが
  PHY を USB-OTG に引き渡す
- `components/midori-runtime/build_config/picoruby_gems.rb` … S3 でも
  `picoruby-usb_midi_device` gem をビルドに含める

### picoruby-usb_midi_device への設定注入（2026-08-02）

gem は製品名を持たない汎用 USB-MIDI デバイス（既定は MIDI 単機能・
"PicoRuby MIDI"）で、Midori 固有の値はすべてビルド定義で外から与える。
一覧と既定値は
[mrbgems/picoruby-usb_midi_device/include/usb_midi_device_config.h](mrbgems/picoruby-usb_midi_device/include/usb_midi_device_config.h)。

`components/midori-runtime/CMakeLists.txt`（ボード分岐の直後）:

| 定義 | 値 |
|------|-----|
| `USB_MIDI_DEVICE_ENABLED` | 1（`CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE` のとき） |
| `USB_MIDI_DEVICE_WITH_CDC` | `CONFIG_USB_MIDI_CONSOLE_ON_CDC`（コンソール / `idf.py monitor` を同じコネクタに載せるため） |
| `USB_MIDI_DEVICE_HIGH_SPEED` | CrowPanel のみ 1（P4 の HS ポートに配線されているため） |
| `USB_MIDI_DEVICE_MANUFACTURER` | `"Midori"` |
| `USB_MIDI_DEVICE_PRODUCT` | `"Midori Tab5"` / `"Midori CoreS3"` / `"Midori Freenove"` / `"Midori CrowPanel"` |
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
