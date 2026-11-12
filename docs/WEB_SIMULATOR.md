# Web シミュレーター / デザイナー 設計（2026-11-12 案）

GitHub Pages（`site/`）に、MIDoRI の画面をブラウザで動かす **シミュレーター** と、画面・レイアウトを
組み立てる **デザイナー** を追加する。Ruby は picoruby.wasm で実行する。

本書は設計案。決定事項は §11.1。§9 の段階 0（技術検証）は済み、§2 の方針（A）で進める（結果は §12）。
実装は [sim/](../sim/)（[sim/README.md](../sim/README.md)）。

## 1. 要求

| # | 要求 | 本書での対応 |
|---|------|-------------|
| R1 | 1 つの Ruby スクリプトで MIDoRI の画面を表現し、実機と同じ画面・操作ができる。MIDI は Web MIDI | §3〜§5 |
| R2 | Knobs / Pads / Keys / XYPad などの画面を設定でき、設定値は裏の Ruby スクリプトにすぐ反映され、そのまま操作できる | §6 |
| R3 | レイアウト DSL でレイアウトと部品を組み合わせて独自の画面を作れる | §7 |

共通の前提:

- 作ったスクリプトは**そのまま実機の `/sd` で動く**こと（シミュレーター専用の API を使わせない）
- GitHub Pages は静的ホスティングで、COOP / COEP ヘッダを付けられない → `SharedArrayBuffer`（wasm のスレッド）は使わない
- 対応ブラウザは Web MIDI のある Chromium 系と Firefox。Safari は Web MIDI が無いので内蔵音源だけで動く（§5.4）

## 2. 基本方針: 実機の UI コードをそのまま wasm にする

### 2.1 2 つの選択肢

| | A. main/ui の C++ を wasm にビルド（**推奨**） | B. 画面を JS / Canvas で作り直す |
|---|---|---|
| 画面の一致 | 同じコード・同じフォント・同じ座標計算なので画素単位で一致 | 見た目を真似るだけ。画面ごとに乖離していく |
| 保守 | 実機の変更がそのまま反映される | 画面を足す・直すたびに 2 か所 |
| 規模 | 約 2.5 万行（main/ui）を移植層の上でビルドする | 画面 14 種 + Sequencer / Tombola の物理・Keys の発音を再実装 |
| 主なリスク | LovyanGFX / M5Unified と ESP-IDF API の差し替え（§4.2） | 実機と同等にならない（R1 を満たせない） |

R1 の「実機と同等」を満たせるのは A だけなので A を採る。B は段階 0 で A が成立しなかったときの退路とする。

### 2.2 全体構成

```
┌──────────────────────────── ブラウザ（メインスレッド 1 本）────────────────────────────┐
│                                                                                          │
│  ページ（site/simulator, site/designer）                                                 │
│   ├ コードエディタ（CodeMirror 6）        ├ インスペクタ / レイアウトグリッド（§6, §7）   │
│   └ デバイス表示 <canvas>  ← Pointer Events（最大 5 指）                                  │
│                │ ▲                                                                        │
│      touch/frame│ │ framebuffer の dirty 矩形                                              │
│                ▼ │                                                                        │
│  ┌───────────── midori-sim.wasm（Emscripten） ─────────────┐     JS ホスト（sim/web）     │
│  │ picoruby-wasm（mruby VM・タスク・JS 連携）                │◄──► ・ホストループ          │
│  │ midori gems: picoruby-ui / -midi / -midi-mml / -amy? …   │      （rAF + setTimeout）   │
│  │ main/ui の C++（画面・UIManager・Sequencer・Keys …）      │     ・Web MIDI ブリッジ      │
│  │ 移植層 sim/shim: M5Unified / LovyanGFX パネル /          │     ・内蔵音源（WebAudio）   │
│  │   FreeRTOS / esp_timer / NVS / esp_log / sdkconfig       │     ・仮想 FS（/sd）・NVS    │
│  └──────────────────────────────────────────────────────────┘                              │
│                                                        ▲ ▼  Web MIDI（MIDIAccess）         │
└────────────────────────────────────────────────────────┼─┼──────────────────────────────┘
                                                          外部 MIDI 機器 / DAW
```

- **1 スレッド**。UI タスク・Sequencer タスク・MIDI 入力タスク・esp_timer はすべて JS のホストループから
  「1 回分」ずつ呼ぶ（§4.3）。Ruby は picoruby-wasm のタスクスケジューラで協調的に動き、`sleep_ms` は
  ブラウザに制御を返す（picoruby-wasm の設計どおり）
- **ボードの画面はビルド時に決まる**（`UI_LAYOUT_LARGE` とジオメトリ定数が `#if`）。wasm はボードごとに
  ビルドし、ページで切り替える。最初は **Tab5（1280x720）**、次に **CoreS3（320x240）**。CrowPanel は需要次第

## 3. シミュレーター（R1）

### 3.1 実行モデル

実機の Supervisor と同じく、**スクリプトを実行するたびにまっさらな VM** で動かす。

1. 「Run」で JS ホストが wasm モジュールを**新しくインスタンス化**する（コンパイル済み
   `WebAssembly.Module` を使い回すので、ダウンロード・コンパイルは初回だけ）。C の静的状態
   （UI のモデル・ルート表・スケジューラ）も含めて丸ごと初期化されるので、`picoruby_esp32_midi_cleanup()`
   相当の後始末漏れが起きない
2. ブート用 Ruby（`sim/mrblib/sim_boot.rb`）が `BoardConfig` と `MIDIDevices` を用意し（§5.2）、
   ユーザーのスクリプトを `load` する
