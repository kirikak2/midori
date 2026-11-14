# attach — 実行中の VM に Ruby を送る

## 概要

シリアルコンソール（`midi_device` モードでは USB CDC）から `attach` と打つと、**いま動いている Ruby VM**
（SD カードのスクリプト、または UI モード）の中で式を評価するセッションに入る。irb がまっさらな VM を
新しく立てる（実行中のスクリプトは止まる）のに対し、attach はスクリプトを動かしたまま、その変数や
オブジェクトを見たり書き換えたりできる。

2026-11-14 に実装し、実機で attach を確認。同じ日に Web シミュレーターの Attach 欄（§ Web シミュレーター）と、
そのための `eval64` / `evalstop`（proto 5）を足した。`eval64` / `evalstop` はビルドのみで**実機未確認**。

```
> load /sd/plugins_demo.rb
...
> attach
Attached to /sd/plugins_demo.rb
Lines run when the script calls UI.process / MIDI.sleep_ms.
exit / Ctrl-D to detach, Ctrl-C to cancel
attach> MIDI::Clock.bpm
=> 120
attach> MIDI::Clock.bpm = 96
=> 96
attach> def hit
attach*   $dev.trigger(36)
attach* end
=> :hit
attach> hit
=> nil
attach> exit
Detached
>
```

| 操作 | 動作 |
|-----|------|
| `exit` / `quit`（式の 1 行目）/ 空行で Ctrl-D | セッションを抜ける。スクリプトは動き続ける |
| Ctrl-C | 評価中の行を止める（Sandbox を `stop`）。待っている行と、入力途中の複数行の式を捨てる |
| 行末の `\` / コンパイルできない行 | 続きを待つ（プロンプトが `attach* `）。irb と同じく、構文エラーも「途中」と見なすので Ctrl-C で捨てる |

## 仕組み

```
シリアル ─▶ console task（C、優先度 4）
              行編集・エコー・Ctrl-C / Ctrl-D・exit
              │ 1 行 = "L<行>"
              ▼
          attach キュー（4 件 × 257 byte、ヒープ）
              │
              ▼  MIDI.run_filters のフック（$__midi_filter_hooks__）
          MIDoRIAttach.poll（スクリプトのタスク上）
              Sandbox("attach") で compile → execute → 終わるまで待つ
              puts "=> ..." → ScriptManager#attach_reply(more) → C がプロンプトを出す
```

- **VM はスレッドセーフではない**ので、別タスクからは評価しない。スクリプト自身が `MIDI.run_filters` を呼んだときに、
  そのタスクの上で 1 件ずつ評価する。`run_filters` は `UI.process`、`MIDI.sleep_ms`（50 ms ごと）、`MIDI.start!` /
  `MIDI.bpm_loop` のクロックループが呼ぶ。UI モードのループも `UI.process` + `MIDI.sleep_ms` なので同じように届く
- 評価は irb と同じく **同じ VM の Sandbox**（`picoruby-sandbox`）。`begin; _ = (行); rescue Exception => _; end; _` で包むので、
  例外はスクリプトに伝わらない。Sandbox はセッションをまたいで使い回すので、attach で作ったローカル変数は次の行・次の
  セッションでも見える（VM と一緒に消える）
- **行編集は C 側**。irb は Ruby の `Editor::Line` が入力を読むが、attach では VM がスクリプトのループを回しているので
  入力待ちで止められない。そのため履歴とカーソル移動は無い（エスケープシーケンスは捨てる）
- **プロンプトは Ruby が評価を終えてから C に出させる**（`attach_reply(more)`）。式が閉じているかを知っているのは Ruby だけなので。
  C が自分で出すのは、セッションに入ったときと、何も評価していないときの Ctrl-C だけ
- 行を積んでから 3 秒たっても取り出されなければ、C が `(waiting: the script has not called UI.process / MIDI.sleep_ms yet; ...)`
  を 1 回出す
- 評価中に `stop`（UI の Stop も）が来たら、Sandbox を止めてスクリプトの協調停止を妨げない
- Supervisor が次のタスク（スクリプトの終了後の UI モード、別のスクリプト）を始めるときに `console_input_attach_exit()` で
  セッションを閉じる（`[attach] the script ended: detached`）。VM が無くなるので

## 制約

- **協調ポイントを通らないスクリプトには届かない**: 素の `sleep_ms` だけで回るループ、長い計算の途中など。
  `ScriptManager#stop_requested?` と同じ種類の制約
- **スクリプトのファイルのローカル変数は見えない**。見えるのはグローバル変数・定数・クラスと、そこから辿れるもの
  （`MIDIDevices.*`、`UI::Sequencer.current`、`MIDI::Clock` など）。attach で触りたいものは `$dev = ...` のように
  グローバルに出しておく
- **評価の間はスクリプトが止まる**。`MIDI.start!` のループの中で重い式を評価すると、その間クロックの tick が遅れる
- スクリプトの `puts` やログは、attach 中も同じ端末に混ざって出る
- irb の実行中と、スクリプトが止まる途中（`stopping`）には attach できない
- 1 行は 255 文字まで（C の行編集のバッファ）

## 機械向け: `eval` / `eval64` / `evalstop`

Web シミュレーターの Attach 欄のように、プログラムからコードを送るためのコマンド。通常のコマンド行で使い、
attach セッションには入らない。仕組みは attach と同じ（同じキューと Sandbox）で、答えに枠を付ける。

