# picoruby-midi 標準化計画

本ドキュメントは、Midori 内で開発してきた `picoruby-midi` および関連 mrbgem を upstream
PicoRuby プロジェクトに標準 mrbgem として提出するための方針とタスクをまとめたものです。

最終更新: 2026-05-05（Phase 6 完了）

## 対象 gem

標準化作業は以下の **gem セット**に対して行う（単独では成立しない）：

| gem | 役割 | 状態 |
|---|---|---|
| `picoruby-midi` | MIDI プロトコル層・パーサ・スケジューラ・クロック | リファクタリング |
| `picoruby-midi-mml` | MML パーサ + プレイヤ | **新規分離**（現 picoruby-midi から切り出し） |
| `picoruby-usb_midi_host` | USB-MIDI Host トランスポート（USB Host stack 含む） | **リネーム + リファクタリング**（現 `picoruby-usb_midi`） |
| `picoruby-uart_midi` | UART MIDI トランスポート（汎用） | **新規作成** |
| `picoruby-sam2695` | SAM2695 シンセ用薄いラッパ | リファクタリング(UART_MIDI に委譲、ports 削除) |

将来的に追加予定：

| gem | 役割 | 状態 |
|---|---|---|
| `picoruby-usb_midi_device` | USB-MIDI Device トランスポート（TinyUSB ベース） | **将来追加**（M5Stack Tab5 等の USB OTG Device 対応ボード用） |
| `picoruby-ble_midi` | BLE-MIDI プロトコル抽象（Apple BLE-MIDI 仕様準拠） | **将来追加**（picoruby-ble の上に構築） |

依存関係（標準化後）：

```
picoruby-midi-mml ──→ picoruby-midi ──→ picoruby-machine
                            ↑
                            │ (Transport interface)
   ┌────────────────────────┼────────────────────────┬───────────────┬───────────────┐
   │                        │                        │               │               │
picoruby-usb_midi_host  picoruby-uart_midi           │  picoruby-usb_midi_device  picoruby-ble_midi
   │                        ↑                        │     (将来追加)               (将来追加)
   │                        │                        │        │                       │
   ▼                  picoruby-sam2695               │        ▼                       ▼
[ESP-IDF usb_host]   (UART_MIDI に委譲) ─────────────┘    [TinyUSB]            picoruby-ble (BTstack)
```

## 進捗状況（2026-05-05 時点）

Phase 1〜6 まで完了。Midori 上で動作確認済み（M5Stack Tab5 / ESP32-P4）。
残タスクは Phase 7（テスト）／Phase 8（upstream 提出）。

### Phase 1: 内部リファクタリング ✅

- `src/midi_parser.c` / `src/midi_scheduler.c` / `src/midi_clock_gen.c` に OS-free コアを抽出
  （`aae01c40` / `1e63c2bf` / `dc86f8f0`）
- `midi_transport_t` 抽象を導入し、`MIDI_Note_trigger` 等を transport 経由に置換
  （`1defcbbf`）
- Input task を Transport interface 経由で動かす形に変更（`42e28e84`）
- `picoruby_esp32_*` への直接依存を削除し、cooperative-stop / cleanup hook 化（`0f3745da`）
- 既知問題: SysEx 入力時の mrbc heap 破壊クラッシュ。**Phase 1 以前から再現する pre-existing
  バグ**であり、本標準化と独立した別 issue として扱う。

### Phase 2: API 整理 ✅（一部は方針変更）

- `MIDI.start!` を新設（`d3313412`）。Midori 側 `main_task_base.rb` は `start!`/`bpm_loop`
  を薄くラップして UI とスクリプト停止 hook を注入する形に。
- `_get_transport_mask` を `transport.transport_id` 経由に置換（`c6ec44fb`）
- `send_midi_batch` を `trigger_batch` にリネーム（`87434ce8`）
- **方針変更**: `MIDI.on_tick` ではなくユーザの希望により `MIDI.on_bpm_change` フックを採用
  （`d73994f8`）。BPM 変更通知は明示的にテンポ追従が必要な箇所（UI バッジ等）にだけ届く。
- レガシー API（`_pop_event` / `_external_bpm` 等の USB-only 版）はまだ残置。
  upstream 提出前に整理予定。

### Phase 3: mruby バインディング ✅

- `src/mruby/midi.c` を mrubyc 版とフィーチャ等価にした（`96d60da8`）。

### Phase 4: スコープ整理 ✅

- MML 関連を `picoruby-midi-mml` に分離（`4b709ef5`）。
- アプリ層参照（`UI.bpm` / `ScriptManager`）を picoruby-midi から完全除去（`d73994f8`）。
- Midori の bridge 役は `main_task_base.rb` に集約。

### Phase 5: 関連 gem の標準化 ✅

- **5a-1** picoruby-usb_midi → picoruby-usb_midi_host にリネーム（`4e844e96`）
- **5a-2** USB Host stack を gem に取り込み（`71bc801b`）。midori `main/usb_midi_host.c` は
  `USB_MIDI_HOST_start_driver()` を呼ぶだけの ~70 行に縮小。
- **5b** picoruby-uart_midi を新規作成（`d7eeee7b`）。31250 baud デフォルト + 任意 baud 指定可。
- **5c** picoruby-sam2695 を pure-Ruby thin wrapper に縮小（`698c82e0`）。`include/` `src/`
  `ports/` を削除（~1085 行 → 64 行）。`SAM2695.new` は内部で `UART_MIDI.new` を構築する。

### Phase 6: 規約準拠 ✅

- 5 gem すべてに `README.md` / `sig/*.rbs` / `example/` を整備（`9f8886c5`）。
  各 gem に最低 1 本の動作する例（`example/*.rb`）を含む。
- `picoruby-midi` の `mrbgem.rake` の author を `Toshio Maki`、summary を
  `MIDI protocol layer (parser, scheduler, clock) for PicoRuby` に最終化。
  他の 4 gem は Phase 5 時点ですでに反映済み。

### 残タスク

- Phase 7: ホストビルドのパーサ単体テスト、mruby/mrubyc 両ビルド確認、RTOS 無し環境（RP2040 等）
  での動作確認。
- Phase 8: upstream 提出（メンテナとのスコープ確認、PR 分割方針）。

## 背景