3. スクリプトの `loop { UI.process; MIDI.sleep_ms 10 }` は `sleep_ms` のたびにブラウザへ戻る。
   `ScriptManager#stop_requested?` も実装し、「Stop」で協調停止 → 一定時間で強制停止（インスタンスを捨てる）
4. スクリプトが終わったら UI モード（Home）に戻す。実機のスクリプト終了後と同じ見え方にする

インスタンスの作り直しにかかる時間は段階 0 で測る。100 ms を大きく超えるなら、VM だけ
`mrb_close` / `mrb_open` し直して C 側は cleanup 関数で戻す方式に切り替える。

### 3.2 画面と操作

- **描画**: LovyanGFX の描画先を wasm ヒープ上の RGB565 フレームバッファにする（Tab5 の
  フレームバッファパネルと同じ形。§4.2）。`M5.Lcd.display()` で dirty 矩形を JS に通知し、
  JS が RGBA に変換して `putImageData`。表示サイズは CSS で拡縮（CoreS3 は `image-rendering: pixelated`）
- **タッチ**: Pointer Events を最大 5 本までタッチ点として M5.Touch の shim に渡す。スマホ・タブレットは
  そのままマルチタッチ。マウスでは **Alt+クリックで「置いたままの指」を追加**でき、XYPad の複数スロットや
  Keys の和音を試せる。タッチ座標は canvas の拡縮を戻して画面座標に変換
- 画面は main/ui をそのまま使うので **Home / Tempo / Pads / Knobs / XYPad / Keys / Tombola / Sequencer /
  MIDI Devices / Layout / Log / Settings** はすべて実機どおり動く。Bluetooth 画面は「シミュレーターでは使えません」
- ボードの外枠は簡単な図形で描く（メーカーのロゴや製品写真は使わない）

### 3.3 ページ構成（`site/simulator/`）

```
┌────────────────────────────────────────────────────────────────────┐
│ [Tab5 ▾] [▶ Run] [■ Stop] [Examples ▾] [Open .rb] [Save .rb] [Share]│
├──────────────────────────────┬─────────────────────────────────────┤
│  コードエディタ               │   デバイス表示（canvas）             │
│  （CodeMirror 6, Ruby）       │                                     │
│                              ├─────────────────────────────────────┤
│                              │ [Console] [MIDI Monitor] [Ports]     │
│                              │  puts / 例外 / MIDI の送受信ログ     │
└──────────────────────────────┴─────────────────────────────────────┘
```

- Examples は `examples/*.rb` をビルド時に仮想 FS の `/sd/` に入れ、一覧から開ける
- Share はスクリプトを deflate（`CompressionStream`）して URL の `#` に入れる。サーバー不要
- 編集中のスクリプトは localStorage に自動保存（per-viewer の利便性。失われても困らない用途に限る）
- 例外は行番号付きで Console に出し、エディタの該当行に印を付ける

## 4. wasm ビルド

### 4.1 置き場所

```
sim/
  build_config/midori-sim-tab5.rb    # picoruby の MRuby::CrossBuild（picoruby-wasm.rb を下敷きに）
  build_config/midori-sim-cores3.rb
  shim/                               # ESP-IDF / M5Unified の代わりのヘッダと実装（§4.2）
    M5Unified.h  freertos/  esp_timer.h  esp_log.h  nvs.h  esp_heap_caps.h
    sdkconfig/<board>/sdkconfig.h     # CONFIG_USB_MIDI_BOARD_* などを固定値で
  port/                               # wasm 用の実装
    panel_wasm_fb.cpp  platform_wasm.cpp  rtos_loop.c  nvs_web.c
  gem/midori-sim/                     # 上の C/C++ と main/ui/*.cpp をビルドに入れる mrbgem
  mrblib/sim_boot.rb                  # BoardConfig / MIDIDevices / ScriptManager
  web/                                # JS ホスト・Web MIDI・デザイナー（ES modules）
  build.sh                            # emsdk で全部ビルドして site/simulator/wasm/ へ
mrbgems/picoruby-midi/ports/wasm/     # トランスポート・入力タスク・クロックの wasm 版
mrbgems/picoruby-ui/ports/wasm/       # 必要なら（まず ports/esp32/ui.c を shim 下でビルドしてみる）
```

- picoruby-wasm の `mrbgem.rake` が最終リンク（emcc のフラグ・`EXPORTED_FUNCTIONS`）を持っているので、
  midori-sim gem から自分の export（`midori_sim_frame` / `_touch` / `_midi_in` など）を足せるようにする。
  picoruby-wasm 側に手が要るなら upstream に「gem から export を追加する口」を提案する
- picoruby-wasm は `MRB_INT32` + word boxing。gem の `mrb_get_args("i")` に `int` を渡す誤り
  （CLAUDE.md の picoruby-gpio の件）は wasm では表に出ないので、実機側のレビューで引き続き見る
- 入れる gem: picoruby-wasm / picoruby-ui / picoruby-midi / picoruby-midi-mml / mruby の `*-ext` 系
  （`picoruby_gems.rb` と揃える）。USB / BLE / UART / SAM2695 / filesystem-fat は入れず、
  §5.2 の仮想デバイスで置き換える

### 4.2 移植層（shim）

main/ui が使っている外部 API は限られている（`#include` の集計: M5Unified 25 ファイル、esp_log 17、
esp_timer 12、FreeRTOS 7、esp_heap_caps 6、nvs 2、USB / BLE のヘッダ数件）。**main/ui のソースは
変えずに**、同名のヘッダを `sim/shim` に置いて差し替えるのを基本にする。

