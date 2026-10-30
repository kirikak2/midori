# MIDI Devices 画面（UI::MIDIDevices）

> **状態: 実装済み（2026-10-22）**。Tab5 / CoreS3 / Freenove のビルドと、ホスト上の Ruby テスト
> （`mrbgems/picoruby-ui/test/midi_devices_test.rb`）のみ。**実機では未確認**。

MIDI デバイスを列挙し、デバイス間の IN → OUT をパッチングする画面と、その Ruby API。
旧 MIDI Info 画面を置き換えた（USB デバイス診断はデバイス詳細へ移した）。

- 画面: [main/ui/screen_midi_devices.cpp](../main/ui/screen_midi_devices.cpp)
- モデル（デバイス一覧・選択・NVS）: [main/ui/ui_midi_devices.cpp](../main/ui/ui_midi_devices.cpp)
- Ruby API: [mrbgems/picoruby-ui/mrblib/ui_midi_devices.rb](../mrbgems/picoruby-ui/mrblib/ui_midi_devices.rb)
- 内蔵デバイスの登録: `components/midori-runtime/mrblib/main_task_base.rb` の `setup_midi_devices`
- サンプル: [examples/midi_devices.rb](../examples/midi_devices.rb)
- 関連: [MIDI_FILTER.md](MIDI_FILTER.md)（`UI::MIDIDevices.input` はこの上に作っている）

## 要件

1. MIDI デバイスが列挙される。各デバイスは MIDI IN と MIDI OUT を持つ
2. Ruby から `register` / `unregister` で `MIDI::Device` を登録・削除できる
3. 異なるデバイスの IN / OUT をパッチングして、任意の MIDI IN を MIDI OUT にルーティングできる
4. ネゴシエーションできていない（未接続の）デバイスは非活性になり、ルーティングがあっても何もしない
5. Ruby プログラムが使う `MIDI::Device` を設定でき、`UI::MIDIDevices.midi_in` /
   `UI::MIDIDevices.midi_out` で取り出せる

## 決定事項

| 項目 | 決定 |
|------|------|
| 内蔵デバイスの登録 | **起動時に自動登録**（UI モードでもパッチ盤として使えるように） |
| パッチの保存 | **NVS に保存**（UI モード・再起動後も残る） |
| `midi_in` 変更時の入力の追従 | **選択に追従する `UI::MIDIDevices.input` を gem 側で提供**（実体は [MIDI_FILTER.md](MIDI_FILTER.md) の宛先のない filter） |

## 基本方針

- **デバイスはトランスポート登録表の 1 ビット**（[midi_transport.h](../mrbgems/picoruby-midi/include/midi_transport.h)）。
  同じトランスポートの `MIDI::Device` を 2 回登録した場合は 1 台として扱い、名前だけ上書きする
- **画面に出すパッチは C のルート表そのもの**（[midi_route.h](../mrbgems/picoruby-midi/include/midi_route.h)）。
  `patch` は `MIDI.route` の別名。スクリプトや `UI::Keys` が `MIDI.route` で張ったルートも、
  両端が登録済みなら画面に出る。モデル（`ui_midi_devices.cpp`）が持つのは、ルート表が知らないこと
  だけ：どのビットを一覧に出すか、表示名、内蔵デバイスの NVS 用キー、Prog の選択、保存済みパッチ
- 向きは Midori から見た向き:
  - **MIDI IN** … デバイスから Midori に入ってくる側 = ルートの送信元（`MIDI_route_sources()` に含まれるもの）
  - **MIDI OUT** … Midori からデバイスへ送る側 = 登録表の任意のトランスポート
  - 送信元になれないトランスポート（AMY）は OUT のみ

## Ruby API

