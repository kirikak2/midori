# MIDI Devices 画面（UI::MIDIDevices）

> **状態: 実装済み（2026-10-22）**。Tab5 / CoreS3 / Freenove のビルドと、ホスト上の Ruby テスト
> （`mrbgems/picoruby-ui/test/midi_devices_test.rb`）のみ。**実機では未確認**。

MIDI デバイスを列挙し、デバイス間の IN → OUT をパッチングする画面と、その Ruby API。
旧 MIDI Info 画面を置き換えた（USB デバイス診断はデバイス詳細へ移した）。

- 画面: [main/ui/screen_midi_devices.cpp](../main/ui/screen_midi_devices.cpp)
- モデル（デバイス一覧・Script・NVS）: [main/ui/ui_midi_devices.cpp](../main/ui/ui_midi_devices.cpp)
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
   `UI::MIDIDevices.midi_out` で取り出せる → 2026-11-01 から**スクリプト自身を `Script` という
   デバイス**にし、`midi_in` / `midi_out` はその仮想デバイス（下の「Script デバイス」）

## 決定事項

| 項目 | 決定 |
|------|------|
| 内蔵デバイスの登録 | **起動時に自動登録**（UI モードでもパッチ盤として使えるように） |
| パッチの保存 | **NVS に保存**（UI モード・再起動後も残る） |
| スクリプトの IN / OUT | **`Script` デバイス**（picoruby-midi の仮想ポート、1 ビット）。2026-11-01 まで Prog(input) 列 / Prog(output) 行で 1 台ずつ選んでいた |

## 基本方針

- **デバイスはトランスポート登録表の 1 ビット**（[midi_transport.h](../mrbgems/picoruby-midi/include/midi_transport.h)）。
  同じトランスポートの `MIDI::Device` を 2 回登録した場合は 1 台として扱い、名前だけ上書きする
- **画面に出すパッチは C のルート表そのもの**（[midi_route.h](../mrbgems/picoruby-midi/include/midi_route.h)）。
  `patch` は `MIDI.route` の別名。スクリプトや `UI::Keys` が `MIDI.route` で張ったルートも、
  両端が登録済みなら画面に出る。モデル（`ui_midi_devices.cpp`）が持つのは、ルート表が知らないこと
  だけ：どのビットを一覧に出すか、表示名、内蔵デバイスの NVS 用キー、保存済みパッチ
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
md.save      # 今の内蔵デバイス間のパッチを NVS に保存（スクリプトの初期状態にする）

md.midi_in  = j6                 # j6 → Script に張り替える（スクリプト限り。nil で全部外す）
md.midi_out = amy                # Script → amy に張り替える
md.midi_in                       # => Script（何かが Script へパッチされていれば）/ nil
md.midi_out                      # => Script（Script から何かへパッチされていれば）/ nil
md.input                         # Script へ届くもの（後述）
md.output                        # Script の MIDI::Device。nil にならない（後述）
md.script_bit                    # Script のビット（= MIDI._route_virtual。無いボードでは 0）
md.devices                       # => 起動済みデバイスの MIDI::Device 配列
md.active?(j6)                   # => 接続済みか（transport の is_connected）

md.on_change do |e|              # UI.process 経由
  # e[:kind] = :patch / :unpatch / :connect / :disconnect（:select_in / :select_out は出なくなった）
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

クロスバー。行が IN（送信元になれるデバイス）、列が OUT（全デバイス）。**スクリプト自身も `Script`
というデバイス**で、ほかと同じく行と列を 1 つずつ持つ（見出しはシアン、一覧の先頭）。Script の行が
スクリプトの出力の行き先、Script の列がスクリプトの入力の出どころ。

```
            Script J-6  SAM  AMY   PC
● Script      ─     ·    ●    ●    ·      ← midi_out は SAM と AMY へ
● J-6         ●     ─    ·    ●    ·      ← J-6 は Script（midi_in）と AMY へ
○ PC          ·     ○    ·    ·    ─
● Keys        ·     ·    ·    ●    ·
```

- 行見出しの丸: 緑 = 接続済み、暗い灰色 = 未接続。列見出しの下の線: 緑 / 灰色 / **黄 = standby**（AMY 起動前）
- パッチ: 送信元の色で塗った丸。**片端が未接続（または standby）なら灰色の輪**（要件 4 の表示）。
  チャンネル指定は左下に `1-4,10,clk` のように（3 つ以上続くチャンネルは範囲、クロックは `clk`。
  セルに収まらなければ末尾を `+` で切る）、Ruby の filter があれば右上に黄色の `f`