`picoruby-midi` は Midori 開発の過程で実装された MIDI プロトコル層で、以下を提供する：

- `MIDI::Device` — トランスポート抽象上で動く MIDI 出力デバイス
- `MIDI::Input` — MIDI 受信のイベント駆動ハンドラ
- `MIDI::Clock` — マスタークロック生成 / 外部クロック sync
- `MIDI::MML` — MML パーサ + プレイヤ
- `MIDI.bpm_loop` — BPM 同期ループ + イベントディスパッチ
- Note Scheduler — 自動 note_off スケジューラ（マルチタッチ用）

トランスポート実装は別 gem として：
- `picoruby-usb_midi`（→ 標準化時に `picoruby-usb_midi_host` にリネーム） — USB-MIDI ホスト用 ring buffer + bridge API
- `picoruby-sam2695` — UART MIDI 送受信（SAM2695 シンセ向けに作られたが実態は UART MIDI）

機能としては充実しているが、現状のままでは「Midori 専用に見える」状態であり、
upstream に提出するには構造的なリファクタリングが必要。

また将来的には：
- M5Stack Tab5（ESP32-P4）で USB OTG Host + Device 同時運用が可能なため、
  `picoruby-usb_midi_device` を別 gem として追加する余地を残す
- BLE 対応ボード（ESP32 系・RP2040W 系等）向けに `picoruby-ble_midi` を別 gem として
  追加する余地を残す（Apple BLE-MIDI 仕様準拠、`picoruby-ble` の上に構築）

## 標準化に向けた問題点

### Critical（必ず修正）

1. **ESP32 / FreeRTOS / 特定トランスポートへの強い結合**
   - `ports/esp32/midi.c` が `picoruby-usb_midi`（→ `picoruby-usb_midi_host`）/ `picoruby-sam2695` のヘッダを直接 include
   - `MIDI_Note_trigger` が C 内部で `USB_MIDI_send_packet` / `SAM2695_send_packet` を直接呼ぶ
   - Input task が USB / SAM2695 専用キューをハードコード
   - Clock タイマが `picoruby_esp32_stop_requested()` / `picoruby_esp32_midi_cleanup()` を参照
   - **MIDI プロトコル層であるべきが、実態は Midori 専用オーケストレーション層**

2. **mruby バインディングが大幅に欠落**
   - `src/mruby/midi.c` は Clock の 5 メソッドのみ
   - `src/mrubyc/midi.c` には Input / Trigger / Batch / External BPM が全部入り
   - PicoRuby は両 VM サポート前提なので、mruby 側は **未完成扱い**

3. **アプリ層への暗黙参照**
   - `bpm_loop` が `Proc.new { UI.bpm }` で UI モジュール（M5Stack 固有）を参照
   - `ScriptManager.new` で picoruby-esp32 独自仕組みに依存

4. **トランスポート抽象の破綻**
   - `_get_transport_mask` が `case @transport.class.to_s when "USB_MIDI"` で文字列マッチ
   - トランスポートに正しい抽象を持たせるべき

### Major（直したほうが良い）

5. **スコープが広すぎ — MML 含む**
   - `midi_mml.rb` (333行) + `midi_mml_player.rb` (347行) は別 gem `picoruby-midi-mml` に切り出し推奨

6. **レガシー API の同梱**
   - `_pop_event` / `_external_bpm` / `_reset_external_clock` は USB-only legacy 版が残存
   - 標準化前に整理すべき技術的負債

7. **規約準拠ファイル不足**
   - `README.md` がない
   - `sig/midi.rbs` がない
   - `example/` がない（他のペリフェラル gem には全部ある）

8. **メタデータ**
   - `mrbgem.rake` の `spec.author = 'PicoRuby'` を実態に合わせる

9. **グローバル状態**
   - parser / scheduler / clock すべて global static
   - 単一インスタンス前提が強く、複数ポート同時利用ができない

10. **命名の混乱**
    - `send_midi_batch` は実体が note_on の trigger 専用 → `trigger_batch` に

11. **`MIDI.bpm_loop` の名前と引数**
    - Input→Output 変換スクリプトでも "BPM ループ" を回す必要があり、命名が合わない
    - 引数が多く、example で全員が `subdivisions: 24, send_start: false, on_loop: UI.process` を書いている
    - → 後述の `MIDI.start!` で再設計

### 関連 gem 固有の問題点

#### picoruby-usb_midi（→ `picoruby-usb_midi_host` にリネーム）

- **gem 名が役割を反映していない**: 実体は USB Host MIDI のみ。将来 USB Device MIDI を
  追加する余地を考えると、明示的に `host` を含む名前が望ましい
- **author = `PicoRuby`** プレースホルダのまま
- **単一インスタンス前提**: `$__usb_midi_instance__` グローバル
- **USB ホストスタックを持たない**: USB Host 処理本体は midori `main/usb_midi_host.c`
  にあり、gem 側は ring buffer + bridge API（`USB_MIDI_notify_connected/disconnected`,
  `push_rx_data`, `pop_tx_packet`）のみを提供
  - upstream 提出時に「USB ホスト stack をどこが持つか」の設計判断が必要
- **README / sig / example 不足**
- **ESP32 port のみ**（rp2040 等の port 無し）
- **Transport interface 適合**: 既に `send_packet` / `bytes_available` / `read_available`
  / `connected?` を提供しており、picoruby-midi の Transport interface 導入時に
  そのまま実装側として接続可能

#### picoruby-sam2695

- **author = `PicoRuby`** プレースホルダのまま
- **gem 名と実態の乖離**: 名前は SAM2695 だが、実装は **UART MIDI トランスポート**
  そのもの（SAM2695 固有のコマンドは無い）
  - → 標準化時に `picoruby-uart_midi` または `picoruby-serial_midi` への
    リネーム/汎用化を検討
- **`DEFAULT_TX_PIN = 13` のハードコード**: ボード差異吸収は呼び出し側で
- **単一インスタンス前提**: `$__sam2695_instance__` / `$__sam2695_tx_pin__` グローバル
- **README / sig / example 不足**
- **ESP32 port のみ**

## 設計方針

### 1. FreeRTOS 分離（OS非依存コアの抽出）

現状の OS 依存はすべて `ports/esp32/midi.c` に集中（`include/` と `src/` には依存ゼロ）。
依存は4種類のみで、すべて抽象化可能：