```ruby
md = UI::MIDIDevices

# 内蔵デバイスは自動登録済み。register は名前・色の上書きか、追加のトランスポート用
j6 = md.register(MIDI::Device.new(MIDIDevices.usb_midi_host), name: "J-6", color: :green)
md.unregister(dev)               # そのデバイスから / へのパッチ（と filter）も外す

md.patch(j6, amy)                # = MIDI.route(j6, amy)
md.patch(j6, sam, channel: 9)
md.patch(pc, sam, channel: :clock)  # クロックだけ
md.patch(j6, amy, channel: [0, 9, :clock])  # チャンネル 1・10 とクロック（ルート 1 件）
md.unpatch(j6, sam)              # = MIDI.unroute
md.patches   # => [{from:, to:, channel: nil, active: true, filtered: false}, ...]（channel は 0..15 / nil = 全部 / :clock / それらの Array）
md.save      # 今の内蔵デバイス間のパッチと選択を NVS に保存（スクリプトの初期状態にする）

md.midi_in  = j6                 # プログラムが使うデバイス（スクリプト限り。保存はしない）
md.midi_out = amy
md.midi_in                       # => MIDI::Device または nil（画面で変わるので毎回聞き直す）
md.midi_out                      # standby の AMY が選ばれていればここで起動する
md.input                         # 選択中の IN に追従する入力（後述）
md.output                        # 選択中の OUT に追従する MIDI::Device（後述）
md.devices                       # => 起動済みデバイスの MIDI::Device 配列
md.active?(j6)                   # => 接続済みか（transport の is_connected）

md.on_change do |e|              # UI.process 経由
  # e[:kind] = :select_in / :select_out / :patch / :unpatch / :connect / :disconnect
  # e[:device]（= e[:from]）/ e[:to] は MIDI::Device、e[:channel] は nil / 0..15 / :clock / それらの Array
end
md.show                          # = UI.show(:midi_devices) / UI.midi_devices
```

返る `MIDI::Device` は、このスクリプトで register したものか、内蔵デバイスの工場ブロック
（`register_builtin` のブロック、例: `MIDIDevices.usb_midi_host`）で作ったもの。どちらも無い
ビット（例: 画面の鍵盤）には `UI::MIDIDevices::BitTransport`（ビットだけを持つ最小のトランスポート）
の `MIDI::Device` を返す。

### プラグインでデバイスを足す（/sd/plugins / 2026-10-30）

プラグイン（`/sd/plugins/*.rb` / `*.mrb`。docs/MIDI_FILTER_REGISTRY.md §5）は**どのモードでも起動時に**
`setup_midi_devices` の中、内蔵デバイスの `register_builtin` の後・保存済みパッチを張り直す `boot` の前に読まれる
（`main_task_base.rb` の `load_plugins`）。そのためプラグインで `register_builtin` したデバイスは
**キーで保存済みパッチとつながる**（`register` で足したものはスクリプト限りの扱いで、パッチは保存されない）。
（2026-10-30 の途中までは `/sd/devices` だった。）

```ruby
# /sd/plugins/din2.rb: 2 本目の DIN MIDI
require 'uart_midi'
din2 = UART_MIDI.new(13, 14)
UI::MIDIDevices.register_builtin(:din2, "DIN 2", MIDI.route_bit(din2), color: :yellow) { din2 }
```

## 画面

クロスバー。行が IN（送信元になれるデバイス）、列が OUT（全デバイス）。最後の列が **Prog(input)**（プログラムの IN を選ぶ）、最後の行が **Prog(output)**（OUT を選ぶ）。
見出しが 1 行に収まらないとき（CrowPanel / CoreS3 の列幅など）は `Prog` と `(input)` / `(output)` の 2 行にする。

```
              J-6  SAM  AMY   PC │Prog(input)
● J-6          ─    ●    ●    ·  │ ◉
○ PC           ○    ·    ·    ─  │ ○
● Keys         ·    ·    ●    ·  │ ○
─────────────────────────────────┼────
  Prog(output)  ·   ◉    ·    ·  │
```

- 行見出しの丸: 緑 = 接続済み、暗い灰色 = 未接続。列見出しの下の線: 緑 / 灰色 / **黄 = standby**（AMY 起動前）
- パッチ: 送信元の色で塗った丸。**片端が未接続（または standby）なら灰色の輪**（要件 4 の表示）。
  チャンネル指定は左下に `1-4,10,clk` のように（3 つ以上続くチャンネルは範囲、クロックは `clk`。
  セルに収まらなければ末尾を `+` で切る）、Ruby の filter があれば右上に黄色の `f`