```
> eval 7 MIDI::Clock.bpm
#<eval 7
#>eval 7 ok 120
> eval 8 nosuch
#<eval 8
#>eval 8 err NameError: undefined local variable or method 'nosuch' for main
> eval 9 (1
#>eval 9 err SyntaxError: incomplete or invalid code
```

| コマンド | 答え | proto |
|---------|------|-------|
| `eval <id> <code>` | `#<eval <id>`、式が出力した行、`#>eval <id> ok <inspect>` か `#>eval <id> err <理由>` | 4 |
| `eval64 <id> + <base64>` | `#>eval64 <id> more`（続きを待つ） | 5 |
| `eval64 <id> . <base64>` | 最後の 1 片。ここまでをつないで Base64 を解き、`eval` と同じ枠で答える | 5 |
| `evalstop` | 待っている eval を捨て、実行中の eval を止める（それは `#>eval <id> err stopped` と答える）。`#>evalstop ok` | 5 |

- `eval` のコードは 1 行の印字可能な ASCII だけ（C の行編集が 0x20〜0x7E しか受けず、1 行 255 文字まで）。
  複数行・日本語・長いコードは `eval64` で UTF-8 を Base64 にして分けて送る（ページは 1 片 192 文字）。
  片は C（コンソールタスク）がヒープにつなぎ（24 KB まで）、そろったら 1 件としてキューへ。Base64 を解くのは Ruby（`Base64.decode64`）
- `<id>` は送る側が決める空白なしのトークン（31 文字まで）。答えはこれを名乗るので、遅れて届いた答えを取り違えない
  （docs/WEB_SIMULATOR.md §13.2 の方針）。`eval64` は 1 つずつ（別の id が来たら途中のものは捨てる）
- `#<eval <id>` と `#>eval <id> ...` の間の行は、式が `puts` したもの。結果は `inspect` を 1 行にしたもの
- 答えは非同期（スクリプトが `run_filters` を呼んだとき）。VM が無い（irb、停止中）・キューが満杯・attach セッション中は C がすぐ
  `#>eval <id> err busy: ...` を返す
- コードは最後の行のあとに改行を足してから包む（最後の行のコメントが包みの `)` を飲み込まないように）

## Web シミュレーター

シミュレーターのページ（`sim/web/index.html`）のログの下に **Attach** 欄がある。対象は 2 つ:

- **シミュレーター**: wasm で動いている VM（Run したスクリプト、または UI モード）。ページの JS がキュー
  （`__midoriSim.takeAttach`）に積み、`sim_boot.rb` の `SimAttachSource` が `MIDoRIAttach` に渡す。
  `MIDoRIAttach` は実機と共有する部分（main_task_base.rb のマーカーより上）にあるので、評価も答えの枠も実機と同じ。
  Run のたびに新しい VM になるので、attach で定義したものも消える
- **実機（MIDoRI）**: 左の実機パネルで接続した MIDoRI の VM（UI モードかスクリプト）。`MidoriLink#evaluate` が `eval64` で送る。
  proto 5 より古いファームウェアでは選べない

Enter で実行、Shift+Enter で改行、↑↓ で履歴（ブラウザに残る）。スクリプトが 3 秒たっても受け取らなければその旨を出し、
「取り消す」で止められる（シミュレーターは `attachCancel`、実機は `evalstop`）。式が出力した行はログではなく Attach 欄に出る。

試験: `node sim/test/node_attach.mjs`（スクリプトモードと UI モード。グローバル変数・複数行・ローカル変数の持ち越し・UTF-8 の出力・
例外・構文エラー・取り消し）

## 実装

| 場所 | 内容 |
|------|------|
| [components/midori-runtime/console_input.c](../components/midori-runtime/console_input.c) | `CONSOLE_MODE_ATTACH`、attach キュー、`attach` / `eval` / `eval64` / `evalstop`、Ctrl-C / Ctrl-D / exit、待ちの通知 |
| [components/midori-runtime/picoruby_supervisor.c](../components/midori-runtime/picoruby_supervisor.c) | `ScriptManager#attach_pop` / `attach_reply` / `attach_cancel?` / `attached?`、タスク開始時の `console_input_attach_exit()` |
| [components/midori-runtime/mrblib/main_task_base.rb](../components/midori-runtime/mrblib/main_task_base.rb) | `MIDoRIAttach`（シミュレーターと共有する、マーカーより上）。スクリプトモードと UI モードで `install(ScriptManager.new)` |
| [sim/mrblib/sim_boot.rb](../sim/mrblib/sim_boot.rb) | `SimAttachSource`（ページのキューから受け取る）と `install` |
| [sim/web/attach_console.js](../sim/web/attach_console.js) | Attach 欄（対象の選択・入力・履歴・取り消し） |
| [sim/web/device.js](../sim/web/device.js) / [sim/web/midori_link.js](../sim/web/midori_link.js) | `evaluate` / `cancelEval`（シミュレーター / 実機） |

キューの 1 件はヒープの文字列で、種類の 1 バイト + 本文: `L` = attach の 1 行、`E` = `eval` の `<id> <code>`、
`B` = `eval64` の `<id> <base64>`。取り出した側が free する。Ctrl-C とセッションの
出入りはキューではなくフラグ（`attach_cancel?`）で伝える（待っている行はその場でキューごと捨てる）。

## 今後

- デザイナー（`sim/web/designer.html`）にも Attach 欄（部品は `attach_console.js` をそのまま使える）
