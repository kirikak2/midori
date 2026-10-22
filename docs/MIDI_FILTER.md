# MIDI filter（ルーティングへの Ruby の差し込み）

> **状態: 実装済み（2026-10-22）**。C のルート表はホストの単体テスト（`host_test/test_midi_route.c`）、
> Ruby 側はスタブを使ったホストテスト（`host_test/filter_test.rb`、CRuby / mruby 3.2）で確認。
> Tab5 / CoreS3 / Freenove のビルドは通る。**実機では未確認**。

`MIDI.route` で張ったルートに、任意の Ruby のブロックを差し込む仕組み。`MIDI::Input#on` の後継。

- ルーティングは基本的に C の中で処理する（入力タスクが直接転送し、Ruby を通らない）。
  **filter を差し込んだときだけ**、その宛先へのメッセージが Ruby を通る
- 宛先を持たない filter が「Ruby で受け取るだけ」の受信口になり、`MIDI::Input#on` を置き換える
- UI::MIDIDevices の「選択に追従する入力」もこの仕組みで作っている（[UI_MIDI_DEVICES.md](UI_MIDI_DEVICES.md)）
- 実装: [midi_filter.rb](../mrbgems/picoruby-midi/mrblib/midi_filter.rb) /
  [midi_route.rb](../mrbgems/picoruby-midi/mrblib/midi_route.rb) /
  [midi_route.c](../mrbgems/picoruby-midi/src/midi_route.c) / ESP32 のキューは
  [ports/esp32/midi.c](../mrbgems/picoruby-midi/ports/esp32/midi.c)
- サンプル: [examples/midi_filter.rb](../examples/midi_filter.rb)

## 決定事項

| 項目 | 決定 |
|------|------|
| ブロックの戻り値 | `MIDI::Message` なら送る / `Array` なら中身を順に送る / `nil`・`false` なら捨てる |
| `MIDI::Input#on` | **非推奨**。`MIDI.on` / 宛先のない `MIDI.filter` に置き換える |
| UI::MIDIDevices のタップ | `MIDI::TapInput` は作らず、**filter に統合** |

## 基本方針

- **filter を付けた宛先の分だけ Ruby を通す**。同じ送信元のほかの宛先は従来どおり C から直接送る
- **C 側で絞り込む**。`types:` と `channel:` を C で判定し、合うものだけを Ruby のキューに積む。
  合わないもの（例: クロック）は filter 付きの宛先にも C から直接送る。24ppq のクロックを Ruby に
  通さないためで、速さはここで決まる
- **filter はスクリプトの中だけで有効**。スクリプト終了時に `MIDI_route_clear()` がルートと一緒に
  消し、`MIDI_Filter_flush()` がキューに残った分を捨てる

## Ruby API

```ruby
kb  = MIDIDevices.usb_midi_host
syn = MIDIDevices.amy

# MIDI.route は MIDI::Route を返す（失敗時は false。従来の if MIDI.route(...) はそのまま動く）
route = MIDI.route(kb, syn)
h = route.filter(types: [:note_on, :note_off]) do |m|
  m.note += 12          # 1 オクターブ上げる
  m                     # 返したものが送られる
end
route.unfilter(h)       # この filter だけ外す / route.unfilter で全部 / route.unroute

# 和音にする（配列を返すと複数送る）。ルートが無ければ張ってから付ける
MIDI.filter(kb, syn, types: [:note_on, :note_off]) do |m|
  [m, m.with(note: m.note + 4), m.with(note: m.note + 7)]
end

# ベロシティカーブ。nil / false を返すと捨てる
MIDI.filter(kb, syn, types: :note_on, channel: 9) do |m|
  m.velocity < 10 ? nil : m.with(velocity: (m.velocity ** 0.7 * 3.3).to_i)
end

MIDI.unfilter(kb, syn)        # (kb → syn) の filter を外す（ルートは残る）
MIDI.unfilter(kb)             # kb からの全ルートの filter を外す（MIDI.on は残る）
MIDI.unroute(kb, syn)         # ルートごと外す（その filter も）
MIDI.unroute_all              # ルート・filter・listener を全部
MIDI.filter_stats             # => { overflows: キューに入らなかった件数 }
```