| 用途 | 現在 | 必要な抽象 |
|---|---|---|
| MIDIクロック生成 | `esp_timer` 周期コールバック | `tick(now_us)` |
| Note Scheduler の note_off | 1ms 周期 `esp_timer` | `tick(now_us)` |
| Input処理 | FreeRTOS task + `xQueue` | `poll()` |
| 時刻取得 | `esp_timer_get_time()` | `Machine.uptime_us` で抽象化済 |

**MIDI バイトパーサ自体（`parse_raw_midi_byte` / `parse_usb_midi_packet` / SysEx accumulator
/ 外部BPM計算）は完全に OS-free。** これがコア。

Ruby 層は既に `respond_to?(:_init_timer)` ガードで C タイマー無し前提の fallback パスを
持っているため、Ruby 側の修正は最小限で済む。

### 2. ディレクトリ構成（提案）

```
include/midi.h              # 抽象API（既にOS-free）
include/midi_transport.h    # トランスポート抽象（関数ポインタ）

src/midi_parser.c           # 純粋なバイトパーサ + SysEx accumulator (OS-free)
src/midi_scheduler.c        # tick(now_us) 駆動の note_off スケジューラ (OS-free)
src/midi_clock_gen.c        # tick(now_us) 駆動の 24PPQ 出力ロジック (OS-free)
src/midi_input_core.c       # transport_ops 経由でバイトを引き、parser に渡す (OS-free)
src/mruby/midi.c            # mruby bindings (poll系メソッドを公開)
src/mrubyc/midi.c           # mrubyc bindings 同上

ports/esp32/midi_port.c     # esp_timer + FreeRTOS task で tick/poll を駆動
ports/rp2040/midi_port.c    # hardware_alarm 版（任意）
（portsを使わない場合）       # Ruby側から MIDI::Input#process と MIDI.start! が tick を駆動

sig/midi.rbs                # RBS シグネチャ
example/                    # 使用例
README.md
```

### 3. Core API（OS非依存）

```c
/* parser — 状態を context 構造体に持つ */
typedef struct midi_parser midi_parser_t;
midi_parser_t *midi_parser_new(midi_event_source_t source);
bool midi_parser_feed_raw(midi_parser_t *p, uint8_t byte, midi_event_t *out);
bool midi_parser_feed_usb(midi_parser_t *p, uint8_t cin, uint8_t b1,
                          uint8_t b2, uint8_t b3, midi_event_t *out);

/* scheduler — port が無ければ Ruby が tick を呼ぶ */
void midi_scheduler_tick(uint64_t now_us);
int  midi_scheduler_trigger(midi_transport_t *tx, uint8_t ch, uint8_t note,
                            uint8_t vel, uint32_t dur_ms);

/* clock generator — 同上 */
void midi_clock_tick(uint64_t now_us);
void midi_clock_set_bpm(float bpm);
```

### 4. Transport 抽象

```c
typedef struct midi_transport_ops {
    int  (*send_packet)(void *ctx, uint8_t cable, uint8_t cin,
                        uint8_t b1, uint8_t b2, uint8_t b3);
    int  (*read_bytes)(void *ctx, uint8_t *buf, size_t maxlen);
    int  (*bytes_available)(void *ctx);
    bool (*is_connected)(void *ctx);
    uint8_t transport_id;  /* USB=1, SERIAL=2, BLE=3, ... */
} midi_transport_ops_t;

typedef struct midi_transport {
    const midi_transport_ops_t *ops;
    void *ctx;
} midi_transport_t;
```

トランスポートは USB-MIDI / Serial-MIDI / BLE-MIDI 等を別 gem として実装し、
`MIDI::Device.new(transport)` に注入する。`_get_transport_mask` の文字列マッチは
`transport.ops->transport_id` で代替。

### 5. Port API（オプショナル）

```c
/* RTOSあり: 起動するとバックグラウンドで tick/poll を呼ぶ */
int  midi_port_start(midi_port_config_t *cfg);
void midi_port_stop(void);
/* RTOSなし: 何も提供しない。Ruby/ユーザコードが tick を呼ぶ */
```

### 6. RTOSなし運用時の流れ

```ruby
input = MIDI::Input.new(device)
clock = MIDI::Clock.new(device)
loop do
  input.process                     # → C側 midi_input_core_poll()
  clock.tick(Machine.uptime_us)     # → C側 midi_clock_tick() / scheduler_tick()
  Kernel.sleep_ms(1)
end
```

### 7. 精度トレードオフ

| 項目 | RTOS版（midori） | poll版 |
|---|---|---|
| Clock精度 (120 BPM ≒ 20.8ms周期) | esp_timer ISR ≈ ±数十μs | Rubyループ依存、ms単位 |
| Note-off精度 | 1ms timer | poll間隔依存 |
| Input遅延 | task駆動でほぼ即時 | poll間隔分の遅延 |
| 同時演奏 | 〇 | △（GC/長いブロックでズレる） |

高精度が必要な Midori は ESP32 port を引き続き使い、低精度でよい用途は port 無しで動く、
という二層構成。

## `MIDI.bpm_loop` → `MIDI.start!` への置き換え

### 現状の使用パターン分析

Midori の examples を分類：

| パターン | スクリプト | BPM意味あり？ | output指定？ |
|---|---|---|---|
| **A. Input→Output変換** | `launch_control_xl`, `midi_monitor`, `midi_to_lumi`, `seaboard_blocks` | **なし** | なし |
| **B. シーケンサ** | `bach_air`, `dual_device`, `pad` | あり | あり |
| **C. external sync** | （example 無し） | 受信側依存 | 任意 |

全 example が以下を共通で書いている：
- `subdivisions: 24` ← 全例で同じ
- `send_start: false` ← Type A で必須、Type B でも 4/3 で指定
- `on_loop: Proc.new { UI.process }` ← 全例で同じ

→ **defaults が現実と乖離している**のが諸悪の根源。

### 新 API: `MIDI.start!`

```ruby
def start!(bpm: 120, output: nil, sync: nil, &block)
```

引数を5個 → 3個（と block）まで圧縮。

