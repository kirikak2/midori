# Timer（UI::Timer）

> **状態: 実装済み（2026-11-17）**。Tab5 のビルド、Web シミュレーター（Tab5 / CoreS3）での動作、ホストの Ruby テストまで。**実機では未確認**。

時間をセットしておき、**MIDI の Play（Start）でカウントダウンを始め、0 秒になったら MIDI の Stop を送る**部品。
1 秒減るたびに任意の Ruby ブロックを呼べる。曲の尺を決めたライブ、制限時間付きのジャム、リハーサルのタイマーなどに使う。

- 画面: [main/ui/screen_timer.cpp](../main/ui/screen_timer.cpp)（Home のタイル `Timer`、`UI_SCREEN_TIMER` = 14、Ruby は `:timer`）
- カウントダウン: [main/ui/ui_timer.cpp](../main/ui/ui_timer.cpp)
- Ruby: [mrbgems/picoruby-ui/mrblib/ui_timer.rb](../mrbgems/picoruby-ui/mrblib/ui_timer.rb)
- レイアウト DSL の部品 `timer`（Tempo と同じく画面をセルに借りる。1 レイアウトに 1 つ）
- テスト: `ruby test/timer_test.rb`（picoruby-ui ディレクトリで）
- サンプル: [examples/timer.rb](../examples/timer.rb)
- 対応ボード: CoreS3 / Tab5 / CrowPanel（UI のあるボード）。Freenove はスタブで、`UI::Timer` は既定値を返すだけで数えない

## 1. Play と Stop

「Play」「Stop」は **`MIDI::Clock`（= `UI::Tempo`）の Start / Stop**。プロセスに 1 つのマスタークロックで、
Tempo 画面の Play / Stop、`UI::Tempo.start` / `.stop`、`MIDI::Clock.start` / `.stop` はどれも同じもの（docs/TEMPO.md）。

| できごと | Timer |
|---|---|
| クロックが止まっている → 動き出す（Start） | **arm されていれば**、セットした時間からカウントダウンを始める |
| 1 秒経つ | 残り時間を 1 減らし、ブロックを呼ぶ |
| 0 秒になる | **クロックを止める**（`MIDI_Clock_stop()`）。Tempo からのルートすべてに MIDI Stop（FC）が出る |
| 0 秒より前にクロックが止まる（Stop） | その残り時間で止まる（Stopped） |
| もう一度 Start | 毎回セットした時間から数え直す（MIDI の Start は「頭から」なので。続きからは数えない） |

- Stop の送り先は **Tempo からのルート**（MIDI Devices 画面の Tempo の行、`MIDI.route(UI::Tempo, dev)`）。
  Start と同じ所へ Stop が届く
- 外部機器から受けた MIDI Start（USB / DIN / BLE のクロックの FA）では始まらない。`MIDI::Clock` の Start だけを見る
  （外部のクロックに Sync していても、外部の Start は `MIDI::Clock` を動かさない）
- Start の検出は UI タスクの周回（`ui_timer_tick`）なので、Start から最大 1 周回（十数 ms）遅れて数え始める。
  そこから先の秒は esp_timer が開始時刻からの絶対時刻で刻むので、UI の描画や Ruby の処理では遅れも溜まりもしない

### 1.1 arm

**arm していない Timer は Start を無視する**。1 分にセットしたまま忘れたタイマーが、Tempo の Play で始めた演奏を
1 分後に勝手に止めないため。

| arm する方法 | 外れるとき |
|---|---|
| Timer 画面の Arm ボタン | もう一度押す（スクリプトが終わっても残る） |
| Timer 画面の Play（arm されていなければ arm してから Start） | 同上 |
| `UI::Timer.armed = true` / `arm`、`on_tick` / `on_start` / `on_finish` / `on_stop` にブロックを渡す | **スクリプトが終わったとき**。`UI::Timer.armed = false` / `disarm` は画面の Arm も含めて外す |

## 2. 画面

```
          01:30                   ← 残り時間。白: 待機 / 緑: カウント中 / 橙: 残り 10 秒 / 赤: 0 / 水色: Stop で停止 / 灰: arm していない
   Set 01:30  Armed: Play starts  ← セットした時間と状態（Counting / Stopped / Time up / Not armed）
 [-1m][-10s][-1s][+1s][+10s][+1m] ← セットする時間（カウント中は押せない）
 [ ▶ Play ][ Reset ][ Armed ]     ← Play / Stop は Tempo と同じ（MIDI Start / Stop）。Reset は時間を戻す
```

- 時間は 1 秒〜99:59（5999 秒）。既定 60 秒。セットした時間は電源を切るまで残る（スクリプトをまたいで残る。NVS には保存しない）
- Reset（ナビバー中央も Reset）: 残り時間をセットした時間に戻す。**カウント中なら頭から数え直す**（クロックは止めない）
- Home のタイルの状態表示は残り時間（`01:30 >` はカウント中、`off` は arm していない）
- 描画は与えられた矩形（`setArea`）に収まるよう各部の大きさを決める。全画面ではコンテンツ領域、レイアウトでは部品のセル

## 3. Ruby API