`from` / `to` は `MIDI.route` と同じく、トランスポート・`MIDI::Device`、または登録表のビット（Integer）。
`MIDI.filter` は登録したブロックを返す（`MIDI.unfilter(from, to, handler)` 用）。表が一杯なら false。

### オプション

| 引数 | 既定 | 意味 |
|------|------|------|
| `types:` | 全チャンネルメッセージ（`:note_on` `:note_off` `:control_change` `:program_change` `:pitch_bend` `:poly_aftertouch` `:channel_pressure`） | Ruby に回す種類。Symbol か配列。システムリアルタイム（`:clock` `:start` `:continue` `:stop` `:active_sensing` `:system_reset`）は明示したときだけ。`:sysex` は不可 |
| `channel:` | nil（全チャンネル） | 0..15 で絞る |
| `overflow:` | `:bypass` | キューが溢れたときの動作（後述） |

`:note_off` を指定すると、ベロシティ 0 の note on（CIN 9）も Ruby に回る（Ruby では `:note_off` として届く）。

### ブロックの約束事

| 戻り値 | 動作 |
|--------|------|
| `MIDI::Message` | そのメッセージを宛先へ送る |
| `Array` | 中身（`MIDI::Message`）を順に送る |
| `nil` / `false` | 捨てる |

- 同じルートに filter を複数付けると、付けた順につないで実行する。前の filter の戻り値が次の入力になる
  （`Array` なら要素ごとに次へ。空になったら打ち切り）。filter の `types` / `channel` に合わない
  メッセージはその filter を素通りする
- ブロックの中で別のデバイスに `dev.note_on` してもよい（宛先を増やす使い方）
- 例外が出たらそのメッセージを捨て、filter ごとに最初の 1 回だけ `[MIDI] filter error: ...` を出す。filter 自体は外さない
- 「何も返さなければ元のメッセージを送る」という約束にはしない。`m.note += 12` だけ書いて戻り値を
  忘れたときに、元の音が鳴ってしまうため

### MIDI::Message

| メソッド | 内容 |
|----------|------|
| `type` | `:note_on` / `:note_off` / `:control_change` / … / `:clock` などのリアルタイム / それ以外のシステムメッセージは `:system` |
| `channel` | 0..15 |
| `note` / `velocity` | ノート系（`data1` / `data2`） |
| `cc` / `program` | `data1` の別名 |
| `value` | velocity / pressure / CC の値 / program。pitch bend は -8192..8191 |
| `raw` | `[cin, b1, b2, b3]`。データバイトは 0..127 に丸める（移調で範囲を出ても壊れたバイトは送らない） |
| `source` | 受信元のキー Symbol（`:usb_host` / `:uart` / `:usb_dev`、UI::MIDIDevices で登録したもの） |
| `device` | 受信元の `MIDI::Device`（UI::MIDIDevices に登録済みなら。無ければ nil） |
| `with(type:, channel:, note:, velocity:, cc:, value:, program:)` | 一部を変えたコピー（mruby/c でも動くように `**` ではなく明示的なキーワード） |

書き換え可能。宛先ごと・呼び出しごとに新しいオブジェクトを作る（保持されても書き換わらないように）。

## 宛先のない filter（MIDI::Input#on の後継）

```ruby
MIDI.on(kb, :note_on) { |m| puts m.note }     # 戻り値は無視
MIDI.on(kb) { |m| ... }                       # :any = 全チャンネルメッセージ
MIDI.on(kb, :clock) { |m| ... }               # リアルタイムは明示したときだけ
MIDI.on(kb, :control_change, channel: 0) { |m| ... }
MIDI.off(kb, :note_on)                        # その種類の listener / MIDI.off(kb) で全部 / MIDI.off(kb, nil, handler)
MIDI.external_bpm(kb)                         # 外部クロックの BPM（USB Host / UART / USB Device）
MIDI.reset_external_clock(kb)
MIDI.bpm_loop(120, sync_from: kb) { |c| ... } # input: / sync: の代わり（MIDI.start! も）
```

- `MIDI.filter(kb) { ... }`（宛先なし）と同じ。宛先は、ルート表の中だけで使う特別なビット
  `MIDI_ROUTE_DST_RUBY`（0x100）で表し、トランスポート登録表の 8 枠を使わない