- **セルをタップ**: パッチの付け外し（新規は全チャンネル）。**長押し**: チャンネル選択
  （All / **Clock** / 1〜16 / Off / Done）。**Clock と 1〜16 は複数選べる**（タップで個別に入り切り。
  All を選んでいたら外れる。最後の 1 つを切るとパッチごと外れる）。All と Off は選んだら閉じる。
  Done かピッカーの外をタップで閉じる。変更はその場で反映・保存する。
  見出しの右の **Ch / Filters** タブで、名前付き filter のチェーンとパラメータも選べる
  （2026-10-24、[MIDI_FILTER_REGISTRY.md](MIDI_FILTER_REGISTRY.md) 6 章）。
  Clock はタイミングクロック・Start / Continue / Stop・ソングポジションだけを通す
  （`MIDI.route(from, to, channel: :clock)`）。複数選ぶとルート 1 件のチャンネル集合になる
  （`channel: [0, 9, :clock]`、C では `MIDI_route_add_selection()` / `MIDI_ROUTE_SEL_*`）。
  NVS では `from>to:clock` / `from>to:0+9+clock`（0 始まりのチャンネルを `+` でつなぐ。旧形式の
  `from>to:9` もそのまま読める）
- **Prog(input) 列**のタップで `midi_in`、**Prog(output) 行**で `midi_out` を選ぶ（もう一度タップで解除）。
  画面での選択は NVS に保存する（ユーザーの明示的な操作なので）
- **見出しを長押し**: デバイス詳細（キー・ビット・状態・USB Host の製品名と EP・USB Device の
  TinyUSB 診断・filter キューの溢れ件数）。タップで閉じる
- 自分自身へのセル（同じデバイスの IN → OUT）は `─`
- 変化の検出: 200ms ごとにモデルの serial・接続状態・ルート表・filter・選択のハッシュを取り、
  変わったら `ui_strip::repaint_content()` で描き直す（スクリプトが `MIDI.route` で張ったルートも出る）
- 画面インデックスは `UI_SCREEN_MIDI_INFO`（= 2）を流用。Ruby は `:midi_devices`
  （`UI::SCREEN_MIDI_DEVICES`、`UI.midi_devices`）。`:midi_info` / `UI.midi_info` も通るが、
  `UI.screen` などが返す名前は `:midi_devices` になった

## 要件 4: 非活性デバイス

設計段階では C に接続マスクを持たせてルーティングを止める案だったが、不要だった：

- 未接続の送信元からはパケットが来ない
- 未接続の宛先への `MIDI_transport_send()` は各ドライバで捨てられる（USB Host は未接続、UART は
  未初期化なら何もしない）。standby の AMY は登録表にビットが無いのでルート自体が張られない

ルート表は触らないので、再接続すればそのまま有効に戻る。接続状態は UI タスク
（`ui_mididev_tick()`、250ms ごと）が見て、変化したら `:connect` / `:disconnect` を発行する。
`midi_out` は未接続でも nil にしない（抜き差しでスクリプトの分岐が変わらないように）。

## 選択に追従する出力（UI::MIDIDevices.output）

`midi_out` が返すのは**その時点で選ばれている機器**の `MIDI::Device` で、変数に入れたり
プレイヤに渡したりすると、あとで画面の Prog(output) 行を選び直しても古い機器へ送り続ける
（2026-10-23 に bach_air.rb で発覚）。保持するなら `output` を使う。

```ruby
out = UI::MIDIDevices.output         # 何度呼んでも同じオブジェクト
player = MIDI::MML::CombinedPlayer.new(out)
UI.pad(1) { out.trigger(36) }
xy = UI::XYPad.new(device: out)      # XYPad は Ruby から送るのでこれで追従する
```

- 中身は `OutputTransport`（Ruby）を包んだ `MIDI::Device`。送るたびにモデルの serial を見て、
  変わっていれば `midi_out` を引き直す。`transport_id` も今の OUT のビットを返すので、
  `trigger` / `trigger_batch`（C のスケジューラにビットで渡る）も今の OUT へ行く。
  OUT が未選択の間は何も送らない（`transport_id` は 0）
- OUT が替わったら、**前の OUT に全チャンネルの All Notes Off** を送る（前の OUT で鳴らした
  ノートの note off が新しい OUT へ行って、鳴りっぱなしになるのを防ぐ）。何も送らない間の
  切り替えも `input` と同じフック（`MIDI.run_filters` / `UI.process`）で拾う