```ruby
UI::Timer.seconds = 90                 # セットする時間（1〜5999）。カウント中なら次の回から
UI::Timer.set(minutes: 1, seconds: 30) # 同じ
UI::Timer.seconds                      # => 90
UI::Timer.remaining                    # => 残り秒数（待機中はセットした時間）
UI::Timer.state                        # => :idle / :running / :stopped / :finished
UI::Timer.running? / finished?
UI::Timer.reset                        # セットした時間に戻す（カウント中は数え直し）
UI::Timer.armed? / armed = true / arm / disarm
UI::Timer.start / stop                 # = UI::Tempo.start / .stop（MIDI Start / Stop）
UI::Timer.show                         # = UI.show(:timer) / UI.timer

UI::Timer.on_tick   { |sec| ... }      # Start でセットした時間、以後 1 秒ごとに残り秒数（最後は 0）
UI::Timer.on_start  { |sec| ... }      # Start（セットした時間）
UI::Timer.on_finish { ... }            # 0 秒。Stop はもう送ってある（on_tick(0) の後）
UI::Timer.on_stop   { |sec| ... }      # 0 秒より前に Stop で止まった（残り秒数）
```

- ブロックは **1 つずつ**。もう一度呼ぶと置き換わり、ブロックなしで呼ぶと外れる（レイアウトの再定義で増えないように）。
  `UI.on(:timer) { |e| e[:kind]; e[:remaining] }` でも受けられる（kind は `:start` / `:tick` / `:finish` / `:stop`）
- ブロックは `UI.process` から呼ばれる（UI モードのループ、`MIDI.sleep_ms`、クロックのループ）。カウントダウンそのものと
  0 秒の Stop は C なので、スクリプトが忙しくて `UI.process` が遅れてもずれない。遅れた分のイベントはキューに溜まり、
  1 秒ずつ漏れなく届く
- ブロックの中の例外はログに出して続ける

```ruby
# レイアウトに置く。seconds: で時間、ブロックは on_tick（arm もする）
UI.layout("Show", cols: 3, rows: 2) do
  timer col: 0..1, row: 0..1, seconds: 300 do |sec|
    dev.trigger(84, 100, duration: 50) if sec <= 10 && sec > 0
  end
  tempo col: 2, row: 0
end.show
```

## 4. 実装

```
 [UI task]                             [esp_timer task]                [PicoRuby task]
  ui_timer_tick()                       on_second()
   ├ MIDI_Clock_is_running の変化を見る   ├ 開始時刻からの経過で残りを計算     UI.process
   ├ Start & armed → start_countdown()   ├ 減った秒ごとに UI_EVENT_TIMER ──→  └ UI::Timer._dispatch
   │   └ UI_EVENT_TIMER(start)           ├ 0 → FINISHED, MIDI_Clock_stop()        └ on_tick など
   └ Stop & RUNNING → STOPPED            └ 次の秒の境目に one-shot を張り直す
       └ UI_EVENT_TIMER(stop)
  ScreenTimer::update()
   └ ui_timer_serial() が変わったら描き直す
```

- 状態（セットした時間・残り・状態・開始時刻・arm）は UI タスク / esp_timer タスク / PicoRuby タスクから触るので
  `portMUX` のクリティカルセクションで守る。イベントの投入と `MIDI_Clock_stop()`・esp_timer の操作はその外で行う
- 秒は esp_timer の one-shot を「開始時刻 + (n+1) 秒」に毎回張り直して刻む（周期タイマのずれが溜まらない）。
  コールバックが遅れて 2 秒分進んでいたら、その間の tick イベントもすべて出す
- 0 秒では先に状態を FINISHED にしてからクロックを止めるので、次の `ui_timer_tick` はそれを外からの Stop と
  取り違えない
- スクリプト終了時（`UIManager::scriptCleanup` → `ui_timer_cleanup`）: カウントダウンを止めて待機に戻し、
  スクリプトが付けた arm を外す。セットした時間と画面の Arm は残す
- イベントは `UI_EVENT_TIMER`（`data.timer.kind` / `remaining`）→ picoruby-ui の `PICORUBY_UI_EVENT_TIMER`（14）→
  Ruby の `{ type: :timer, kind:, remaining: }`
- レイアウトの部品は `UI_WIDGET_TIMER`（= 8）。Tempo と同じく `ScreenTimer` を `setArea()` で部品のセルに狭めて借りる

### 4.1 C API（ui_common.h）

```c
void ui_timer_tick(void);                  // UI タスク、毎周回
void ui_timer_set_seconds(int seconds);    // 1..UI_TIMER_MAX_SECONDS
int  ui_timer_seconds(void);
int  ui_timer_remaining(void);
int  ui_timer_state(void);                 // ui_timer_state_t
void ui_timer_reset(void);
void ui_timer_set_armed(bool on, bool by_script);
bool ui_timer_armed(void);
uint32_t ui_timer_serial(void);
void ui_timer_cleanup(void);
```

Ruby からは `UI._timer_seconds` / `_timer_set_seconds` / `_timer_remaining` / `_timer_state` / `_timer_reset` /
`_timer_set_armed`（スクリプトの arm）/ `_timer_armed`。mruby/c のバインディングは無い（MIDoRI は mruby のみ）。

## 5. 未対応・今後

- 外部機器からの MIDI Start / Stop で始める・止める（Timer を MIDI Devices の宛先にする。登録表のビットが要る）
- 続きから数える（MIDI Continue）、カウントアップ、複数のタイマー
- 拍・小節で数える（BPM に合わせた尺）
- 時間を画面でスワイプして決める（いまは ±1m / ±10s / ±1s のボタンと `seconds=`）