| 引数 | 役割 | 省略時 |
|---|---|---|
| `bpm:` | クロック生成BPM。`Numeric` または `Proc`/`callable`（動的取得） | 120 |
| `output:` | クロック出力する `MIDI::Device` | なし＝クロック非送信 |
| `sync:` | 外部 sync 元 `MIDI::Input`（指定時は外部BPMに追従） | なし |
| `&block` | 毎クロック（24 PPQ）に呼ばれる callback。`yield clock_count` | なし＝poll専用ループ |

### 削除する引数

- **`subdivisions:`** — 内部実装を 24 PPQ 固定にし、ユーザーが block 内で `clock % N == 0` で間引く
- **`send_start:`** — `output` 有無から自動判定
- **`bpm_source:`** — `bpm:` に Proc を渡せばよい
- **`on_loop:`** — picoruby-midi の責務外。後述の hook 機構に分離
- **`on_error:`** — block 内 `rescue` に移譲
- **`input:`（sync 用）** — `sync: input` で input 自体を渡せばよい

### UI.process フックの分離

`on_loop: Proc.new { UI.process }` は picoruby-midi が知るべきでないアプリ層の関心事。
モジュールレベル hook に分離：

```ruby
# picoruby-midi 内
module MIDI
  @tick_hooks = []
  class << self
    def on_tick(&block)
      @tick_hooks << block
    end
    def _run_tick_hooks
      @tick_hooks.each { |h| h.call rescue nil }
    end
  end
end
```

```ruby
# midori 側（main_task_base.rb で1回だけ登録）
MIDI.on_tick { UI.process }
```

これで SD カード上のユーザースクリプトは `MIDI.start!` を呼ぶだけで `UI.process` が回る。
RTOSなし環境や picoruby-midi 単体使用時は hook を登録しなければ何も起きない。

### 書き換え後のスクリプト例

**Type A（Input→Output 変換）**
```ruby
# Before
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  # 空
end

# After
MIDI.start!
```

**Type B（シーケンサ）**
```ruby
# Before
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(25, output: device, subdivisions: 24, on_loop: on_loop) do |clock|
  player.tick(clock)
end

# After
MIDI.start!(bpm: 25, output: device) { |clock| player.tick(clock) }
```

**Type C（external sync）**
```ruby
# After
MIDI.start!(output: device, sync: input_device) { |clock| player.tick(clock) }
```

**動的 BPM**
```ruby
MIDI.start!(bpm: -> { UI.bpm }, output: device) { |clock| ... }
```

## タスク一覧

### Phase 1: 内部リファクタリング（後方互換維持）

優先度高。Midori の動作を壊さずに進められる。

- [ ] `ports/esp32/midi.c` から純粋なパーサロジックを `src/midi_parser.c` に抽出
  - `parse_raw_midi_byte` / `parse_usb_midi_packet` / SysEx accumulator
  - グローバル状態を `midi_parser_t` 構造体に格納
- [ ] Note Scheduler を `src/midi_scheduler.c` に抽出
  - `MIDI_Note_scheduler_tick(now_us)` API を新設
  - ESP32 port 側は esp_timer から `_tick` を呼ぶラッパに
- [ ] Clock generator を `src/midi_clock_gen.c` に抽出
  - 同様に `MIDI_Clock_tick(now_us)` API を新設
- [ ] Transport 抽象 `midi_transport_t` を導入
  - `MIDI_Note_trigger` 等が `transport->ops->send_packet(...)` を呼ぶ形に
  - 既存の `USB_MIDI_send_packet` / `SAM2695_send_packet` 直接呼びを置換
- [ ] Input task を transport 抽象経由で動くように変更
  - 現状の USB / SAM2695 専用キューを汎用化
- [ ] `picoruby_esp32_stop_requested()` / `picoruby_esp32_midi_cleanup()` 参照を削除
  - cleanup hook を関数ポインタで受け取る形に

### Phase 2: API 整理

- [ ] `MIDI.start!` を新設（`bpm_loop` の改良版）
- [ ] `MIDI.on_tick` hook 機構を実装
- [ ] Midori `main_task_base.rb` で `MIDI.on_tick { UI.process }` を 1 回だけ登録
- [ ] examples を `MIDI.start!` で書き換え（後方互換のため `bpm_loop` も deprecation 経由で残す）
- [ ] `bpm_loop` に deprecation warning を追加
- [ ] レガシー API 削除（`_pop_event` / `_external_bpm` / `_reset_external_clock` の USB-only 版）
- [ ] `_get_transport_mask` を transport interface 経由に置換
- [ ] `send_midi_batch` を `trigger_batch` にリネーム

### Phase 3: VM サポート充実

- [ ] mruby バインディング (`src/mruby/midi.c`) を mrubyc 版とフィーチャ等価にする
  - Input 系メソッド全部
  - `_trigger` / `_scheduler_clear` / `_send_batch`
  - External BPM / reset_external_clock の各種

### Phase 4: スコープ整理

- [ ] MML 関連 (`midi_mml.rb` / `midi_mml_player.rb`) を別 gem `picoruby-midi-mml` に分離
- [ ] アプリ層参照（`UI.bpm` / `ScriptManager.new`）を完全除去

### Phase 5: 関連 gem の標準化

#### picoruby-usb_midi → picoruby-usb_midi_host へリネーム

- [ ] gem 名を `picoruby-usb_midi` → **`picoruby-usb_midi_host`** にリネーム
- [ ] Ruby クラス名を `USB_MIDI` → **`USB_MIDI_HOST`** に変更
- [ ] include/src/ports/mrblib のファイル名を `usb_midi_host` プレフィックスに変更
  - `include/usb_midi.h` → `include/usb_midi_host.h`
  - `src/usb_midi.c` → `src/usb_midi_host.c`
  - `src/mruby/usb_midi.c` → `src/mruby/usb_midi_host.c`
  - `src/mrubyc/usb_midi.c` → `src/mrubyc/usb_midi_host.c`
  - `ports/esp32/usb_midi.c` → `ports/esp32/usb_midi_host.c`
  - `mrblib/usb_midi.rb` → `mrblib/usb_midi_host.rb`
- [ ] C シンボル名を変更
  - `USB_MIDI_send_packet` → `USB_MIDI_HOST_send_packet` 等
  - `USB_MIDI_notify_connected` / `_disconnected` / `push_rx_data` / `pop_tx_packet` 等も同様