- **C から送る部品には効かない**: `UI::Keys` / `UI::Tombola` の `device:` と `MIDI.route` は、
  渡された時点のビットを C に覚える。これらは `on_change` の `:select_out` で `midi_out` を
  渡し直す（examples/keys.rb / tombola.rb）。`MIDI.start!(output:)` のクロックのルートも同じで、
  examples/bach_air.rb は `MIDI::Clock` のルートを自分で張り直している

## 選択に追従する入力（UI::MIDIDevices.input）

```ruby
inp = UI::MIDIDevices.input          # 何度呼んでも同じオブジェクト
h = inp.on(:note_on) { |m| UI::MIDIDevices.midi_out&.note_on(m.note + 12, m.velocity) }
inp.on(:any) { |m| puts m.device }  # m は MIDI::Message（MIDI.on と同じ）
inp.off(h)
inp.device                           # 今聞いているデバイス
inp.external_bpm                     # その外部クロックの BPM
MIDI.bpm_loop(120, sync_from: inp) { |tick| ... }
```

- ハンドラを持つ小さな Ruby オブジェクト。選択中の IN に対して `MIDI.on` を張り、選択が変わったら
  旧受信元の `MIDI.off` → 新受信元の `MIDI.on` で付け替える
- **付け替えは Ruby 側**。`MIDI.run_filters` の先頭のフック（`$__midi_filter_hooks__`）で
  モデルの serial を見て、変わっていれば追従する。`MIDI.sleep_ms` / クロックループ / `UI.process`
  のどれかを回していれば数十 ms で切り替わる（設計段階では UI タスクが C で直接付け替える案だった。
  `MIDI.on` の listener と同じ C のスロットを共有するので、Ruby の filter 表と食い違わないよう Ruby に寄せた）
- 選択が送信元でない（登録解除された等）ときは「選択なし」として扱う（例外を出さない）

## 内蔵デバイスの自動登録

**UI / スクリプト / irb のどのモードでも起動時に** `setup_midi_devices` が登録し、続けて保存済み
パッチを張り直す（`UI::MIDIDevices.boot`）。スーパーバイザはスクリプトの切り替えのたびに
`picoruby_esp32_midi_cleanup()` でルート表を消すので、各モードの起動時が張り直す場所になる。

| キー（NVS 用） | デバイス | IN | OUT | ビット | 備考 |
|------|---------|----|-----|-----|------|
| `usb_host` | USB-MIDI Host | ○ | ○ | 0x01 | 表示名は接続中デバイスの製品名、未接続時 "USB Host" |
| `uart` | SAM2695 | ○ | ○ | 0x02 | |
| `usb_dev` | USB-MIDI Device | ○ | ○ | 0x04 | 表示名 "PC" |
| `amy` | AMY | — | ○ | 起動時に決まる | standby で登録 |
| `keys` | UI::Keys | ○ | ○ | 起動時に決まる | 鍵盤のモデルが作られたとき C 側（`ui_keys.cpp`）が自分で登録 |
| `tempo` | UI::Tempo | ○ | — | 起動時に決まる | MIDI クロックを送る（docs/TEMPO.md）。UI の初期化時に C 側（`ui_tempo.cpp`）が登録。OUT の列は出さない（`UI_MIDIDEV_NO_OUT`） |

- USB Host / UART / USB Device は起動時に工場ブロックを呼んで初期化する（スクリプトモードでも。
  以前は初回アクセスまで遅延していた）。UART は初期化しないと接続扱いにならず受信もしないため
- **AMY は standby**（ビット 0）で登録する。`AMY::Transport.instance` はオーディオエンジンを起動する
  （I2S・ディレイライン・Tab5 の USB パッドの付け替え）ので、保存済みパッチ・Prog の選択・画面での
  パッチで必要になったときに初めて起動する（C が「wanted」に印を付け、Ruby が `UI::MIDIDevices._poll`
  で拾って工場ブロックを呼ぶ）。スクリプトが `MIDIDevices.amy` で起動した場合も `init_amy` が
  `UI._mididev_activate` を呼ぶ。起動すると、**AMY が関わる保存済みパッチだけ**を張る