- 従来の `MIDI::Input` は受信キューを 2 本しか持たなかった（USB Host / UART）。filter は
  `MIDI_route_packet()` から分岐するので、**USB Device（PC）と `UI::Keys` からも受け取れる**
- 外部クロックの BPM は送信元のパーサが測る（USB Host / UART は従来のもの、USB Device には
  クロック計測用のパーサを足した）。画面の鍵盤はクロックを出さないので 0.0
- 測れるのは入力タスクがその送信元を読んでいる間だけ。ルートも filter も無い送信元を読ませるのが
  **watch**（`MIDI_route_set_watch()`。持ち主は 2 つ: アプリ = Tempo 画面、スクリプト = `sync_from:`）。
  `sync_from:` は指定した送信元を watch するので、ルートや `MIDI.on` が無くても同期できる
  （スクリプト終了時に `MIDI_route_clear()` が外す）。`MIDI.external_bpm(from)` を直接呼ぶだけでは watch しない

### MIDI::Input の非推奨化

- `MIDI::Input` は互換のため残し、`MIDI::Input.new` の初回に
  `[MIDI] MIDI::Input is deprecated; use MIDI.on(device, :note_on) { |m| ... }` を 1 回出す
- `MIDI::Input` は従来の 2 本のキューを読み続ける（filter のキューとは別なので食い合わない）
- **未対応（TODO）**: 既存の examples（`midi_monitor.rb` `dual_device.rb` `layout.rb` `amy_*.rb` など 8 本）の
  `MIDI.on` への書き換え。実機で動作を見ながら移したいので残してある。全部移したら 2 本のキューと
  `MIDI_Input_set_queueing()` ごと撤去を検討する
- mruby/c 版のバインディング（`src/mrubyc/midi.c`）にも C 関数は足した。mrblib は共通だが、
  mruby/c 上では動かしていない

## C 側の仕組み

```
 入力タスク（USB Host / UART / USB Device） / UI タスク（UI::Keys）
   └ MIDI_route_packet(src, ...)  = MIDI_route_dispatch(src, MIDI_route_targets(src, ...), ...)
        filter 表から (src, 宛先) が合い、CIN が types に入り、チャンネルが合うものを divert に
        MIDI_transport_send(dst_mask & ~divert, ...)          ← 従来の経路
        divert があれば sink（キュー）へ {dst = divert, src, cin, b1..b3} を push（待たない）
          入らなければ overflow: :bypass の宛先へはそのまま送る
                                            │
 Ruby VM: MIDI.run_filters ←────────────────┘  （MIDI_Filter_pop）
   └ 宛先ビットごとに (src, dst) の filter を順に実行 → 戻り値を MIDI._transport_send(dst)
      dst が MIDI_ROUTE_DST_RUBY なら MIDI.on のハンドラを呼ぶだけ
      その (src, dst) の filter が積まれた後に外れていたら、元のまま送る
```

- filter は**ルート表とは別の表**（`MIDI_FILTER_MAX` = 16 件、1 件 = 送信元 1 ビット × 宛先 1 ビット）。
  同じ (src, dst) に Ruby の filter が複数あっても C には 1 件で、types は和集合、channel は全員が
  同じときだけ絞る、overflow は全員 `:drop` のときだけ drop（`MIDI._filter_apply`）
- キューは port 側（ESP32 では FreeRTOS のキュー 64 件、`MIDI_Filter_init()` で初回に作る）。
  `midi_route.c` は OS を知らず、`MIDI_route_set_filter_sink()` で渡された関数に積むだけ
- `UI::Keys` は自分で送る送信元なので、`MIDI_route_dispatch()` を呼ぶよう変えた（note on と note off の両方）
- 入力タスクが USB Device を読む条件 `MIDI_route_from()` は filter（listener 含む）も数える。
  `MIDI_route_active()` も同様

### C API