| 依存 | wasm での実装 |
|------|--------------|
| LovyanGFX（M5GFX 0.2.24 に同梱） | 描画コアをそのままビルド。パネルは自前の `Panel_WasmFB`（RGB565 のメモリ、`display()` で dirty 矩形を JS へ）。M5GFX のプラットフォーム判定は shim で wasm 用を選ばせる。成立しなければ LovyanGFX の SDL パネル + Emscripten の SDL2 に切り替える（段階 0 で判断） |
| M5Unified | 使っている部分だけの薄い shim（`M5.Lcd` / `M5.Display` / `M5.Touch` / `M5.update()`、Power は空）。タッチは JS から受けたタッチ点を M5.Touch と同じ状態遷移（pressed / released / dragging）で返す |
| `ui_fb`（Tab5 / CrowPanel の直接コピー） | `Panel_WasmFB` のメモリへのコピーにする |
| FreeRTOS（タスク・ミューテックス・キュー） | ミューテックスは空実装（1 スレッドなので）。キューはリングバッファ。`xTaskCreate` は**タスクを登録するだけ**で、本体はホストループが回す（§4.3） |
| esp_timer | `performance.now()` 基準の µs 時計 + one-shot / periodic タイマ表。ホストループが期限の来たものを呼ぶ |
| NVS | localStorage（キー = `nvs/<名前空間>/<キー>`）。MIDI Devices のパッチ・Settings が保存される |
| esp_log / printf | Console パネルへ |
| esp_heap_caps | `malloc`（PSRAM / 内部 RAM の区別なし） |
| USB / BLE のヘッダ | 「つながっていない」を返すスタブ |

### 4.3 タスクとホストループ

実機の各タスクは「無限ループ + 待ち」の形をしている。wasm では待てないので、**ループ 1 回分を関数に切り出す
小改修を main/ui と gem の port に入れる**（ESP32 側はその関数を従来どおりループで呼ぶので動作は変わらない）。

| 実機 | wasm |
|------|------|
| UI タスク（`UIManager::update()` を周期実行） | `requestAnimationFrame` ごとに 1 回（60 fps）。タブが裏に回ったら止まってよい |
| Sequencer タスク（Core 1 / 優先度 5）・F8 間の 3 tick（esp_timer one-shot） | ホストループの高頻度側（下記）で「期限が来た分だけ」進める |
| MIDI 入力タスク（ルート・filter） | Web MIDI の `onmidimessage` で即座にリングに積み、高頻度側で処理 |
| picoruby-wasm のスケジューラ | picoruby-wasm 既定の `setTimeout(run, 0)` バッチ |
| `MIDI::Clock`（esp_timer） | 高頻度側。送信には時刻を付ける（§5.3） |

高頻度側は `MessageChannel` で回すループ（`setTimeout` の 4 ms 下限を避ける）を 1 本だけ持ち、
1 回あたりの処理時間に上限を設けて UI を止めないようにする。

## 5. MIDI

### 5.1 トランスポートの対応

picoruby-midi の登録表（16 ビット）はそのまま使う。ports/wasm がトランスポートを次のように登録する。

| 実機のデバイス | シミュレーターでの中身 | Ruby |
|---------------|----------------------|------|
| USB-MIDI Host（0x01） | **Ports パネルで選んだ Web MIDI の入出力ポート**（例: 手元の USB 鍵盤） | `MIDIDevices.usb_midi_host` |
| USB-MIDI Device（0x04） | 選んだ Web MIDI ポート（例: DAW へつなぐ IAC / loopMIDI） | `MIDIDevices.usb_midi_device` |
| SAM2695（UART 0x02） | **内蔵音源**（WebAudio の GM 風シンセ）。Ports パネルで Web MIDI 出力に振り替えも可 | `MIDIDevices.sam2695` |
| AMY | 段階 5 で AMY の wasm 版（AMY は Emscripten ビルドを持つ）。それまでは内蔵音源 | `MIDIDevices.amy` |
| Script / UI::Keys / Tempo / Tombola / XYPad / Sequencer | 実機と同じ（C のまま動く） | 同じ |
| その他の Web MIDI ポート | 動的ビットで登録（表示名はポート名）。MIDI Devices 画面に行・列として出る（§12.1） | `MIDIDevices.web_midi("名前")`（gem `picoruby-web_midi`、wasm のみ。§5.2） |

### 5.2 スクリプトの互換性

- `MIDIDevices.*` / `MIDI::Device.new(...)` / `MIDI.route` / `MIDI.on` / `MIDI.filter` は実機と同じ API。
  `sim_boot.rb` は `main_task_base.rb` の `setup_midi_devices` と同じ順で登録し、NVS（localStorage）の
  保存済みパッチを復元する。共通にできる部分は `main_task_base.rb` から切り出して両方で使う
- `MIDIDevices.web_midi` は `mrbgems/picoruby-web_midi`（wasm のビルドだけに入れる gem）が足す。実機には無いので、
  エディタで警告を出す（実機では NoMethodError になるので、`MIDIDevices.respond_to?(:web_midi)` で分けるよう案内する）。
  基本は「USB-MIDI Host 役」「USB-MIDI Device 役」への割り当てで済ませ、スクリプトを実機互換に保つ
- `/sd/plugins` は仮想 FS に置けば起動時に読む（filter の登録など）

### 5.3 タイミング

- 送信は Web MIDI の `output.send(data, timestamp)` に**時刻付き**で渡す。C 側は送るべき時刻（µs）を持って
  いるので、`performance.now()` 基準に直して数 ms 先読みで渡す。これで JS のタイマのジッタが音に出ない
  （先読み量は Ports パネルで調整、既定 10 ms）