- [ ] **USB Host stack を gem 内に取り込み**（設計判断 B 案1採用）
  - midori `main/usb_midi_host.c` の USB Host 処理を `ports/esp32/` に移植
  - デバイス列挙 / MIDI Streaming subclass 検出 / Bulk endpoint 管理 / ホットプラグ
  - midori 側は gem の API を呼ぶだけで USB MIDI Host が使える形に
- [ ] picoruby-midi の Transport interface を実装する形に修正
- [ ] グローバル変数 `$__usb_midi_instance__` 依存の整理（→ `$__usb_midi_host_instance__`）
- [ ] `mrbgem.rake` の `author` / `summary` / `require_name` を実態に合わせる
  - `require_name = 'usb_midi_host'`
- [ ] `README.md` / `sig/usb_midi_host.rbs` / `example/` を整備
- [ ] midori 側の追従修正
  - SDカード上の examples の `USB_MIDI` 参照を `USB_MIDI_HOST` に書き換え
  - `main_task_base.rb` の `MIDIDevices` モジュール内 `USB_MIDI.instance` を更新
  - `main/` 配下の C コードで `USB_MIDI_*` を呼んでいる箇所を更新

#### picoruby-uart_midi（新規作成）

- [ ] gem を新規作成（`mrbgem.rake` / 基本ディレクトリ構成）
- [ ] picoruby-sam2695 の `ports/esp32/sam2695.c` から汎用 UART MIDI 部分を移植
  - UART driver init / send / receive / input task
  - 31250 bps デフォルト、引数で変更可能に
- [ ] `UART_MIDI` クラスを mrblib に実装（Transport interface 実装）
- [ ] mruby / mrubyc 両 VM の bindings を実装
- [ ] `README.md` / `sig/uart_midi.rbs` / `example/` を整備
- [ ] `mrbgem.rake` の `author` を実態に合わせる

#### picoruby-sam2695（薄いラッパに変更）

- [ ] `ports/esp32/sam2695.c` を**削除**（picoruby-uart_midi に移管済み）
- [ ] `src/mruby/sam2695.c` / `src/mrubyc/sam2695.c` を**削除または最小化**
  - SAM2695 固有 C コードがなければ完全に Pure Ruby gem 化
- [ ] `mrblib/sam2695.rb` を `UART_MIDI` への委譲形式にリファクタリング
- [ ] `include/sam2695.h` を削除（または SAM2695 固有定数のみ残す）
- [ ] `mrbgem.rake` に `add_dependency 'picoruby-uart_midi'` を追加
- [ ] `mrbgem.rake` の `author` を実態に合わせる
- [ ] `DEFAULT_TX_PIN` ハードコードを除去
- [ ] グローバル変数 `$__sam2695_instance__` / `$__sam2695_tx_pin__` 依存の整理
- [ ] `README.md` / `sig/sam2695.rbs` / `example/` を整備

#### picoruby-usb_midi_device（将来追加・本標準化スコープ外）

本標準化と同時には作成しないが、Tab5 等の USB OTG Device 対応ボードで使うために
将来追加する予定。設計上の余地は以下：

- [ ] (将来) gem 名: `picoruby-usb_midi_device`
- [ ] (将来) Ruby クラス名: `USB_MIDI_DEVICE`
- [ ] (将来) ESP-IDF `esp_tinyusb` component ベースで実装
- [ ] (将来) Transport interface を実装し、picoruby-midi からは均質に扱える
- [ ] (将来) Host gem と同時 require 可能 — 単一 OTG ポート ボードでは排他チェック
- [ ] (将来) USB Configuration Descriptor の宣言（Class Audio / Subclass MIDI Streaming）

#### picoruby-ble_midi（将来追加・本標準化スコープ外）

BLE 対応ボード向けに Apple BLE-MIDI 仕様準拠の MIDI トランスポートを提供する gem。
本標準化と同時には作成しないが、設計上の余地を残す：

- [ ] (将来) gem 名: `picoruby-ble_midi`
- [ ] (将来) Ruby クラス名: `BLE_MIDI`
- [ ] (将来) `picoruby-ble` の上に構築
- [ ] (将来) BLE GATT Service / Characteristic UUID（Apple BLE-MIDI 仕様）の登録
- [ ] (将来) Transport interface を実装し、picoruby-midi からは均質に扱える
- [ ] (将来) BLE-MIDI 固有のタイムスタンプ付きパケット形式の解釈・付与ロジック
- [ ] (将来) Peripheral / Central 両モード対応（init 引数で切替）
- [ ] (将来) MTU 制限対応・SysEx 分割送信
- [ ] (将来) `picoruby-ble` の ESP32 port 整備が前提（NimBLE または Bluedroid）

→ 本標準化では Transport interface を `send_packet` / `bytes_available` /
`read_available` / `connected?` で揃えておくことで、後続の USB Device / BLE-MIDI gem
追加時に picoruby-midi の修正が不要となるよう設計する。

### Phase 6: 規約準拠（picoruby-midi 本体）

- [ ] `README.md` を新規作成（他の gem を参考に）
- [ ] `sig/midi.rbs` を新規作成
- [ ] `example/` ディレクトリを gem 直下に作成（Midori の examples から汎用的なものを抜粋）
- [ ] `mrbgem.rake` の `author` を実態に合わせる

### Phase 7: テスト

- [ ] パーサのホストビルドユニットテスト（OS-free 化により可能になる）
- [ ] mruby と mrubyc 両 VM でのビルド確認
  - `picoruby-midi` / `picoruby-midi-mml` / `picoruby-usb_midi_host` / `picoruby-uart_midi` / `picoruby-sam2695`
- [ ] FreeRTOS 無し環境（RP2040 等）での動作確認
- [ ] picoruby-sam2695 が picoruby-uart_midi を介して動作することの確認
- [ ] picoruby-midi-mml の単独動作確認

### Phase 8: upstream 提出

- [ ] PicoRuby メンテナに以下を事前確認
  - MIDI gem のスコープ感（プロトコル層のみか、scheduler 含めるか）
  - ports に何を期待するか
  - `picoruby-uart_midi` / `picoruby-usb_midi_host` の命名と既存 gem との衝突有無
  - USB-MIDI gem に USB ホストスタックを含める方針の是非
  - 将来的な `picoruby-usb_midi_device` 追加方針への意見
  - 将来的な `picoruby-ble_midi` 追加方針への意見（picoruby-ble の ESP32 port
    整備状況も含めて確認）