```c
/* midi_route.h（OS 非依存） */
#define MIDI_ROUTE_DST_RUBY  0x100
#define MIDI_FILTER_MAX      16
#define MIDI_FILTER_OVERFLOW_BYPASS 0
#define MIDI_FILTER_OVERFLOW_DROP   1
typedef struct { uint16_t dst; uint8_t src, cin, b1, b2, b3; } midi_filter_event_t;
void     MIDI_route_set_filter_sink(midi_filter_sink_t sink);
int      MIDI_route_set_filter(uint8_t src_bit, uint16_t dst_mask, uint16_t types, int channel, uint8_t overflow);
int      MIDI_route_clear_filter(uint8_t src_bit, uint16_t dst_mask);   /* 0xFFFF で全部 */
uint16_t MIDI_route_filtered(uint8_t src_bit);                          /* 画面の f 表示 */
uint32_t MIDI_route_filter_overflows(void);
void     MIDI_route_dispatch(uint8_t src_bit, uint8_t dst_mask, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3);
bool     MIDI_route_get(int i, uint8_t *src_bit, uint8_t *dst_mask, int *channel);
void     MIDI_route_set_watch(int owner, uint8_t src_mask);   /* MIDI_WATCH_APP / MIDI_WATCH_SCRIPT */
uint8_t  MIDI_route_watched(void);

/* midi.h（port） */
int  MIDI_Filter_init(void);
bool MIDI_Filter_pop(uint16_t *dst, uint8_t *src, uint8_t *cin, uint8_t *b1, uint8_t *b2, uint8_t *b3);
bool MIDI_Filter_wait(uint32_t timeout_ms);
void MIDI_Filter_flush(void);
float MIDI_Input_get_external_bpm_from(uint8_t src_bit);
void  MIDI_Input_reset_external_clock_from(uint8_t src_bit);
int   MIDI_route_start_source(uint8_t src_bit);
```

## Ruby をいつ実行するか（遅延）

- `MIDI.run_filters` が実行する。呼ばれるのは `MIDI.process_all_inputs`（`MIDI.sleep_ms` /
  `bpm_loop` / `MIDI.start!` の中）と、Midori では `UI.process`（`main_task_base.rb` で包んでいる）。
  自前のループならそこで `MIDI.run_filters` を呼ぶ
- filter があるときの待ち（`MIDI._idle_ms`）は **filter キューを待ちながら寝る**（`MIDI_Filter_wait()`
  = `xQueuePeek` にタイムアウト）。`MIDI.sleep_ms` やクロックループの中で寝ていれば、メッセージが
  来た瞬間に起きて実行するので、遅延は数 ms + ブロックの実行時間。filter が無ければ従来の `Kernel.sleep_ms`
- 素の `sleep_ms`（Kernel）で寝ているスクリプトでは実行されない。重い計算で VM を長く占有しても遅れる
- `run_filters` の先頭で `$__midi_filter_hooks__` を呼ぶ（UI::MIDIDevices.input が選択に追従するため）

## キューが溢れたとき（overflow:）

| 値 | 動作 |
|----|------|
| `:bypass`（既定） | Ruby を通さず元のメッセージをそのまま宛先へ送る。音は止まらないが変換が抜ける |
| `:drop` | 捨てる。変換していない音を鳴らしたくないとき |

既定を `:bypass` にしたのは、note off が捨てられて音が鳴りっぱなしになる被害の方が大きいため。
listener（`MIDI.on`）はどちらでも捨てられる。

## 注意点

- **音程を変える filter は note on と note off に同じ変換をすること**。固定の移調なら自然にそうなる。
  ランダムやスケールの切り替えのように毎回違う変換には `MIDI::NoteMap` を使う：

  ```ruby
  map = MIDI::NoteMap.new
  MIDI.filter(kb, syn, types: [:note_on, :note_off]) do |m|
    map.apply(m) { |x| x.note = scale.sample; x }   # note on だけがブロックを通る
  end
  # filter を替える前に、押さえている音を止める
  map.release_all.each { |x| syn_dev.note_off(x.note, channel: x.channel) }
  ```

  note on の変換結果を (チャンネル, ノート) ごとに覚え、note off をそれに対する note off に置き換える
- filter を通る宛先は、直接転送される宛先より遅れて届く。同じ音を 2 つの音源で重ねると厳密には揃わない
- **SysEx は filter の対象外**（常に直接転送）。`types: :sysex` には C 側で SysEx を組み立ててから
  積む必要がある（入力タスクの既存の組み立て処理を使い回す）
- UI::MIDIDevices の画面では filter 付きのパッチの右上に `f` を出す。filter は NVS に保存しない