- **パッチを外すと宛先の音を止める**（2026-11-01）: そのパッチでノートを通したチャンネルにだけ、宛先へ
  Sustain Off（CC64 = 0）と All Notes Off（CC123）を送る（picoruby-midi の `MIDI_route_remove()`。
  鳴っている音のノートオフは、外したあとでは届かないため）。チャンネルを選び直したとき（外して張り直す）も同じ
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
- Script → Script のセルは自分自身なので `─`（張れない）
- **見出しを長押し**: デバイス詳細（キー・ビット・状態・USB Host の製品名と EP・USB Device の
  TinyUSB 診断・filter キューの溢れ件数）。タップで閉じる
- 自分自身へのセル（同じデバイスの IN → OUT）は `─`

### スクロールと表示フィルタ（2026-11-01）

デバイスが最大 16 台になったので、全部は 1 画面に収まらない。

- **スクロール**: セルには最小サイズがある（Tab5 / CrowPanel 84×52、CoreS3 34×22）。全部が入る
  ときは従来どおり最大サイズまで広げ、入らないときは入るだけの行・列を並べて残りをスクロールする。
  グリッドの上で**ドラッグ**すると指に付いてセル単位で動く（オフセットが変わったときだけ描き直す）。
  行見出し・列見出しは**固定**で、常に見えている。はみ出している向きには、
  右端 / 下端のマージンにスクロールバー（灰色のつまみ）が出る
- **表示フィルタ**: 左上の角（行見出しの上、以前は空き）のボタンをタップするたびに
  **All → Patched → Unpatched** と切り替わる（All 以外はシアン地）
  - Patched: パッチ（ルート）が 1 本でも出ている IN の行と、1 本でも入っている OUT の列だけ
  - Unpatched: パッチが 1 本も出ていない IN の行と、1 本も入っていない OUT の列だけ
    （何にもつながっていない機器を探して、それ同士をつなぐ用。すでに使っている OUT へ
    足すときは All に戻す）
  - 判定は行・列（デバイス）単位。セル単位で絞るとグリッドにならないため
  - **画面でパッチを付け外しした行・列は、フィルタを選び直すまで残る**（Unpatched でつないだ途端に
    行が消えて隣のセルを押してしまう、などを防ぐ）。画面に入り直すか、デバイスの数が変わったときも解除
  - 何も残らないときはグリッドの下に `No patches` / `Nothing unpatched`
  - 選んだ表示は画面を出ても保持する（NVS には保存しない）
- 各セルのパッチ選択は、モデルが変わったとき（200ms ごとのハッシュ比較・タップ）に 1 回だけ
  `ui_mididev_patch_selection()` で引いてヒープの Grid に持つ（描画は帯ごとに何度も呼ばれるため）
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

## Script デバイス（midi_in / midi_out / input / output、2026-11-01）

スクリプト自身を **`Script`** というデバイスにした（キー `script`、UI の初期化時に C の
`ui_mididev_script_init()` が一覧の先頭に登録）。2026-11-01 までは Prog(input) 列 / Prog(output) 行で
スクリプトの IN / OUT を 1 台ずつ選んでいたが、ほかの機器と同じくパッチで表すようにした。

**ビットは 1 つ**（IN / OUT を分けない）。向きで意味が変わる:

| 操作 | 意味 |
|---|---|
| Script **へ送る**（`midi_out` / `output` への送信、`device: md.output` にした `UI::Keys` などの C からの送信、`trigger`） | **Script からの出力**。Script から張ったルートへ流れる（`MIDI_route_packet(script, ...)`） |
| 他の機器から Script **へのルート** | **スクリプトの入力**。ルート表はトランスポートへ送らず、Ruby のキューへ積む（`midi_in` / `input` / `MIDI.on(script)`） |
| Script → Script | 張れない（送信元と宛先が同じルートは C が拒む） |

仕組みは picoruby-midi の**仮想ポート**（[midi_route.h](../mrbgems/picoruby-midi/include/midi_route.h)、
`MIDI_route_set_virtual()`）。登録表のビットを 1 つ仮想ポートに指定すると、`MIDI_route_dispatch()` は
宛先に入ったそのビットを `MIDI_transport_send()` から外し、`MIDI_ROUTE_DST_VIRTUAL`（0x20000）を付けて
filter のキューへ積む（Ruby が聞いている CIN だけ。`MIDI_route_set_virtual_listen()`）。
Script のトランスポートの `send_packet` は `MIDI_route_packet()` を呼ぶだけ。
filter の付いたルートの宛先が Script のときも、Ruby は filter の結果を送らずに Script のリスナーへ渡す
（`MIDI._deliver`）。