- [ ] 5 gem セットでまとめて PR 作成
  - `picoruby-midi`
  - `picoruby-midi-mml`
  - `picoruby-usb_midi_host`（旧 `picoruby-usb_midi`）
  - `picoruby-uart_midi`
  - `picoruby-sam2695`
- [ ] レビュー対応

## 設計判断（決定済）

1. **`subdivisions` は削除しない。デフォルトを 24 PPQ とする。**
   - 引数自体は残し、`subdivisions: 24` を default にする
   - examples で全員が `24` を書いていた状況を default で吸収
   - 必要に応じて `subdivisions: 1`（四分音符のみ）等に上書き可能
   - これにより `MIDI.start!` のシグネチャは：
     ```ruby
     def start!(bpm: 120, output: nil, sync: nil, subdivisions: 24, &block)
     ```

2. **`on_tick` hook 機構は picoruby-midi には入れない。**
   - midori 側の `main_task_base.rb` で `MIDI.start!` を wrapper する形で `UI.process` を注入する
   - picoruby-midi 本体は UI / ScriptManager 等のアプリ層概念を一切持たない
   - 実装イメージ（midori 側）：
     ```ruby
     # main_task_base.rb（midori 側）
     module MIDI
       class << self
         alias_method :_original_start!, :start!
         def start!(**kwargs, &block)
           wrapped_block = block ? ->(c) { UI.process; block.call(c) }
                                 : ->(c) { UI.process }
           _original_start!(**kwargs, &wrapped_block)
         end
       end
     end
     ```
   - これにより picoruby-midi は完全に UI 非依存となり、upstream 提出時に余計な hook API も生やさず済む

3. **block 無しでも external clock の送信は継続する。**
   - `output:` が指定されていれば、block の有無に関わらず 24 PPQ でクロック送信
   - block の役割はあくまで「クロックに合わせて何かする」ための拡張点
   - block 無し = "クロック送信 + イベント poll のみ" のループ

4. **`bpm_loop` は alias として当面維持。**
   - 即削除はしない、deprecation warning も当面は出さない
   - 将来のいつかのタイミングで warning → 削除の段階的廃止を検討
   - upstream 提出時には alias を残したまま提出

5. **MML 分離は標準化と同時に実施。**
   - `picoruby-midi-mml` を別 gem として切り出す
   - upstream に提出するのは `picoruby-midi`（コア）と `picoruby-midi-mml`（MML）の 2 本立て
   - Midori 側の依存関係は両方を引く形に

## 設計判断（関連 gem・決定済）

### A. `picoruby-sam2695` のスコープ

**方針: `picoruby-sam2695` と `picoruby-uart_midi` の 2 gem 体制とする。**

- `picoruby-uart_midi` を**新規作成**し、UART MIDI の汎用トランスポート gem として
  upstream に提出
  - DIN MIDI / 任意の UART MIDI 機器で利用できる
  - 汎用性が高く upstream で受け入れられやすい
- `picoruby-sam2695` は**そのまま gem として残す**（SAM2695 が広く使われている
  デバイスなので独立 gem として価値あり）
  - ただし **ESP32 独自 port を持たない**形にリファクタリング
  - UART 通信は `picoruby-uart_midi` に委譲
  - `picoruby-sam2695` 自身は SAM2695 固有のデフォルト設定（baud=31250 等）と、
    将来的な SAM2695 固有コマンド（GS/GM リセット等）の置き場として薄く存在

#### 現状確認

picoruby-sam2695 の `ports/esp32/sam2695.c` (444 行) を調査した結果、
**SAM2695 固有のコードは存在せず、すべて汎用 UART MIDI 処理**：
- `uart_driver_install` / `uart_param_config` / `uart_set_pin`
- `uart_write_bytes` / `uart_read_bytes`
- 31250 bps（標準 MIDI baud rate）固定
- USB-MIDI packet → raw MIDI bytes 変換

→ ESP32 port を丸ごと `picoruby-uart_midi` に移管可能。

#### リファクタリング後の構成

```
picoruby-uart_midi/
├── include/uart_midi.h     # API
├── src/                    # mruby/mrubyc bindings
├── ports/esp32/uart_midi.c # ESP32 UART driver wrapping
├── ports/rp2040/...        # 将来追加
├── mrblib/uart_midi.rb     # UART_MIDI クラス (Transport interface 実装)
└── mrbgem.rake

picoruby-sam2695/
├── mrblib/sam2695.rb       # SAM2695 クラス (UART_MIDI を内部利用)
└── mrbgem.rake             # add_dependency 'picoruby-uart_midi'
（C コード・ports なし — pure Ruby gem）
```

picoruby-sam2695 の Ruby 実装イメージ：
```ruby
class SAM2695
  DEFAULT_BAUD = 31250  # 標準 MIDI baud rate

  def initialize(tx_pin, rx_pin = -1)
    @uart = UART_MIDI.new(tx_pin: tx_pin, rx_pin: rx_pin, baud: DEFAULT_BAUD)
  end

  # Transport interface を委譲
  def send_packet(*args); @uart.send_packet(*args); end
  def bytes_available;    @uart.bytes_available;   end
  def read_available;     @uart.read_available;    end
  def connected?;         @uart.connected?;        end

  # 将来的に SAM2695 固有コマンドをここに追加
  # def gs_reset; ...; end
  # def reverb_level(level); ...; end
end
```

### B. `picoruby-usb_midi_host` の USB ホストスタック責務

**方針: 案1採用 — gem 側にリファレンス USB ホスト実装を取り込む。**

- ESP-IDF ベースの USB Host 処理を `ports/esp32/` 内に取り込む
- 現状 midori `main/usb_midi_host.c` にある USB Host 処理を gem 内に移植
- example だけで動く「完成品」の gem として提出
- midori 側は gem の API を呼ぶだけで USB MIDI が使える形に

#### 移植対象（midori `main/usb_midi_host.c`）

- USB Host ライブラリ初期化 / イベントループ
- デバイス列挙 / interface descriptor parse
- MIDI Streaming subclass (Audio/MIDI) 検出
- Bulk endpoint 探索 / IN/OUT transfer 管理
- ホットプラグ対応（接続/切断ハンドリング）

#### 残課題