- 受信は `MIDIMessageEvent.timeStamp` を C の受信時刻として渡す（外部クロックの BPM 計測に使う）
- 内蔵音源も同じ時刻で `AudioContext` の時刻に換算して鳴らす

### 5.4 権限とブラウザ

- `navigator.requestMIDIAccess()` は Ports パネルを開いたとき（ユーザー操作の後）に呼ぶ。SysEx の権限は
  スクリプトが SysEx を使うときだけ求める
- Web MIDI が無い・拒否されたときは内蔵音源と画面内デバイスだけで動く（エラーにしない）
- `AudioContext` は最初のタッチ / Run で resume する（自動再生の制限）

## 6. 画面デザイナー（R2）

### 6.1 正本は Ruby スクリプト、編集用にプロジェクトモデル

Ruby を解析して GUI に戻すのはやらない。代わりに:

- デザイナーは **プロジェクトモデル**（JSON）を持つ: ボード、使うデバイス、Pads / Knobs / Keys / XYPad /
  Tombola / Tempo の設定、レイアウト（§7）、各部品の「動作」
- スクリプトはモデルから**決まった形で生成**する。生成したスクリプトの先頭にモデルを
  `# midori-designer: <base64 JSON>` として埋め込むので、保存した `.rb` をデザイナーで開き直せる。
  実機はコメントを無視して普通に動く
- 「動作」はプリセット（CC 送信 / ノート trigger / Program Change / 何もしない）か、ユーザーが書く
  **Ruby の本文**（ブロックの中身だけ）。本文はモデルに文字列で持つ
- 生成されたスクリプトを直接書き換えたいときは「スクリプトとして編集」でシミュレーターへ渡す
  （以降はデザイナーで開けない旨を表示）

### 6.2 生成するスクリプト（プロジェクトスクリプト）

デザイナーで作るものは、**プロジェクト全体を 1 本の Ruby スクリプト**として生成する。画面の設定（§6）・
すべてのレイアウト（§7）・使うデバイスとルート・部品の動作が 1 ファイルに入り、このファイルがプロジェクトの UI を
丸ごと管理する。シミュレーターで動かすのも、`/sd` 直下に保存するのも（§7.3）この同じファイル。

実機では**普通のスクリプト**として動く。Scripts 画面から選んで実行するほか、起動時に既定で実行するスクリプト
（別途実装予定の機能）にも指定できる。どちらでも `$midori_mode == :script` で、Supervisor の新しい VM で動き、
終われば UI モードに戻る。

すぐに反映できるよう、部品の定義を**名前の付いたメソッド**に分けて生成する（§6.3 で 1 つだけ定義し直すため）。

```ruby
# midori-designer: eyJ2IjoxLCJib2FyZCI6InRhYjUiLC4uLn0=
# MyStage — MIDoRI Designer で作成（2026-11-12）。編集はデザイナーで
require 'midi'
require 'ui'

module MyStage
  # --- デバイスとルート
  def self.synth
    @synth ||= MIDI::Device.new(MIDIDevices.sam2695)
  end

  def self.routes
    MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.sam2695) if MIDIDevices.usb_midi_host
  end

  # --- 1 つしかないモデルのインスタンス（複数のレイアウトから同じものを置く）
  def self.xy
    @xy ||= UI::XYPad.new(scale: [48, 50, 52, 55, 57], y_cc: 74, device: synth)
  end

  # --- 画面の設定（§6）
  def self.knob_1
    UI.knob(1, label: "Cutoff", color: :cyan, value: 64) do |v|
      synth.control_change(74, v.to_i)
    end
  end

  def self.pad_1
    UI.pad(1, label: "Kick") { synth.trigger(36, 127, duration: 100) }
  end

  # --- レイアウト（§7）。それぞれ Home のタイル
  def self.layout_perform
    UI.layout("Perform", cols: 4, rows: 3) do
      areas <<~GRID
        xy xy xy k
        xy xy xy k
        p  p  p  p
      GRID
      xypad area: :xy, pad: MyStage.xy
      knob area: :k, label: "Cutoff" do |v| MyStage.synth.control_change(74, v.to_i) end
      grid(area: :p, cols: 4) { 4.times { |i| pad col: i do MyStage.synth.trigger(36 + i) end } }
    end
  end

  def self.layout_solo
    UI.layout("Solo", cols: 1, rows: 1) do
      xypad col: 0, row: 0, pad: MyStage.xy
    end
  end

  def self.setup
    routes
    knob_1; pad_1
    @perform = layout_perform   # UI.layout はレイアウトを返す
    layout_solo
    UI.knob_send_all(bank: :all)
    UI.show(@perform)           # 最初に出す画面（デザイナーで選ぶ）
  end

  def self.run
    sm = ScriptManager.new
    until sm.stop_requested?
      UI.process
      MIDI.sleep_ms 10
    end
  end
end

MyStage.setup
MyStage.run
```

- ルート・テンポ・最初の画面もプロジェクトの設定として書ける（スクリプトなので、プラグインのような制約はない）。
  スクリプトが終わると Supervisor のクリーンアップでルート・レイアウト・`UI.menu` は元に戻る
- ループは `ScriptManager#stop_requested?` を見て抜ける（Scripts 画面の Stop で協調停止できるように）
- モジュール名（`MyStage`）はプロジェクト名から作る定数
- デバイスや XYPad / Tombola / Keys のインスタンスはメソッドで参照するので、定義を 1 つだけ評価し直しても動く。
  XYPad / Tombola / Keys はモデルが 1 つなので（UI_LAYOUT_DSL.md の実装メモ）、1 つのファイルで全レイアウトを
  管理する形にしておけば、複数のレイアウトに同じインスタンスを安全に置ける。デザイナーは他の部品と同じく
  「プロジェクト内で 1 つ」を検証する。プラグインのレイアウトは `$midori_mode == :ui` でしか定義されないので、
  スクリプトの実行中にぶつかることはない