- 内蔵デバイスのエントリはスクリプトをまたいで残る（スクリプトが付けた名前は終了時に既定名へ戻る）。
  スクリプトが `register` したデバイスは終了時に消える

## パッチの NVS 保存

登録表の動的ビット（AMY・Keys）は登録順で決まり、起動ごとに変わり得るので、NVS には
**キー文字列**で保存する。

- 名前空間 `midi_devs`
- `patches` … `"<from>><to>[:<ch>]"` を `,` で連結（例: `usb_host>amy,usb_host>uart:9,keys>amy`）、最大 16 件
- `in` / `out` … Prog の選択（キー。未選択は削除）
- `defaults` … 既定パッチをどの版まで足したか（u8、2026-10-25）

既定パッチ（`ui_midi_devices.cpp` の `DEFAULT_PATCHES`）: 起動して初めて NVS を読んだとき、`defaults` が
`DEFAULTS_VERSION` より小さければ、まだ保存済みパッチに無い既定パッチを足して保存し、`defaults` を上げる。
**1 回だけ**なので、ユーザーが画面で外したものは次の起動で戻らない。既定パッチを増やすときは表に足して
`DEFAULTS_VERSION` を上げる（足した分だけがすでに使っている機体にも入る）。

| 版 | パッチ | 理由 |
|---|---|---|
| 1 | `tempo>seq:clock`（Tempo → Sequencer、Clock のみ） | スクリプトなしで Sequencer が Tempo の Play / テンポで動くように（docs/SEQUENCER.md） |

タイミング:

- **画面での変更は即保存**（タップは頻繁ではないので遅延書き込みはしていない）。両端が内蔵デバイスの
  パッチだけが保存対象
- 起動時に張り直す。片端のデバイスがこのボードに無い行は読み飛ばす（保存内容は消さない）
- **スクリプトが `patch` / `MIDI.route` で張ったものはスクリプト限り**。終了時にルート表ごと消え、
  次のモードの起動時に NVS の内容で張り直される。残したいときは `md.save`
- `md.midi_in=` / `md.midi_out=` もスクリプト限り（終了時に NVS の選択へ戻る）
- スクリプトが `register` したデバイスが絡むパッチ・選択は保存しない

## C 側の構成

| 場所 | 内容 |
|------|------|
| picoruby-midi `midi_route.h` | `MIDI_route_get()`（ルート表の走査）、filter（[MIDI_FILTER.md](MIDI_FILTER.md)） |
| picoruby-midi `midi.h` / port | `MIDI_route_start_source(bit)`: UART の入力開始と入力タスクの起動（Ruby の `MIDI._start_source` の C 版）。画面から張ったルートがスクリプトなしでも効くように |
| `main/ui/ui_midi_devices.cpp` | デバイス表（最大 8 件、spinlock）、選択、保存済みパッチ、NVS、接続ポーリング（`ui_mididev_tick`）、終了処理（`ui_mididev_cleanup`、`UIManager::scriptCleanup` から） |
| `main/ui/screen_midi_devices.cpp` | マトリクスの描画とタッチ。セルのタップで `ui_mididev_toggle_patch()` → `MIDI_route_add` / `MIDI_route_remove` を UI タスクから直接 |
| picoruby-ui `ports/esp32/ui.c` / `src/mruby/ui.c` | `UI._mididev_*` と `:midi_devices` イベント（mruby/c 版のバインディングは picoruby-ui 自体が追従していないので未対応） |
| `main/platform/platform_freenove.c` | 画面の無い Freenove 用の no-op スタブ（一覧は常に空） |

## 制約

- 登録表は 8 ビット（USB Host / UART / USB Device / AMY / Keys で既に 5）。**同時に登録できるのは実質 8 台**
- ルート表は 8 件（送信元 × チャンネル集合ごとに 1 件、宛先はマスクで束ねる）
- 1 つのトランスポートを複数ケーブル・複数チャンネルに分けて別デバイスとして見せることはできない
- `UI::MIDIDevices.input` の受信元は Prog(input) の選択 1 つ。スクリプトが自分で張った `MIDI.on(from, ...)` は
  固定の受信元を読み、選択には追従しない
- Freenove（画面なし）では一覧が空で、`midi_in` / `midi_out` は常に nil