```ruby
md = UI::MIDIDevices
out = md.output                      # Script の MIDI::Device。何度呼んでも同じ、nil にならない
player = MIDI::MML::CombinedPlayer.new(out)
UI.pad(1) { out.trigger(36) }        # スケジューラもビットで Script へ → パッチの先で鳴る
keys = UI::Keys.new(device: out)     # C から送る部品も、画面で Script を張り替えれば行き先が移る

inp = md.input                       # Script へパッチされた機器から届く（何台でも）
h = inp.on(:note_on) { |m| out.note_on(m.note + 12, m.velocity) }
inp.on(:any) { |m| puts m.source }   # どの機器から来たか
inp.off(h)
inp.external_bpm                     # Script へパッチされた機器のうち、最初にクロックが測れたもの
MIDI.on(md.script_bit, :control_change) { |m| ... }   # input と同じ

md.midi_out ||= MIDIDevices.amy      # Script から何もパッチされていなければ AMY へつなぐ
md.midi_in  ||= MIDIDevices.usb_midi_host
```

- `midi_out` / `midi_in` は、Script からの / Script へのパッチが**ある間だけ** Script を返し、なければ nil
  （以前の「未選択なら nil」に合わせた。`md.midi_out ||= dev` と `md.midi_out&.note_on` がそのまま動く）。
  保持するなら `output` / `input`
- `midi_out = dev` / `midi_in = dev` は Script からの / への**パッチを張り替える**（スクリプト限り。
  スクリプトが終わればルート表ごと消え、保存済みのパッチに戻る）。`nil` で全部外す
- 以前の「選択に追従する」仕掛け（`OutputTransport`、`:select_out` での `device:` の渡し直し、
  OUT を替えたときの All Notes Off）は不要になった。Script のビット自体は変わらないので、C に覚えさせても
  パッチの張り替えに追従する
- **`MIDI.start!` / `bpm_loop` の `output: md.output`**: クロックを Script へルートするとスクリプトの
  入力になってしまうので、`MIDI::Clock._attach` は仮想ポートを断り、ループが自分で F8 / Start / Stop を
  Script へ送る（＝パッチの先へ出ていく。Ruby の計時）
- **`UI::Sequencer.new(input: md.midi_in)` は意味が変わった**: Script → Sequencer のルート（スクリプトの
  出力をシーケンサへ）になる。外部の鍵盤を入力にするなら機器を直接渡す（examples/sequencer.rb）
- 同様に `MIDI.route(md.midi_in, x)` は「スクリプトの出力 → x」になる。機器どうしを直接つなぐ

## 内蔵デバイスの自動登録

**UI / スクリプト / irb のどのモードでも起動時に** `setup_midi_devices` が登録し、続けて保存済み
パッチを張り直す（`UI::MIDIDevices.boot`）。スーパーバイザはスクリプトの切り替えのたびに
`picoruby_esp32_midi_cleanup()` でルート表を消すので、各モードの起動時が張り直す場所になる。

| キー（NVS 用） | デバイス | IN | OUT | ビット | 備考 |
|------|---------|----|-----|-----|------|
| `script` | このスクリプト | ○ | ○ | 起動時に決まる | 仮想ポート（上の「Script デバイス」）。UI の初期化時に C 側（`ui_midi_devices.cpp`）が一覧の先頭に登録 |
| `u<VID><PID>` | USB-MIDI Host の機器 | ○ | ○ | 1 台目 0x01、2 台目以降は起動時に決まる | ハブ越しに最大 4 台、機器ごとに 1 行。つないだときに C 側（`ui_midi_devices.cpp` の `sync_usb_host`）が製品名で登録し、その機器の保存済みパッチを張る。同じ機器 2 台は後ろにスロット番号。旧 `usb_host` のパッチは最初につないだ機器に移る（2026-11-04） |
| `uart` | SAM2695 | ○ | ○ | 0x02 | |
| `usb_dev` | USB-MIDI Device | ○ | ○ | 0x04 | 表示名 "PC" |
| `amy` | AMY | — | ○ | 起動時に決まる | standby で登録 |
| `keys` | UI::Keys | ○ | ○ | 起動時に決まる | 鍵盤のモデルが作られたとき C 側（`ui_keys.cpp`）が自分で登録 |
| `tempo` | UI::Tempo | ○ | — | 起動時に決まる | MIDI クロックを送る（docs/TEMPO.md）。UI の初期化時に C 側（`ui_tempo.cpp`）が登録。OUT の列は出さない（`UI_MIDIDEV_NO_OUT`） |
| `tombola` | UI::Tombola | ○ | ○ | 起動時に決まる | 衝突のノートを送り、受けたノートをボールにする（docs/TOMBOLA.md）。UI の初期化時に C 側（`screen_tombola.cpp`）が登録 |
| `xypad` | UI::XYPad | ○ | — | 起動時に決まる | Ruby の内蔵ハンドラが鳴らすメッセージをルートへも流す（docs/XYPAD.md）。UI の初期化時に C 側（`ui_common.cpp`）が登録。送るだけ（`UI_MIDIDEV_NO_OUT`） |