- シミュレーターでは `sim_boot.rb` が実機のスクリプトモードを再現してこのファイルを `load` する（§3.1）

### 6.3 すぐに反映する仕組み

インスペクタで値を変えると（入力は 150 ms 程度でまとめる）、変更の種類で反映の仕方を変える。

| 変更 | 反映 | 状態 |
|------|------|------|
| 部品の値・見た目（label / color / CC 番号 / scale / value / 動作の本文…） | **ライブパッチ**: 変わった部品のメソッドを定義し直して呼ぶ Ruby を、実行中の VM で評価する（例: `module MyStage; def self.knob_1 … end; end; MyStage.knob_1`） | 保たれる（他の部品のノブ値・押下中のパッド・鳴っている音） |
| レイアウトの構造 | 同じタイトルでレイアウトを定義し直す（AMYPanel と同じ再定義の仕組み）。表示中なら表示し直す | レイアウト内の値は生成コードの `value:` に今の値を入れて引き継ぐ |
| デバイス・ボードの変更、ライブパッチの評価エラー | **再起動**（§3.1） | ノブ値などはモデルに書き戻した値で復元 |

- ライブパッチは JS → wasm の「評価要求」で積み、Ruby のメインループ（`UI.process`）の中で評価する。
  ハンドラ実行中に定義を差し替えないため（AMYPanel の `request` → `_poll` と同じ理由）。評価口は
  `sim_boot.rb` が `UI.process` をラップして用意し、ユーザーのスクリプトからは見えない
- 逆向き: 画面でノブを回した・パッドを押した結果（値）はイベントでデザイナーに戻し、インスペクタの値を更新する
  （`value:` の初期値に採用するかはボタンで選ぶ）

### 6.4 インスペクタの内容

部品ごとの設定は既存の Ruby API のキーワード引数そのもの（docs/KNOBS.md / XYPAD.md / KEYS.md / TOMBOLA.md /
TEMPO.md / ENVELOPE.md）。API の一覧を `sim/web/designer/schema.js` に 1 か所で書き、インスペクタのフォームと
コード生成の両方をそこから作る。

| 画面 | 主な設定 |
|------|---------|
| Pads | 番号、label、color、動作（ノート / CC / PC / 本文） |
| Knobs | 番号、bank、label、color、value、origin、sensitivity、動作 |
| Keys | octaves、low、velocity、device、鍵ごとの色（スケール / 和音のプリセット） |
| XYPad | scale、glide_range、y_cc、hold、スロットごとの上書き |
| Tombola | sides、rotation、gravity、scale、ボール、device |
| Tempo / Sequencer | BPM、ルート（Sequencer は保存ファイルの読み込みまで。編集は画面で） |
| MIDI ルート | `MIDI.route(from, to, channel:)` の一覧（MIDI Devices 画面と同じクロスバーを HTML で） |

## 7. レイアウトデザイナー（R3）

### 7.1 画面

```
┌──────────────────────────────────────────────────────────────────────┐
│ MyStage [開く▾][保存▾]  レイアウト: [Perform ▾][+]  cols [4] rows [3] │
├───────────────┬──────────────────────────────────┬───────────────────┤
│ 部品パレット   │  グリッド編集                      │ インスペクタ       │
│  pad          │  ┌────┬────┬────┬────┐            │  xypad (xy)       │
│  knob         │  │ xy      xy   xy │ k  │            │   scale [...]     │
│  keys         │  ├              ┤    │            │   y_cc  [74]      │
│  xypad        │  │ xy      xy   xy │ k  │            │   device [▾]      │
│  tombola      │  ├────┬────┬────┬────┤            │                   │
│  tempo        │  │ p  │ p  │ p  │ p  │            │                   │
│  envelope     │  └────┴────┴────┴────┘            │                   │
│  grid（入れ子）│  [プレビュー = 実機の描画 ↗]       │                   │
└───────────────┴──────────────────────────────────┴───────────────────┘
```

- グリッド上をドラッグして矩形を選び、パレットの部品を置く。置いた部品は名前付きの area になる
- トラックの幅（`cols: [1, 2, 1]` のような比率）と `grid`（入れ子）も docs/UI_LAYOUT_DSL.md の範囲で扱う
- 右のプレビューはデバイス表示そのもの（シミュレーターと同じ wasm）。編集のたびに §6.3 のレイアウト再定義で
  反映され、その場でタッチして操作できる

### 7.2 生成と検証

- 生成するのは `areas <<~GRID` を使った `UI.layout` ブロック（人が読み書きしやすい形）
- **グリッドの解決と検証は VM 内の本物の `ui_layout.rb` に任せる**（重なり・はみ出し・1 レイアウトに 1 つまでの
  部品の重複など）。エラーは例外として返ってくるので、該当セルを赤くしてメッセージを出す。JS 側に同じ検証を
  書かない
- レイアウトはプロジェクトスクリプト（§6.2）の `layout_*` メソッドとして生成する。1 つのプロジェクトに
  いくつでも持て、それぞれ（スクリプトの実行中）Home のタイルになる。レイアウトだけを別ファイルに分けることはしない

### 7.3 `/sd` への保存

保存の単位は**プロジェクト**。§6.2 のプロジェクトスクリプト 1 本（全レイアウト・画面の設定・デバイスとルート）を
**`/sd/<プロジェクト名>.rb`**（SD カードの直下）に保存する。

