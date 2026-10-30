# Tempo（UI::Tempo）

> **状態: 実装済み（2026-10-22、クロックを `MIDI::Clock` に一本化 2026-10-23）**。Tab5 のビルドとホストの Ruby テストのみ。**実機では未確認**。

テンポ（BPM）の画面と、その**テンポで MIDI クロックを送る MIDI デバイス**。

- 画面: [main/ui/screen_main.cpp](../main/ui/screen_main.cpp)（クラス名・`UI_SCREEN_MAIN`・`:main` は互換のため旧名のまま）
- クロック: picoruby-midi の `MIDI::Clock`。UI とのつなぎは [main/ui/ui_tempo.cpp](../main/ui/ui_tempo.cpp)
- Ruby: [mrbgems/picoruby-ui/mrblib/ui_tempo.rb](../mrbgems/picoruby-ui/mrblib/ui_tempo.rb)
- サンプル: [examples/plugins/clock.rb](../examples/plugins/clock.rb)（`/sd/plugins` 用）/ [examples/tempo_keys.rb](../examples/tempo_keys.rb)

## 画面

```
   120 BPM            ← タップで TAP tempo（ナビバーの TAP も同じ）、スワイプで ±1 BPM ずつ。Start 中は緑、Sync 中は水色
 Ext: 120.0 BPM       ← 選んだ送信元の外部クロック
[USB-H][USB-D][DIN][BLE]   ← 外部クロックの送信元（ボードの USB モードで使えるものだけ）
[ ▶ Play ][ Sync ]    ← Play / Stop: MIDI Start（FA）/ Stop（FC）を送る。Sync: 外部クロックに追従
```

- **Play / Stop ボタン**は押すたびに MIDI Start と Stop を交互に送る（`UI::Tempo.start` / `.stop` と同じ）。
  停止中は「▶ Play」（紺）、再生中は「■ Stop」（緑）。送り先は Tempo からのルート（クロックと同じ）

- **BPM の表示をスワイプするとテンポが 1 BPM ずつ変わる**（2026-10-24）。上 / 右で速く、下 / 左で遅く。
  最初に動いた向き（縦か横）の移動量だけを見て、`SWIPE_STEP_PX`（Tab5 / CrowPanel 12px、CoreS3 6px）ごとに 1 BPM。
  押した時点の BPM（整数に丸める）から数えるので、行き過ぎたら戻せばよい。スワイプ中は BPM が黄色になる。
  押した瞬間は TAP として数え（タイミングを押下で取るため）、`SWIPE_START_PX`（16px / 8px）以上動いたら
  スワイプとみなしてその TAP を取り消す（タップの履歴を捨て、TAP で変わったテンポも押す前に戻す）。
  Sync 中は動かない。レイアウトの `tempo` のセルでも同じ
- 以前あった **-10 / -1 / +1 / +10 ボタンはなくした**。BPM はスワイプのほか `UI::Tempo.bpm` で設定でき、
  ノブやパッドから変えたいときはスクリプトでつなぐ（下の例）
- **Bar / Beat カウンタと拍のバーは削除した**（使っていなかった。`UIManager::setBarBeat` などの API ごと）
- 描画は与えられた矩形（`setArea`）の中に収まるよう、各部の大きさを矩形から決める。
  全画面ではコンテンツ領域、レイアウトでは部品のセル

## MIDI クロックを送るデバイス

Tempo のクロックは **picoruby-midi の `MIDI::Clock`**（プロセスに 1 つのマスタークロック、2026-10-23）。
Tempo はそれを画面・TAP・Sync・Play / Stop で操作する UI で、自前のタイマは持たない。
`UI::Tempo` と `MIDI::Clock` は同じクロックを指す（`UI::Tempo.transport_id == MIDI::Clock.transport_id`）。

```ruby
MIDI.route(UI::Tempo, MIDIDevices.usb_midi_device)          # PC（DAW）へ。MIDI.route(MIDI::Clock, ...) と同じ
MIDI.route(UI::Tempo, MIDIDevices.sam2695, channel: :clock) # クロックだけ（Tempo はもともとクロックしか出さない）
UI::Tempo.start   # MIDI Start（FA）  / UI::Tempo.stop で Stop（FC）。MIDI::Clock.start / .stop と同じ
```