- USB Host stack は ESP32 の場合 ESP-IDF の `usb_host` 依存
- RP2040 等別 port では別実装（TinyUSB Host 等）
- まずは ESP32 port で完全動作する状態で upstream に出し、他 port は後続 PR で

### C. リネームの後方互換

**方針: 後方互換は持たない（別 gem 扱い）。**

- `picoruby-uart_midi` は **新規作成 gem**であり、`picoruby-sam2695` とは別物
- `picoruby-sam2695` は名前を維持したまま中身が変わる（UART_MIDI に依存する形に）
- midori 既存スクリプトの `SAM2695.new(tx, rx)` API は維持される（実装が UART_MIDI
  への委譲に変わるだけ）
- → 既存スクリプトの動作互換は実質的に保たれるが、**alias 等の明示的な互換層は設けない**

### D. USB Host MIDI と USB Device MIDI の gem 分離

**方針: 別 gem として分離。標準化と同時に Host gem をリネーム、Device gem は将来追加。**

USB Host MIDI と USB Device MIDI は MIDI プロトコル層から見ると同じ Transport interface を
実装する均質なトランスポートだが、内部の USB stack（`usb_host` vs TinyUSB）と
ライフサイクルが大きく異なるため、独立した gem に分ける。

#### 標準化と同時に実施

- `picoruby-usb_midi` を **`picoruby-usb_midi_host`** にリネーム
- Ruby クラス名を `USB_MIDI` から **`USB_MIDI_HOST`** に変更
  - 例: `MIDI::Device.new(USB_MIDI_HOST.instance)`
- include/src/ports/mrblib のファイル名・include パス・C シンボル名も `usb_midi_host`
  プレフィックスに変更
  - `USB_MIDI_send_packet` → `USB_MIDI_HOST_send_packet` 等
- midori の既存スクリプト・C コードはすべて新名称に追従して書き換え（後方互換なし）

#### 将来追加: `picoruby-usb_midi_device`

M5Stack Tab5 (ESP32-P4) のように USB OTG Host + Device 同時運用が可能なボードで使う
予定の gem。本標準化と同時には作成しないが、設計上の余地を以下の形で残す：

- 同じ Transport interface（`send_packet` / `bytes_available` / `read_available` / `connected?`）
  を実装するため、picoruby-midi 側の修正は不要
- TinyUSB（ESP-IDF の `esp_tinyusb` component）ベースで実装予定
- Ruby クラス名は `USB_MIDI_DEVICE` を予約
- 同一プロジェクトで Host gem と Device gem を同時 require できる
  - Tab5 等の OTG 2 ポートボードでは両方同時に使える
  - S3 等の単一 OTG ポートボードでは初期化時に排他チェックが必要（未着手）

#### 想定ユースケース（Tab5 等での Host + Device 同時利用）

```ruby
require 'midi'
require 'usb_midi_host'    # 現存
require 'usb_midi_device'  # 将来追加

# PC 側コントローラからの入力 → ESP32 で処理 → DAW へ送信
input  = MIDI::Input.new(MIDI::Device.new(USB_MIDI_HOST.instance))
output = MIDI::Device.new(USB_MIDI_DEVICE.instance)

input.on(:note_on) do |e|
  output.note_on(e[:note] + 12, e[:velocity])  # 1 オクターブ移調して転送
end

MIDI.start!  # 永続ループ
```

#### USB-MIDI 2.0 (UMP) 対応の余地

- 現状はすべて USB-MIDI 1.0（4-byte packet, CIN+MIDI3）
- 将来的な USB-MIDI 2.0 (Universal MIDI Packet, 32/64-bit) サポートのために、
  Transport interface で `send_packet` の方を抽象化済みにしておく
- UMP 対応時には別 gem（`picoruby-usb_midi_host_ump` 等）または既存 gem 内の
  オプション機能として追加検討

### E. BLE-MIDI 対応（`picoruby-ble_midi`）

**方針: 将来追加 gem として `picoruby-ble_midi` を予約。本標準化スコープ外。**

BLE-MIDI（Apple Bluetooth Low Energy MIDI Specification）は Bluetooth LE 上で
MIDI を伝送するプロトコルで、iOS / macOS / Android / Windows などが標準対応している。
USB ケーブル不要のワイヤレス MIDI として、近年のキーボード・コントローラ・
シンセに広く採用されている。

#### 設計方針

- gem 名: `picoruby-ble_midi`
- Ruby クラス名: `BLE_MIDI`
- 既存の `picoruby-ble`（Bluetooth LE 抽象 gem、ESP32 / RP2040W で利用可能）の
  上に構築
- Transport interface（`send_packet` / `bytes_available` / `read_available` / `connected?`）
  を実装し、picoruby-midi からは他の MIDI トランスポートと均質に扱える

#### 実装上の特徴と難所

BLE-MIDI には以下の固有事項があるため、USB-MIDI とは異なる実装ロジックが必要：

1. **タイムスタンプ付きパケット形式**
   - BLE-MIDI パケット = `[Header byte][Timestamp byte][MIDI status][data]...`
   - 13-bit タイムスタンプ（msb=Header の下位6bit、lsb=Timestamp の下位7bit、ms 単位）
   - 正確な BPM 同期にはタイムスタンプの解釈が必要
2. **BLE GATT Service / Characteristic**
   - Service UUID: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
   - Characteristic UUID: `7772E5DB-3868-4112-A1A9-F2669D106BF3`
   - Notify + Write/Write Without Response が必須
3. **Central / Peripheral 両方を考慮する必要**
   - **Peripheral モード**: ESP32 が BLE-MIDI 機器として広告 → スマホ/PC が接続
   - **Central モード**: ESP32 が BLE-MIDI 機器（鍵盤等）に接続
   - USB と同様、内部実装が大きく異なるため `picoruby-ble_midi_peripheral` /
     `picoruby-ble_midi_central` の更なる分割を検討する余地あり
   - 当面は `picoruby-ble_midi` 1 gem 内で role 切替（init 引数で peripheral / central
     を選択）する形を想定。複雑化したら分割
4. **MTU とバッチ送信**
   - BLE の MTU 制限により、長い SysEx は複数パケットに分割
   - 連続する MIDI イベントを 1 つの BLE パケットにまとめると効率が良い
   - これは Transport 内部の最適化

