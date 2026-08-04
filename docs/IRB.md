# irb — USB CDC 経由のインタラクティブモード

## 概要

シリアルコンソール（`midi_device` モードでは USB-C の CDC）から **irb** を起動し、
Ruby の式をその場で評価する機能。R2P2 の irb と同じく
[picoruby-shell](../components/picoruby-esp32/picoruby/mrbgems/picoruby-shell/mrblib/shell.rb)
の `Shell#start(:irb)` を使い、1 行ごとに **PicoRuby の Sandbox** でコンパイル /
実行する。

irb は Supervisor から **SD カードのスクリプトと同じ扱い**で起動される
（専用の PicoRuby タスク + まっさらな VM、終了したら UI モードに戻る）。
`Kernel.load` もスクリプトを Sandbox で実行する（[picoruby-require の
`load_file`](../components/picoruby-esp32/picoruby/mrbgems/picoruby-require/mrblib/require.rb)）
ので、irb とスクリプトは実行機構まで揃っている。

## 使い方

```
> irb
Starting irb...
irb Mode
PicoRuby irb - M5Stack Tab5 (USB-MIDI Device)
quit / exit / Ctrl-D to leave, Ctrl-C to abort an expression

irb> 1 + 1
=> 2
irb> sam = MIDI::Device.new(MIDIDevices.sam2695)
=> #<MIDI::Device>
irb> sam.trigger(60, 100, duration: 300)
=> nil
irb> quit
Leaving irb
irb finished, exiting to supervisor
>
```

| 操作 | 動作 |
|-----|------|
| `quit` / `exit` / Ctrl-D | irb を終了し UI モードへ戻る |
| Ctrl-C | 実行中の式を中断（入力中なら行をクリア） |
| ↑ / ↓ | ヒストリ（メモリ上、最大 10 件） |
| 行末 `\` / 未完の式 | 継続行（`sandbox.compile` が通るまで入力を続ける） |

M5Stack の Scripts 画面には irb 実行中も `[Running] :irb` と `[Stop]` が出る。
Stop を押すか、別のスクリプトを選ぶと irb は終了する（後述のとおり強制停止なので
数秒かかる）。

**ターミナルについて**: 起動時に `IO.wait_terminal` で端末を判定する。ANSI 端末
（PicoRuby web terminal、`screen`、`minicom` 等）ならカーソル位置や画面幅を使った
描画になり、応答しない端末（`idf.py monitor` など）は `TERM=dumb` として 24x80
固定で動く。どちらでも入力・評価は同じように使える。

## アーキテクチャ

```
[console タスク]                    [Supervisor タスク]        [PicoRuby タスク]
 "irb" 行を解釈
   supervisor_request_irb() ──────> CMD_LOAD_SCRIPT
                                     stop_picoruby_task()
                                     midi cleanup / cleanup_vm()
                                     start_picoruby_task(":irb") ──> main_task.rb
                                                                      irb_requested? → true
 raw モードへ <───────────────────────── sm.irb_begin
 受信バイトを                                                        Shell#start(:irb)
 picorb_hal_stdin_push() ─────────────────────────────────────────>   Editor::Line
                                                                       ↓ 1 行ごと
                                                                      Sandbox#compile
                                                                      Sandbox#execute
 line モードへ <───────────────────────── sm.irb_end (ensure)         quit で break
                                     EVT_TASK_COMPLETED <──────────── タスク終了
                                     UI モードで再起動
```

### Supervisor 層（[picoruby_supervisor.c](../components/picoruby-esp32/picoruby_supervisor.c)）

irb は擬似スクリプトパス `SUPERVISOR_IRB_PATH`（`":irb"`）として、通常の
「要求スクリプト」スロットを流れる。こうすることで停止・VM クリーンアップ・
UI モード復帰といったライフサイクル処理を一切分岐せずに再利用できる。
先頭のコロンで `/sd` の実ファイルと区別する。

| API | 用途 |
|-----|------|
| `supervisor_request_irb()` | irb セッションを要求（`supervisor_request_script(":irb")` の別名） |
| `ScriptManager#irb_requested?` | main_task.rb がモード判定に使う |
| `ScriptManager#irb_begin` / `#irb_end` | コンソールの受け渡し（後述） |