- 直下の `*.rb` は Scripts 画面の一覧に出るので、選んで実行できる
- 起動時に既定で実行するスクリプトとして指定することも想定している（その指定の仕組みは別途実装する。
  デザイナーは保存ダイアログで「起動時のスクリプトにする」を出せるよう、指定方法が決まったら合わせる）
- レイアウトを足す・消すのもこのファイルを保存し直すだけで、レイアウトごとのファイルは作らない
- `/sd/plugins` には保存しない（プラグインは毎回の起動・全モードで読まれるもの。プロジェクトはスクリプトとして
  選んで動かすもの）

#### ファイル

- 中身は §6.2 そのもの。先頭の `# midori-designer:` にプロジェクトモデル全体を埋め込み、デザイナーで開き直せる
- ファイル名はプロジェクト名から作る（英数字と `_`）。同名のファイルがあれば上書きを確認する。既存ファイルに
  `# midori-designer:` が無い（手書きのスクリプト）ときは上書きせず別名を勧める
- 名前を変えて保存したら、古い名前のファイルを消すか尋ねる
- シミュレーターのエディタで書いた普通のスクリプトも同じく `/sd/<名前>.rb` に保存できる

#### 保存先は 3 つ

| 保存先 | 仕組み | 条件 |
|--------|--------|------|
| **シミュレーターの `/sd`** | 仮想 FS（Emscripten の FS を `/sd` にマウントし、IndexedDB に同期）へ書く。シミュレーターの Scripts 画面にも出る | なし（どのブラウザでも）。ブラウザのデータを消すと消える |
| **実機の SD（USB 接続）** | Web Serial で実機の CDC を開き、**PicoModem の `FILE_WRITE`** で `/sd/<プロジェクト名>.rb` へ送る（docs/PICOMODEM.md。実機側は実装済み・改修不要）。Scripts 画面の一覧に出すには SD の再読み込み（Scripts 画面）が要るので、送ったあと「実行する」を選ぶとコンソールの `load /sd/<名前>.rb` を送る | Chromium 系（Web Serial）、実機が `midi_device` モード（コンソールが USB CDC）、UI モード（スクリプト実行中は不可） |
| **PC に挿した SD カード** | File System Access API（`showDirectoryPicker`）で SD のルートを選び、直下に書く。選んだフォルダのハンドルは IndexedDB に覚えて次から聞かない | Chromium 系。それ以外のブラウザは `.rb` のダウンロード（手でコピー） |

- 実機への転送は Ruby ではなく JS で行う（PicoModem のホスト側は picoruby-wasm の demo の
  `terminal.html` に JS で実装されているので、それを `sim/web/picomodem.js` にモジュールとして移す）。
  シミュレーターの VM とは別の経路なので、シミュレーターを動かしたまま転送できる
- 実機が `host` モード（CoreS3 / Freenove の既定）だと USB は MIDI 機器側でコンソールが UART なので、
  Web Serial では届かない。この場合は SD カードを PC に挿す方法を案内する
- 転送の前に実機が UI モードか（`> ` プロンプトが返るか）を確かめ、スクリプト実行中なら `stop` を送るか尋ねる
  （実行中のプロジェクトを上書きして実行し直す流れ: `stop` → `FILE_WRITE` → `load`）

#### 開き直す

開き直しと一覧は §7.4。開いたファイルの `# midori-designer:` からプロジェクトモデルを復元する。無いファイル
（手書き）はシミュレーターのエディタで開く（デザイナーでは編集できない）。

### 7.4 デザイナーの Scripts（実機の `/sd` を扱う）

実機の Scripts 画面と同じく、**`/sd` 直下のスクリプトを一覧して、開く・保存する・実行する**パネルをデザイナーに持たせる。
接続先は「シミュレーターの `/sd`」「実機（Web Serial）」「PC の SD カード」の 3 つで、操作は共通。

```
┌ Scripts ─────────────── [接続: 実機 Tab5 ▾] [↻] ┐
│ ● my_stage.rb      プロジェクト  Perform, Solo   │
│ ● live_set.rb      プロジェクト  Main            │
│   arp_demo.rb      スクリプト                     │
│   tombola.rb       スクリプト                     │
├──────────────────────────────────────────────────┤
│ [開く] [名前を付けて保存] [実行] [停止]            │
└──────────────────────────────────────────────────┘
```

#### 実機側に足すもの（コンソールコマンド）

PicoModem にはディレクトリ一覧が無いので、実機のコンソールに次のコマンドを足す（`console_input.c`）。
どちらも **C 側で完結**させる（`stop` / `irb` と同じ。スクリプト実行中でも答えられる）。

| コマンド | 動作 |
|---------|------|
| `ls` | C が持っているスクリプト一覧（`notify_scripts_to_c` が SD をスキャンして渡したもの。`g_script_list`）を 1 行 1 ファイルで返す。機械で読むため、前後に区切り行（`#<ls` / `#>ls <件数>`）を付ける |
| `rescan` | SD の再読み込みを要求する（Scripts 画面の再読み込みと同じフラグ）。UI モードのループが処理し、一覧が更新される |

- 一覧は C の `g_script_list` なので、FatFs に Ruby からアクセスしなくてよい。プロジェクトかどうか（`# midori-designer:` の有無）
  とレイアウト名は、デザイナーがファイルを `FILE_READ` したときに分かるので、読んだものはキャッシュして表示する
- 保存（`FILE_WRITE`）の後は `rescan` を送って実機の Scripts 画面の一覧も更新する。新しいファイルを足したときだけでよい
- 実行は `load /sd/<名前>.rb`、停止は `stop`（既存のコマンド）
- 索引ファイル（以前の案の `_designer_index.txt`）は使わない