#### 想定ユースケース

```ruby
require 'midi'
require 'ble_midi'

# Peripheral モード: ESP32 が iPad/Mac 等から見える BLE-MIDI 機器になる
ble = BLE_MIDI.new(role: :peripheral, name: 'Midori-MIDI')
device = MIDI::Device.new(ble)
device.note_on(60, 100)

# Central モード: ESP32 が外部 BLE-MIDI 機器に接続する
ble = BLE_MIDI.new(role: :central, target: 'My Keyboard')
input = MIDI::Input.new(MIDI::Device.new(ble))
input.on(:note_on) { |e| puts "#{e[:note]} #{e[:velocity]}" }

MIDI.start!
```

#### picoruby-midi の Transport interface への影響

BLE-MIDI のタイムスタンプは Transport 内で扱い、picoruby-midi の core API には
影響を与えない設計とする：

- 受信側: BLE_MIDI が受信時にタイムスタンプを解釈、必要なら遅延補正をかけて
  USB-MIDI 互換の 4-byte 形式（または相当の MIDI イベント）として
  Transport interface 経由で picoruby-midi に渡す
- 送信側: picoruby-midi から渡された MIDI イベントを、BLE_MIDI が自前で現在時刻を
  Header/Timestamp として付与して BLE 送信
- → Transport interface に新しいメソッドや変更は不要

#### 残課題

- `picoruby-ble` 自体が現状 RP2040W/Pico W 向けに開発されており、ESP32 port が必要
  - ESP32 では NimBLE または Bluedroid を使う形になる見込み
- BLE-MIDI 対応は picoruby-ble の ESP32 port 整備とセットで必要
- まずは ESP32 port + Peripheral モードで完全動作する状態で upstream に出し、
  Central モード / RP2040W port は後続 PR で

## 参考: コード規模（現状 → 標準化後の見込み）

### picoruby-midi（現状）
```
mrblib/midi.rb              44 行
mrblib/midi_clock.rb        171 行
mrblib/midi_constants.rb    144 行
mrblib/midi_device.rb       301 行
mrblib/midi_input.rb        445 行
mrblib/midi_mml.rb          333 行  → picoruby-midi-mml に分離
mrblib/midi_mml_player.rb   347 行  → picoruby-midi-mml に分離
src/mrubyc/midi.c           534 行
src/mruby/midi.c            103 行  (要拡充)
ports/esp32/midi.c          1277 行 (要分割)
合計                         3699 行
```

標準化後の見込み：
- MML 分離で約 680 行を picoruby-midi-mml に移動
- `ports/esp32/midi.c` (1277行) → `src/midi_parser.c` + `src/midi_scheduler.c` +
  `src/midi_clock_gen.c` + `src/midi_input_core.c` + 薄い `ports/esp32/midi_port.c` に分割
- src/mruby/midi.c は mrubyc 版同等まで拡充（103 → ~500 行）

### picoruby-midi-mml（新規・picoruby-midi から分離）
```
mrblib/midi_mml.rb          333 行
mrblib/midi_mml_player.rb   347 行
合計                          680 行（pure Ruby gem）
```

### picoruby-usb_midi_host（現 picoruby-usb_midi をリネーム → 標準化後）
```
現状（picoruby-usb_midi）:
mrblib/usb_midi.rb           69 行
src/mrubyc/usb_midi.c       188 行
src/mruby/usb_midi.c        195 行
ports/esp32/usb_midi.c      377 行
合計                          829 行

標準化後の見込み（picoruby-usb_midi_host）:
mrblib/usb_midi_host.rb     ~80 行
src/mrubyc/usb_midi_host.c  ~190 行
src/mruby/usb_midi_host.c   ~200 行
ports/esp32/usb_midi_host.c ~400 行
+ ports/esp32/usb_host.c    ~800 行  (midori main/usb_midi_host.c から移植)
合計見込み                   ~1670 行
```

参考: 現状 midori 側の `main/usb_midi_host.c` は 823 行で、これが gem 内に取り込まれる。

### picoruby-uart_midi（新規作成）
```
mrblib/uart_midi.rb         ~80 行   (Transport interface 実装、UART_MIDI クラス)
src/mrubyc/uart_midi.c      ~190 行  (現 picoruby-sam2695 のものから流用)
src/mruby/uart_midi.c       ~150 行  (同上)
ports/esp32/uart_midi.c     ~440 行  (現 ports/esp32/sam2695.c をほぼそのまま移管)
合計見込み                   ~860 行
```

### picoruby-sam2695（現状 → 標準化後）
```
現状:
mrblib/sam2695.rb           134 行
src/mrubyc/sam2695.c        183 行   → 削除（UART_MIDI に移管）
src/mruby/sam2695.c         151 行   → 削除
ports/esp32/sam2695.c       444 行   → 削除
合計                          912 行

標準化後の見込み:
mrblib/sam2695.rb           ~50 行   (UART_MIDI に委譲する薄いラッパ)
（C コード・ports なし — pure Ruby gem）
合計見込み                    ~50 行
```

### 全体サマリ

| gem | 現状 | 標準化後（概算） |
|---|---|---|
| picoruby-midi | 3699 行 | 約 3000 行（MML 分離後、再構成済み） |
| picoruby-midi-mml | — | 680 行（新規・picoruby-midi から分離） |
| picoruby-usb_midi_host（旧 picoruby-usb_midi） | 829 行 | 約 1670 行（USB Host stack 取り込み） |
| picoruby-uart_midi | — | 約 860 行（新規・picoruby-sam2695 から移管） |
| picoruby-sam2695 | 912 行 | 約 50 行（薄いラッパに縮退） |
| **合計** | **5440 行** | **約 6260 行**（USB Host stack 取り込み分の純増） |

将来追加予定（本標準化スコープ外）：

| gem | 見込み |
|---|---|
| picoruby-usb_midi_device | 未着手（TinyUSB ベースで ~1000 行程度を想定） |
| picoruby-ble_midi | 未着手（picoruby-ble の上に ~600〜800 行程度を想定。Peripheral/Central role 対応） |

純増分は主に midori `main/usb_midi_host.c` を gem 内に取り込むことに由来し、
**midori 本体側からは同じ量だけコードが減る**。総コード量はほぼ変わらず、
**責務分離と再利用性が大幅に向上**する。