`get_autorun_script` は `":irb"` のとき **nil を返す**。Script モードが
`File.exist?(":irb")` を試さないようにするため。

### コンソール層（[console_input.c](../components/picoruby-esp32/console_input.c)）

コンソールタスクは 3 つのモードを持つ：

| モード | 受信バイトの行き先 |
|-------|------------------|
| `CONSOLE_MODE_LINE` | エコー + 行編集 + コマンド解釈（通常） |
| `CONSOLE_MODE_MODEM` | `picorb_hal_stdin_push()`（PicoModem。[docs/PICOMODEM.md](PICOMODEM.md)） |
| `CONSOLE_MODE_IRB` | `picorb_hal_stdin_push()`（irb の Editor が読む） |

irb 中に C 側の行編集を止めるのは、`Editor::Line` が自前でエコーと再描画を行う
ため。両方が動くと文字が二重に出て矢印キーも壊れる。

- **CDC モード**: コンソールタスクが stream buffer から取り出したバイトを
  そのまま stdin リングバッファへ流す（CDC FIFO は gem の RX コールバックが
  吸い出すので、この経路以外に入力は届かない）
- **host / serial モード**: コンソールタスクは **stdin を読むのをやめる**。
  picoruby-machine の `stdin_reader` タスクが同じリンクをポーリングしていて、
  2 つが読むとキーストロークが分配されてしまうため
- 端末モードは **cooked のまま**にする。cooked では `picorb_hal_stdin_push()` が
  0x03 / 0x1A をシグナルに変換し、それが Ctrl-C / Ctrl-Z の実装になっている
  （`IO#read_nonblock` は読み出しの間だけ自分で raw に切り替える）
- ログ抑止はしない。PicoModem と違ってバイナリではないので、MIDI ドライバの
  ESP_LOG が混ざっても読める（むしろ irb で機器を触るときは見たい）

コンソールを元に戻すのは Ruby 側の `ensure`（`sm.irb_end`）だが、
`start_picoruby_task()` も irb 以外のタスクを起動する前に
`console_input_irb_exit()` を呼ぶ。クラッシュや強制停止でコンソールが
無反応のまま残らないようにするため。

### Ruby 層（[main_task_base.rb](../components/picoruby-esp32/mrblib/main_task_base.rb)）

`run_irb` が Script モードと同じ最小限の初期化（machine / watchdog / shell、
SD の再マウント）を済ませてから `Shell.new(clean: true).start(:irb)` を呼ぶ。

`Shell.new(clean: true)` の `clean:` は端末判定（`IO.wait_terminal`）を伴う。
これを省くと `ENV['TERM']` が未設定のままになり、`Editor::Line#refresh` が
毎キーストロークでカーソル位置問い合わせのタイムアウト 500ms を待つ。

`Machine.signal_self_manage` は upstream の `irb.rb` に倣って呼ぶ。最初の
`Sandbox#wait`（`Shell#run_irb` のウォームアップ実行）だけがこのフラグを消費し、
以降の wait はシグナルを見る。これが「実行中の式を Ctrl-C で中断できる」動作。

## 制限

- **停止は協調的でない**: irb の入力ループは `STDIN.read_nonblock` にいるので
  `ScriptManager#stop_requested?` を見ない。UI の Stop や別スクリプト選択では
  `stop_picoruby_task()` の 5 秒タイムアウト後に強制削除される（その後 VM は
  `cleanup_vm()` で作り直されるので状態は残らない）
- **irb 中は UI が更新されない**: `UI.process` を回すループがないため、
  M5Stack のパッドイベントは処理されない（画面描画と Stop ボタンは C 側なので動く）
- **irb 中は PicoModem が使えない**: どちらもコンソールを占有する。ファイル転送は
  UI モード（`> ` プロンプト）で行う
- **セッションをまたいだ状態は残らない**: 終了すると VM ごと破棄される