- クロックは gem の port（[ports/esp32/midi.c](../mrbgems/picoruby-midi/ports/esp32/midi.c)）が持つ送信専用の
  トランスポートで、登録表の最初の動的ビット（0x08）を起動時に取る。UIManager の初期化時（`ui_tempo_init`）に
  タイマを起こし、MIDI Devices に内蔵デバイス `tempo`（表示名 "Tempo"）として載せる。**IN の行にだけ出る**
  （`UI_MIDIDEV_NO_OUT`。送られても何もしないので OUT の列は出さない）。画面で張ったパッチは他の内蔵デバイスと
  同じく NVS に保存される（キーは名前なので、自前のトランスポートだった頃のパッチもそのまま使える）
- クロック（F8、4 分音符あたり 24 回）は esp_timer（`ESP_TIMER_TASK`）が送る。タイマは常に回っていて、
  **クロックからのルート・filter・watch がある間だけ** F8 を `MIDI_route_dispatch()` で送る（filter / listener も効く）。
  Start / Stop は別のメッセージで、F8 は止めない
- テンポは UI の内部 BPM がそのまま `MIDI::Clock` のテンポ（`UIManager::setBpm` → `MIDI_Clock_set_bpm`）。
  スクリプトが `MIDI::Clock.bpm =` や `MIDI.bpm_loop(120, ...)` で変えたテンポは、UI タスクが毎周回
  （`ui_tempo_tick`）拾って UI の BPM にする（`:bpm_change` も出る）。**Sync 中は外部クロックの BPM に従う**
  （`MIDI_Clock_set_follow_bpm`。0.05 BPM 未満の揺れでは周期を変えない）。外部クロックを受けて
  そのテンポで別のデバイスへクロックを出し直すこともできる
- Start / Stop は明示的に送る（`UI::Tempo.start` / `.stop`）。Start の直後に周期を取り直すので、
  Start の次の F8 が 1 拍目になる。スクリプト終了時のクリーンアップは全デバイスに Stop を送り、
  クロックも停止状態に戻す（`MIDI_Clock_reset()`）
- `MIDI.bpm_loop(output: dev)` / `MIDI.start!(output: dev)` は自分で F8 を送らず、ループの間だけ
  クロックを `dev` へルートする（すでにパッチがあればそのまま）。Tempo と二重に送ることはない
- Freenove（画面なし）では `UI::Tempo` のデバイスは無い（`transport_id` が 0）が、`MIDI::Clock` は使える

## Ruby API

```ruby
UI::Tempo.bpm             # => 120.0（Sync 中は外部クロックのテンポ）
UI::Tempo.bpm = 128       # 内部テンポ（20〜300）。Sync 中はクロックに影響しない
UI::Tempo.tap             # TAP tempo を 1 回
UI::Tempo.start / .stop / .running?
UI::Tempo.sync? / UI::Tempo.sync = true   # 外部クロックがあるときだけ ON になる
UI::Tempo.on_change { |bpm| ... }         # TAP・Sync・スクリプトで変わったとき（:bpm_change）
UI::Tempo.show            # = UI.show(:tempo)
```

ノブやパッドでテンポを変える:

```ruby
UI.knob(1, label: "BPM", min: 40, max: 240, step: 1, value: UI::Tempo.bpm) do |v|
  UI::Tempo.bpm = v
end
UI::Tempo.on_change { |bpm| UI.knob_set(1, bpm, notify: false) }   # TAP / Sync でもノブが追従する
UI.pad(1, label: "-1") { UI::Tempo.bpm -= 1 }
UI.pad(2, label: "+1") { UI::Tempo.bpm += 1 }
UI.pad(3, label: "TAP") { UI::Tempo.tap }
UI.pad(4, label: "Play", type: :toggle) { |on| on ? UI::Tempo.start : UI::Tempo.stop }
```

`UI.bpm` / `UI.set_bpm` は互換のため残している（`UI::Tempo.bpm` / `bpm=` と同じ）。

## レイアウト DSL

`tempo` で Tempo 画面をセルに置ける（1 レイアウトに 1 つ。他の画面のビューと同じく、表示中だけ
画面オブジェクトをセルに狭めて借りる）。戻り値は `UI::Tempo`。最小サイズの目安は 120x72px。

```ruby
UI.layout("Clock", cols: 3, rows: 2) do
  tempo col: 0..1, row: 0..1
  knob col: 2, row: 0, label: "BPM", min: 40, max: 240, step: 1, value: UI::Tempo.bpm do |v|
    UI::Tempo.bpm = v
  end
  pad col: 2, row: 1, label: "Play", type: :toggle do |on| on ? UI::Tempo.start : UI::Tempo.stop end
end.show
```