#### シミュレーター・PC の SD カード

- シミュレーターの `/sd` は仮想 FS を直接列挙する。実行は §3.1 の Run と同じ
- PC の SD カードは File System Access API でルートを列挙する（Chromium 系のみ。他は「ファイルを開く」/ ダウンロード）

## 8. サイトと CI

- ページ: `site/simulator/`（§3.3）、`site/designer/`（§6, §7。左上のタブで「画面」「レイアウト」）、
  それぞれ `site/ja/` 版。デバイス表示・Web MIDI・ホストループは `sim/web` の共通モジュールで、両ページが使う
- ライブラリは CDN に頼らず `site/assets/vendor/` に置く（CodeMirror 6 のバンドル）
- `pages.yml` に wasm のビルドを足す: emsdk（版を固定）→ `sim/build.sh` → `site/simulator/wasm/` に
  `midori-sim-<board>.{js,wasm}`。`paths` に `sim/**`、`main/ui/**`、`mrbgems/**`、`components/midori-runtime/picoruby` を足す。
  ビルドが落ちたら前回のサイトを残す（Pages の公開はしない）
- examples と `site/reference/` の各コード例に「シミュレーターで開く」リンク（Share と同じ URL 形式）
- サイズの目安は段階 0 で測る（目標: 1 ボードあたり wasm が gzip 後 3 MB 以下）

## 9. 段階

| 段階 | 内容 | 完了条件 |
|------|------|---------|
| 0. 技術検証 | LovyanGFX + `Panel_WasmFB` + shim で Home / Knobs 画面を canvas に描く。picoruby-wasm と同じ wasm にリンクし、`UI.knob` で定義したノブをマウスで回せる | §2.1 A の可否、wasm サイズ、インスタンス作り直しの時間、描画の fps |
| 1. シミュレーター | 全画面、タッチ、Run / Stop、Console、Examples、内蔵音源（SAM2695 役）、Tab5 | `examples/knobs.rb` `keys.rb` `xypad.rb` `tombola.rb` が実機と同じに見え、音が出る |
| 2. Web MIDI | ports/wasm のトランスポート、Ports パネル、時刻付き送信、外部クロック、MIDI Devices 画面のパッチ保存 | 外部鍵盤 → Keys が光る / Sequencer → DAW が同期して鳴る |
| 3. 画面デザイナー | プロジェクトモデル、コード生成、ライブパッチ、インスペクタ（Pads / Knobs / Keys / XYPad から） | 値を変えると音を止めずに反映、保存した `.rb` が実機で動き、デザイナーで開き直せる |
| 4. レイアウトデザイナー | グリッド編集、`ui_layout.rb` による検証、プロジェクトスクリプトの生成、`/sd` への保存（シミュレーターの `/sd`・実機へ PicoModem・PC の SD カード）と開き直し | examples/plugins/perform.rb 相当を含む複数レイアウトのプロジェクトをデザイナーだけで作り、1 本のスクリプトとして実機の `/sd` 直下に保存し、Scripts 画面から実行すると全レイアウトが Home に出る |
| 5. 仕上げ | CoreS3 ビルド、AMY の wasm 版、Share URL、ja / en、リファレンスからのリンク | |

## 10. シミュレーターでは扱わないもの

- BLE-MIDI、USB の列挙（機器の抜き差し）、OTA / ファームウェアの書き込み
- 実機の SD カードをシミュレーターの `/sd` としてマウントすること（実機の SD へはファイル単位の保存・読み込みのみ。§7.3）
- irb（段階 5 以降の候補。Console を入力可能にすれば picoruby-shell の irb がそのまま動く見込み）
- 実機の性能の再現（ブラウザの方が速い。重いスクリプトが実機で間に合うかは実機で確認する）

## 11. リスクと決定事項

### 11.1 決定事項（2026-11-13）

- **最初のボードは Tab5**
- **`MIDIDevices.web_midi` を入れる**。新しい mrbgem `mrbgems/picoruby-web_midi` として用意し、wasm のビルドだけに入れる（§5.1）
- **デザイナーはシミュレーターとは独立したページ**（`site/designer/`）として作る
- 実機との接続は USB CDC（`midi_device` モード）が前提。`host` モードの実機には直接つながらないことは了承済み
- **デザイナーに Scripts 画面相当の機能を持たせる**: 実機の `/sd` のスクリプト一覧・読み込み・保存（§7.4）
- 起動時に既定で実行するスクリプトの指定は別途実装する。デザイナーの保存ダイアログはその指定方法に合わせる

### 11.2 リスク（まず試す。難しければ代案を相談する）

1. ~~**LovyanGFX / M5GFX を Emscripten でビルドできるか**~~ → **できた**（§12）
2. **main/ui に入れる小改修の範囲** → ここまでは 2 か所（§12）。ESP32 側の挙動を変えないこと、実機での回帰確認が要る
3. **1 スレッドでの負荷**。Tab5 の 1280x720 を毎フレーム全面描くと重い可能性。dirty 矩形の転送と、
   画面が変わらないフレームの省略で対処（実装済み。ブラウザでの計測はまだ）
4. ~~picoruby-wasm の upstream 追従~~ → picoruby-wasm の最終リンクは使わず、sim/build.sh が libmruby.a を自分でリンクする
   （picoruby-wasm の mrbgem.rake は無改造。エクスポートは sim 側で足す）。picoruby-wasm のリンクフラグが変わったら追う
5. PicoModem の動作確認はこれまで `midi_device`（CDC）モードのみ。Tab5 / CoreS3 / CrowPanel それぞれで転送を確かめる

## 12. 段階 0 の結果（2026-11-13）