- UART / USB Device は起動時に工場ブロックを呼んで初期化する（スクリプトモードでも。
  以前は初回アクセスまで遅延していた）。UART は初期化しないと接続扱いにならず受信もしないため
- **AMY は standby**（ビット 0）で登録する。`AMY::Transport.instance` はオーディオエンジンを起動する
  （I2S・ディレイライン・Tab5 の USB パッドの付け替え）ので、保存済みパッチ・画面での
  パッチで必要になったときに初めて起動する（C が「wanted」に印を付け、Ruby が `UI::MIDIDevices._poll`
  で拾って工場ブロックを呼ぶ）。スクリプトが `MIDIDevices.amy` で起動した場合も `init_amy` が
  `UI._mididev_activate` を呼ぶ。起動すると、**AMY が関わる保存済みパッチだけ**を張る
- 内蔵デバイスのエントリはスクリプトをまたいで残る（スクリプトが付けた名前は終了時に既定名へ戻る）。
  スクリプトが `register` したデバイスは終了時に消える

## パッチの NVS 保存

登録表の動的ビット（AMY・Keys）は登録順で決まり、起動ごとに変わり得るので、NVS には
**キー文字列**で保存する。

- 名前空間 `midi_devs`
- `patches` … `"<from>><to>[:<ch>]"` を `,` で連結（例: `usb_host>amy,usb_host>uart:9,keys>amy`）、最大 32 件
  （2026-11-01 まで 16 件）
- `in` / `out` … 以前の Prog の選択（キー）。2026-11-01 から使わない。残っていれば最初の読み込みで
  `<in>>script` / `script><out>` の保存済みパッチに置き換え（すでにあれば足さない）、キーは消す
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
- `md.midi_in=` / `md.midi_out=` も Script のパッチの張り替えなのでスクリプト限り
- スクリプトが `register` したデバイスが絡むパッチは保存しない

## C 側の構成

| 場所 | 内容 |
|------|------|
| picoruby-midi `midi_route.h` | `MIDI_route_get()`（ルート表の走査）、filter（[MIDI_FILTER.md](MIDI_FILTER.md)）、仮想ポート（Script） |
| picoruby-midi `midi.h` / port | `MIDI_route_start_source(bit)`: UART の入力開始と入力タスクの起動（Ruby の `MIDI._start_source` の C 版）。画面から張ったルートがスクリプトなしでも効くように |
| `main/ui/ui_midi_devices.cpp` | デバイス表（最大 16 件、spinlock。表と保存済みパッチは最初の読み込み時にヒープに確保）、Script のトランスポート、保存済みパッチ、NVS、接続ポーリング（`ui_mididev_tick`）、終了処理（`ui_mididev_cleanup`、`UIManager::scriptCleanup` から） |
| `main/ui/screen_midi_devices.cpp` | マトリクスの描画とタッチ。セルのタップで `ui_mididev_toggle_patch()` → `MIDI_route_add` / `MIDI_route_remove` を UI タスクから直接 |
| picoruby-ui `ports/esp32/ui.c` / `src/mruby/ui.c` | `UI._mididev_*` と `:midi_devices` イベント（mruby/c 版のバインディングは picoruby-ui 自体が追従していないので未対応） |
| `main/platform/platform_freenove.c` | 画面の無い Freenove 用の no-op スタブ（一覧は常に空） |

## 制約

- 登録表は 16 ビット（`midi_tmask_t` = `uint16_t`、2026-11-01 まで 8 ビット）。USB Host / UART / USB Device /
  Script / Tempo（クロック）/ AMY / Keys / Sequencer / Tombola / XYPad で既に 10。**同時に登録できるのは 16 台**（デバイス表も 16 件）
- ルート表は 32 件（送信元 × チャンネル集合ごとに 1 件、宛先はマスクで束ねる。2026-11-01 まで 8 件）
- 1 つのトランスポートを複数ケーブル・複数チャンネルに分けて別デバイスとして見せることはできない
- 仮想ポートは 1 つ（Script）。Script へのルートの内容は filter と同じキュー（64 件）を通るので、
  スクリプトが `UI.process` / `MIDI.sleep_ms` を回さないと溢れる（溢れた分は捨てる）
- Freenove（画面なし）では一覧が空で Script も無く、`midi_in` / `midi_out` は常に nil、`output` はどこにも送らない
