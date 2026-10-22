# Tempo（UI::Tempo）

> **状態: 実装済み（2026-10-22）**。Tab5 のビルドとホストの Ruby テストのみ。**実機では未確認**。

テンポ（BPM）の画面と、その**テンポで MIDI クロックを送る MIDI デバイス**。

- 画面: [main/ui/screen_main.cpp](../main/ui/screen_main.cpp)（クラス名・`UI_SCREEN_MAIN`・`:main` は互換のため旧名のまま）
- クロックを送るデバイス: [main/ui/ui_tempo.cpp](../main/ui/ui_tempo.cpp)
- Ruby: [mrbgems/picoruby-ui/mrblib/ui_tempo.rb](../mrbgems/picoruby-ui/mrblib/ui_tempo.rb)
- サンプル: [examples/tempo.rb](../examples/tempo.rb)

## 画面

```
   120 BPM            ← タップで TAP tempo（ナビバーの TAP も同じ）。Start 中は緑、Sync 中は水色
 Ext: 120.0 BPM       ← 選んだ送信元の外部クロック
[USB-H][USB-D][DIN][BLE]   ← 外部クロックの送信元（ボードの USB モードで使えるものだけ）
[ ▶ Play ][ Sync ]    ← Play / Stop: MIDI Start（FA）/ Stop（FC）を送る。Sync: 外部クロックに追従
```

- **Play / Stop ボタン**は押すたびに MIDI Start と Stop を交互に送る（`UI::Tempo.start` / `.stop` と同じ）。
  停止中は「▶ Play」（紺）、再生中は「■ Stop」（緑）。送り先は Tempo からのルート（クロックと同じ）

- 以前あった **-10 / -1 / +1 / +10 ボタンはなくした**。BPM は `UI::Tempo.bpm` で設定し、
  ノブやパッドから変えたいときはスクリプトでつなぐ（下の例）
- **Bar / Beat カウンタと拍のバーは削除した**（使っていなかった。`UIManager::setBarBeat` などの API ごと）
- 描画は与えられた矩形（`setArea`）の中に収まるよう、各部の大きさを矩形から決める。
  全画面ではコンテンツ領域、レイアウトでは部品のセル

## MIDI クロックを送るデバイス

Tempo は picoruby-midi のトランスポート（`UI::Tempo.transport_id`）で、**送信だけ**のデバイス。
`MIDI.route` や MIDI Devices 画面で任意のデバイスへルーティングすると、そこへクロックが届く。

```ruby
MIDI.route(UI::Tempo, MIDIDevices.usb_midi_device)          # PC（DAW）へ
MIDI.route(UI::Tempo, MIDIDevices.sam2695, channel: :clock) # クロックだけ（Tempo はもともとクロックしか出さない）
UI::Tempo.start   # MIDI Start（FA）  / UI::Tempo.stop で Stop（FC）
```

- UIManager の初期化時に登録される（`ui_tempo_init`）。MIDI Devices には内蔵デバイス
  `tempo`（表示名 "Tempo"）として載り、**IN の行にだけ出る**（`UI_MIDIDEV_NO_OUT`。送られても何もしないので OUT の列は出さない）。
  画面で張ったパッチは他の内蔵デバイスと同じく NVS に保存される
- クロック（F8、4 分音符あたり 24 回）は esp_timer（`ESP_TIMER_TASK`）が送る。
  **Tempo からのルートがある間だけ**タイマを動かす（`ui_tempo_tick`、UI タスクの毎周回）。
  送るのは `MIDI_route_dispatch()` 経由なので、Tempo に付けた filter / listener も効く
- テンポは `ui_get_bpm()`、つまり内部 BPM、**Sync 中は外部クロックの BPM**。外部クロックを受けて
  そのテンポで別のデバイスへクロックを出し直すこともできる。0.05 BPM 未満の変化では周期を変えない
  （Sync 中に測った BPM が細かく揺れるため）
- Start / Stop は明示的に送る（`UI::Tempo.start` / `.stop`）。Start の直後に周期を取り直すので、
  Start の次の F8 が 1 拍目になる。スクリプト終了時のクリーンアップは全デバイスに Stop を送り、
  Tempo も停止状態に戻る
- Freenove（画面なし）では Tempo のデバイスは無い（`transport_id` が 0。`MIDI.route` は登録されていない
  トランスポートとして拒否する）

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