**§2.1 A（main/ui の C++ をそのまま wasm に）で成立した。** Tab5 の画面が実機と同じ描画で出て、Ruby のスクリプトが動く。

- **LovyanGFX**: M5GFX 0.2.24 に同梱の LovyanGFX の描画コアは Emscripten で無改造のままビルドできた。
  プラットフォームは `platforms/sdl` を選ばせ、SDL2 は**偽のヘッダ**（`sim/shim/SDL2/SDL.h`、時計と型名だけ）で
  済ませた（SDL2 のポートはリンクしない）。パネルは `Panel_FrameBufferBase` の派生 `Panel_WasmFB`。
  M5GFX.cpp は非 ESP 側が SDL 前提なのでビルドせず、使うメンバだけ `sim/port/m5gfx_wasm.cpp` に書いた
  （パネルは CrowPanel と同じく `M5GFX::init(Panel_Device*)` で差し込む）
- **main/ui は 26 ファイルすべて shim の上でコンパイルできた。** shim は ESP-IDF / FreeRTOS / M5Unified の
  同名ヘッダ（`sim/shim`）と、その実装（`sim/port`）。未定義のシンボルはリンクで全部埋めている
  （`-sERROR_ON_UNDEFINED_SYMBOLS=1`）
- **ソースの改修は 2 か所**（どちらもタスクのループ 1 回分の切り出し。ESP32 では従来どおりタスクで回る）:
  - picoruby-midi `ports/esp32/midi.c`: 入力タスクの本体を `midi_input_pass()` に。`MIDI_INPUT_POLLED` のときは
    タスクを作らず、ホストが `MIDI_Input_poll()` を呼ぶ（gem は MIDoRI もシミュレーターも知らないまま）
  - `main/ui/ui_sequencer.cpp`: タスクの本体を `handle_item()` に。`MIDORI_SIM` のときはキューを空にするステップを登録
- **Ruby**: picoruby-wasm + MIDoRI の gem（picoruby-midi / -midi-mml / -ui / -usb_midi_host / -usb_midi_device /
  -uart_midi / -sam2695）を `sim/build_config/midori-sim.rb` でビルド。gem の port は wasm に無いので rake は
  バインディングと mrblib だけを作り、port は sim 側のビルド（ESP32 の port をそのまま shim の上で）から入る
- **起動の Ruby は実機と共有**: `main_task_base.rb` の前半（`MIDIDevices`、`MIDI.start!` / `UI.process` のラップ、
  `setup_midi_devices` など）を、目印のコメントまでビルド時に切り出して使う（`sim/build.sh`）。
  そのため `main_task_base.rb` に目印のコメントを 1 行入れた。目印より前は SD カード・ScriptManager・モードに依存させない
- **確認できたこと**（Node 上で wasm を動かし画面を画像にして確認）: Home、Knobs（Ruby の `UI.knob` をドラッグで回すと
  ブロックに値が届く）、Layout DSL（`areas` + knob + 入れ子の grid の pad。タップで SAM2695 役へノート）、Keys
  （C++ 側の発音経路で SAM2695 役へ）、Sequencer の起動
- **サイズ**: wasm 1.85 MB（gzip 前）、JS 132 KB。LovyanGFX と main/ui で約 0.3 MB、残りは PicoRuby
- **まだ**: 仮想 `/sd` と Scripts、AMY、SysEx、CoreS3、デザイナー、CI（ブラウザでの動作確認は済み）

### 12.1 web_midi（2026-11-13）

[mrbgems/picoruby-web_midi](../mrbgems/picoruby-web_midi/)（wasm のビルドだけに入れる、mruby のみ。別リポジトリ kirikak2/picoruby-web_midi のサブモジュール）。

- ページは Web MIDI の入力と出力を**名前で組にして 1 ポート**（スロット、最大 8）にし、VM の起動前に
  `web_midi_port()` で渡す。ポートは picoruby-midi の登録表の動的ビットを取り、入力はパケットソースとして
  入力タスクが読む。だから `MIDI.route` / `MIDI.on` / filter / MIDI Devices 画面が実機のトランスポートと同じに効く
- Ruby: `MIDIDevices.web_midi("名前")`（完全一致、なければ部分一致・大小無視）/ `MIDIDevices.web_midis` / `WebMIDI::Port`
- 起動時（`load_plugins` の位置 = 保存済みパッチの前）に `WebMIDI.register_devices` が MIDI Devices 画面へ載せる。
  キーは名前の FNV-1a（`w` + 16 進 8 桁。画面のキーは 12 バイトまでで、「IAC Driver Bus 1 / 2」は先頭が同じ）。
  入力だけのポートは出力の列に出さない（picoruby-ui に `register_builtin(..., output: false)` を追加）
- 「役」（USB-MIDI Host / Device / SAM2695 の代わり）への割り当ては従来どおりで、同じポートを役と名前の両方で使ってもよい
- ポートの増減は次の Run から（開閉は実行中の VM にすぐ届く）
- **picoruby-midi の修正**: filter（`MIDI.on` など）がある間の `MIDI.sleep_ms` は C のキュー待ちで寝るが、シミュレーターの
  C は待てずに即座に戻り、Ruby の時計はページの tick でしか進まないので抜けられなかった。`MIDI_Filter_can_wait()`
  （`MIDI_INPUT_POLLED` では false）を足し、待てないホストでは VM の中で 5 ms ずつ寝てからキューを見る
- **シミュレーターのビルド**: rake は gem の mrblib の変更に気付かない（実機と同じ）。`sim/build.sh` は libmruby.a より
  新しい mrblib / src を持つ MIDoRI の gem のビルドを消してから rake を呼ぶ
