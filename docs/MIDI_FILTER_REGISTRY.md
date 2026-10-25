# 名前付き MIDI filter（MIDI::Filter / register_filter / /sd/filters）

> **状態: 実装済み（2026-10-24）**。Ruby はホストのテスト（picoruby-midi `host_test/filter_class_test.rb`、
> picoruby-ui `test/midi_filters_test.rb` / `test/layout_test.rb`）、C は Tab5 のビルドのみ。**実機では未確認**。
> 設計からの差分は 11 章

[MIDI_FILTER.md](MIDI_FILTER.md) の filter（`MIDI.route` に差し込む Ruby のブロック）を、
**名前の付いた再利用できる部品**にする。スクリプトを書かなくても、MIDI Devices 画面でパッチに
filter を付け外しでき、パラメータも画面で変えられるようにする。

- 関連: [MIDI_FILTER.md](MIDI_FILTER.md)（土台。C での絞り込み・キュー・実行タイミング）/
  [UI_MIDI_DEVICES.md](UI_MIDI_DEVICES.md)（パッチ画面と NVS）/
  [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md)（filter の操作パネル）/
  [PICORUBY_SUPERVISOR.md](PICORUBY_SUPERVISOR.md)（モードごとに VM が作り直されること）
- 実装: picoruby-midi [midi_filter_class.rb](../mrbgems/picoruby-midi/mrblib/midi_filter_class.rb) /
  picoruby-ui [ui_midi_filters.rb](../mrbgems/picoruby-ui/mrblib/ui_midi_filters.rb) /
  [main/ui/ui_filters.cpp](../main/ui/ui_filters.cpp) / [main/ui/screen_midi_devices.cpp](../main/ui/screen_midi_devices.cpp)
- サンプル: [examples/filters/](../examples/filters/) の `chord.rb` / `arp.rb` / `channel.rb` / `lumi.rb`（+ `lib/lumi_sysex.rb`。
  `/sd/filters/` にコピーして使う）

## 1. 要件

1. filter は **`MIDI::Filter` かその派生クラスのインスタンス**
2. `register_filter` / `unregister_filter` で自由に登録・解除できる。登録時に**名前**を付けて識別する
3. `/sd/filters/` 以下に filter を置いておくと、**MIDoRI が起動時に読み込む**
4. MIDI Devices 画面のパッチの長押しで、適用する filter を選択・解除できる。
   **複数選ぶと選んだ順にチェーン**になる
5. filter が `MIDI::Filter` の仕組みで**パラメータを宣言**すると、filter を選んだときの設定項目になる。
   また filter の初期化時に **UI レイアウト**（pad / knob）を定義して、演奏中に動かすパラメータを作れる
6. 用途の例: コードハーモナイザ、アルペジエータ。`examples/midi_to_lumi.rb` も filter として書き直せる

## 2. 決定事項

| 項目 | 案 | 理由 |
|------|----|------|
| 登録するもの | **インスタンス**（プロトタイプ）。パッチに付けるときは `instantiate` で**パッチごとに新しいインスタンス**を作る | アルペジエータのように状態を持つ filter を 2 本のパッチで共有すると混ざる。パラメータもパッチごとに違う。登録するインスタンスのパラメータが既定値になるので、`Arp.new(rate: 16)` を `:arp16` として登録すれば「プリセット違い」も作れる |
| 名前 | Symbol（`[a-z0-9_]`、15 文字まで）。画面に出す名前は別に `label` | NVS のキーになるので安定した短い識別子にする |
| 同じ名前で再登録 | 置き換える。付いているパッチは新しいプロトタイプで作り直し、パラメータ値は名前と範囲が合うものだけ引き継ぐ | `/sd/filters` を直して再読み込みしたときに付け直さずに済む |
| チェーン | パッチ 1 本につき 1 本の `MIDI::FilterChain`。C の filter 表では 1 件 | C の表（16 件）を filter の数だけ消費しない |
| パッチの選択の持ち主 | **C のモデル**（`ui_midi_devices.cpp`）。Ruby はそれを読んでチェーンを組む | 画面は UI タスクで動き、filter の実体は VM の中にある。AMY の standby と同じ「C が印を付け、Ruby が `_poll` で拾う」形にする |
| 保存 | 両端が内蔵デバイスのパッチのチェーン（filter 名と各パラメータ値）を NVS に保存 | パッチの保存規則（UI_MIDI_DEVICES.md）に合わせる |
| 読み込み | **UI / スクリプト / irb のどのモードでも**起動時に `/sd/filters/*.rb` を読む | VM はモードごとに作り直される。保存済みチェーンはスクリプト実行中も効いてほしい |
| UI | filter のレイアウトは**ルーティングで使われているアクティブな filter のインスタンスだけ**が作り、使われなくなったら**作成済みでも削除する**（4.2） | 登録しただけの filter や外れた filter のタイルで Home が埋まらないように |
| `/sd/filters` の形式 | **`.rb` だけ**を毎回 `load` する（2026-10-24 決定）。`.mrb` は読み込みが遅いと分かってから検討 | まず単純な方で進める |
| 1 つのインスタンスを 2 本のパッチで共有 | **非対応**（2026-10-24 決定）。同じ filter を 2 本に付けると別々のインスタンスになる。1 つの状態で 2 台を鳴らしたいときは、宛先を増やす filter（`call` / `emit` の中で別の `MIDI::Device` にも送る）を書く | `emit` の先が 1 つに決まる方が単純 |
| SysEx | **filter の対象外**（2026-10-24 決定）。常に C から直接転送する（MIDI_FILTER.md と同じ）。filter が SysEx を送るのは `output.send_sysex` で直接 | 入力タスクで SysEx を組み立ててからキューに積む仕組みが要るため |
| レイアウトの数 | **上限を外す**（メモリが許す限り。4.3） | filter のパネルとスクリプトのレイアウトが枠を取り合わないように |
| Home | **今の縦ドラッグのスクロールのまま**（4.4） | 行スクロールでタイルが何枚でもたどれる |

## 3. Ruby API

### 3.1 MIDI::Filter

```ruby
class Chord < MIDI::Filter
  label "Chord"                          # 画面の名前（省略時は登録名）
  types :note_on, :note_off              # Ruby に回す種類（省略時は全チャンネルメッセージ）

  CHORDS = { major: [0, 4, 7], minor: [0, 3, 7], sus4: [0, 5, 7], seventh: [0, 4, 7, 10] }

  param :chord,  :enum, choices: [:major, :minor, :sus4, :seventh], default: :major
  param :octave, :int,  range: -2..2, default: 0, label: "Oct"

  def setup                              # パッチに付いたとき（params が入った後）
    @map = MIDI::NoteMap.new
  end

  def call(m)                            # 戻り値の約束は MIDI.filter と同じ
    @map.apply(m) do |x|
      base = x.note + 12 * params[:octave]
      CHORDS[params[:chord]].map { |i| x.with(note: base + i) }
    end
  end
end

MIDI.register_filter(:chord, Chord.new)
MIDI.register_filter(:minor7, Chord.new(chord: :seventh, octave: -1), label: "Min7")   # 既定値違いのプリセット
```

#### クラスに書く宣言

| 宣言 | 内容 |
|------|------|
| `label "Text"` | 画面に出す名前。CoreS3 では 5 文字前後で切れる |
| `types *syms` | `call` に渡す種類（[MIDI_FILTER.md](MIDI_FILTER.md) の `types:` と同じ）。合わないものは素通り |
| `channel n` | 0..15 で絞る（省略時は全チャンネル） |
| `param name, kind, ...` | パラメータ（3.2） |
| `clock :internal` / `clock :source` | `on_clock` を受ける（3.4） |

宣言は派生クラスに引き継がれる（`params` の宣言は親 → 子の順に連結）。

#### インスタンスのメソッド

| メソッド | 呼ぶ人 | 内容 |
|----------|--------|------|
| `initialize(**params)` | 利用者 | パラメータの初期値。**上書きしない**（初期化は `setup` に書く）。上書きするなら `super(**params)` を呼び、キーワードだけを受けること（`instantiate` が `self.class.new(**params)` で作るため） |
| `instantiate(params = nil)` | 仕組み | パッチ用の新しいインスタンス。既定は `self.class.new(**(self.params.merge(params)))` |
| `setup` | 仕組み → 派生 | パッチに付いたとき 1 回 |
| `call(m)` | 仕組み → 派生 | メッセージ 1 件。既定は `m` を返す（素通し） |
| `teardown` | 仕組み → 派生 | 外されるとき。鳴らしたノートはチェーンが止めるので、ここで送る必要はない（3.3） |
| `param_changed(name, value)` | 仕組み → 派生 | 画面・レイアウトでパラメータが変わったとき |
| `layout(ui)` | 仕組み → 派生 | 操作パネルを作る（4 章）。定義しなければパネルは無い |
| `on_clock(tick)` / `on_start` / `on_stop` | 仕組み → 派生 | `clock` を宣言したとき（3.4） |
| `params` | 派生 | `{ name => value }`。enum は Symbol、bool は true / false |
| `set_param(name, value)` | 派生 / レイアウト | 範囲に丸めて設定し、`param_changed` を呼び、画面のモデルにも反映する |
| `emit(msg_or_array)` | 派生 | **自分より後ろ**のチェーンへ流す（`call` の外から送るとき。アルペジエータなど） |
| `every(ms) { ... }` | 派生 | 周期処理。止めるための handle（`#cancel`）を返す。filter が外されたら全部止まる |
| `output` / `source` | 派生 | 宛先・送信元の `MIDI::Device`。SysEx などチェーンを通さず直接送りたいもの用 |
| `name` / `route` | 派生 | 登録名、付いているパッチ（`MIDI::Route`） |

#### 登録

```ruby
MIDI.register_filter(name, filter, label: nil)   # => filter。filter が MIDI::Filter でなければ ArgumentError
MIDI.unregister_filter(name)         # => true / false（無かった）
MIDI.filters                         # => { name => プロトタイプ }（登録順）
MIDI.registered_filter(name)         # => プロトタイプ or nil
```

- `label:` は画面に出す名前（省略時はクラスの `label`）。同じクラスをプリセット違いで複数登録するときは
  付けないと Filters タブに同じ名前が並ぶ（2026-10-24 に追加）
- `unregister_filter` は、その filter をチェーンから外し（3.3 のノート止めあり）、画面の一覧から消す。
  **NVS の保存内容は消さない**（`/sd/filters` のファイルを消しただけで設定まで失わないように）。
  画面では、保存済みだが登録の無い filter は灰色で出し、チェーンでは素通しとして扱う
- 登録は VM 限り。スクリプトが `register_filter` した filter は、スクリプトの終了とともに消える
  （次のモードは `/sd/filters` から読み直す）
- 登録できるのは 16 個まで（C の一覧の大きさ）。超えたら false

### 3.2 パラメータ

```ruby
param :rate,   :int,   range: 1..32, default: 8, label: "Rate", step: 1
param :mode,   :enum,  choices: [:up, :down, :updown, :random], default: :up
param :hold,   :bool,  default: false
param :root,   :note,  default: 60                 # 0..127。画面は "C4" のように出す
param :ch,     :channel, default: nil              # nil（そのまま）/ 0..15。画面は "-" / 1..16
```

| kind | 値 | C のモデルでの表現 |
|------|----|------------------|
| `:int` | Integer（`range:` に丸める） | int16 |
| `:enum` | Symbol（`choices:` の 1 つ。最大 8 個、各 6 文字程度まで表示） | 添字 |
| `:bool` | true / false | 0 / 1 |
| `:note` | 0..127 | そのまま |
| `:channel` | nil / 0..15 | -1 / 0..15 |

- 1 つの filter につき **8 個まで**（画面に収まる数）。C のモデルは値を整数で持つので、画面だけで編集できる
  （Ruby への問い合わせなしで +/− できる）
- `float` は持たない（必要なら `:int` を 100 倍などで持つ）

### 3.3 チェーン（MIDI::FilterChain）

```ruby
route = MIDI.route(kb, syn)
route.chain(:chord, [:arp, { rate: 16 }])   # 選んだ順。既存のチェーンは置き換え
route.chain                                  # => [#<Chord>, #<Arp>]（インスタンス）
route.chain([])                              # 外す
MIDI.chain(kb, syn, :chord, :arp)            # ルートが無ければ張る（MIDI.filter と同じ）

md.patch(j6, amy, filters: [:chord, :arp])   # UI::MIDIDevices から
md.patches  # => [{from:, to:, channel:, filters: [:chord, :arp], ...}]
```

- **順番**: メッセージは選んだ順に各 filter の `call` を通る。前の戻り値が次の入力
  （`Array` は要素ごと、空なら打ち切り）。filter の `types` / `channel` に合わないものはその filter を
  素通り。ここまでは既存の「同じルートの filter を付けた順につなぐ」と同じ
- `emit` は**呼んだ filter の次から**流す。「コード → アルペジエータ」の順なら、和音を分散和音にできる。
  逆順なら分散した 1 音ずつに和音が付く
- **C で絞る種類**はチェーン全体の `types` の和集合（`MIDI._filter_apply` の既存の計算をそのまま使う）。
  どの filter も欲しがらないもの（例: CC）は従来どおり C から直接届く
- チェーンは `$__midi_filters__` の (src, dst) のリストに**先頭**の 1 件として入る。スクリプトが
  `MIDI.filter` で付けたブロックはその後ろにつながる
- **鳴っているノートの後始末**: チェーンは出口（宛先へ送った分）の note on / note off を数えておき、
  チェーンを変えたとき・filter が外れたとき・再登録で作り直したときに、残っている音へ note off を送る。
  そのあと各 filter の `teardown`。filter ごとに `NoteMap.release_all` を書く必要をなくすため
- 例外は filter ごとに最初の 1 回だけ報告し、そのメッセージを捨てる（既存と同じ）。`every` / `on_clock`
  の例外も同様

### 3.4 時間で動く filter（emit / every / on_clock）

アルペジエータは入力が無いときも音を出す。そのための 2 つの方法:

| 方法 | 動き | 向いているもの |
|------|------|----------------|
| `every(ms) { }` | `MIDI.run_filters` の先頭（既存の `$__midi_filter_hooks__`）で期限を見て呼ぶ | テンポと関係ないアニメーション（LUMI のウェーブ） |
| `clock :internal` + `on_clock(tick)` | `MIDI::Clock`（Tempo 画面のテンポ）の F8 を `MIDI.on(MIDI::Clock, :clock)` で受ける。tick は 24ppq の通し番号 | アルペジエータ。Tempo 画面・外部同期に従う |
| `clock :source` + `on_clock(tick)` | パッチの送信元から来るクロックを使う（チェーンの types に `:clock` を足す） | 外部シーケンサに直接合わせたいとき |

- `on_start` / `on_stop` は Start / Stop。アルペジエータは Stop で鳴っている音を止める
- `MIDI::Clock` の受信者が複数のチェーンにいても、`MIDI.on(MIDI::Clock, :clock)` は 1 つだけ張って配る
- **待ちの上限**: `MIDI._idle_ms` は filter キューを待ちながら寝るので、`every` の期限が来ても
  メッセージが来なければ起きない。待ち時間を「次の `every` の期限まで」に縮める変更を入れる
- 精度は Ruby の VM 次第（数 ms のジッタ）。数 ms のずれが問題になるなら C のスケジューラ
  （`output.trigger(note, vel, duration:)`）で note off を予約する。ただし `trigger` はチェーンの
  後ろを通らないので、最後の filter でだけ使う

## 4. filter の操作パネル（UI レイアウト）

```ruby
class Arp < MIDI::Filter
  def layout(ui)
    ui.layout(cols: 4, rows: 2) do
      areas <<~GRID
        rate mode oct hold
        rate mode oct hold
      GRID
      param_knob :rate, area: :rate, color: :cyan        # 宣言済みパラメータに結び付ける
      param_knob :octaves, area: :oct
      param_pad  :mode, area: :mode                      # enum はタップで順送り
      param_pad  :hold, area: :hold, color: :orange      # bool はトグル
    end
  end
end
```

- `ui` は UI 側（picoruby-ui）が渡す小さなオブジェクト。`ui.layout(...)` は
  `UI.layout(タイトル, ...)` を呼ぶ。**タイトルは自動**（`"Arp J-6>AMY"`。同じ filter が 2 本のパッチに
  付いていれば 2 枚になる）
- ブロックの中は [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md) の Builder に 2 つ足したもの:
  - `param_knob name, area:/col:/row:, ...` … `:int` / `:note` をノブに。値の範囲・初期値・ラベルは
    宣言から取り、回すと `set_param`
  - `param_pad name, ...` … `:bool` はトグル、`:enum` はタップで次の値（ラベルに今の値を出す）
  - 普通の `pad` / `knob` も書ける。**宣言していない値（動的なパラメータ）**はブロックで filter の
    インスタンス変数を変えればよい（保存はされない）:
    `knob area: :k, label: "Swing" do |v| @swing = v end`
- `set_param` は C のモデルにも書くので、長押しの設定画面とパネルの値は常に一致する
- `UI.pad(n)` / `UI.knob(n)`（Pads / Knobs 画面の番号付きのもの）は**filter からは使わない**。
  スクリプトの pad と番号を取り合うため。レイアウトの部品だけを使う
- レイアウトを作るのは**アクティブな filter だけ**、使われなくなったら**消す**（4.2）
- パネルの操作は `UI.process` で届く（4.1）

### 4.2 レイアウトの寿命

**アクティブな filter** = ルート表に今あるルートのチェーンに入っている filter のインスタンス。
レイアウトはインスタンスに 1 枚で、その寿命はインスタンスがアクティブな間と一致させる。

- **作る**: インスタンスがアクティブになったとき（チェーンに追加された、そのルートが張られた、
  起動時に保存済みチェーンが張られた）に `layout(ui)` を呼ぶ。登録しただけの filter や、
  保存済みでもルートが張られていないチェーン（片端が standby の AMY など）はレイアウトを作らない
- **消す**: インスタンスがアクティブでなくなったら、作成済みのレイアウトを `Layout#delete` で消す
  （Home のタイルも消え、メモリも返す）。きっかけは次のどれでも同じ:
  - Filters タブ・`route.chain` でチェーンから外した
  - ルート自体が無くなった（セルのタップ・チャンネルの Off・`MIDI.unroute` / `unroute_all`・
    `md.unpatch`・デバイスの `unregister`）
  - `unregister_filter` した
  - 同じ名前で再登録した（古いインスタンスのレイアウトを消し、作り直したインスタンスで作り直す）
- **見つけ方**: Ruby の API（`route.chain` / `MIDI.unroute` など）で外したときはその場で消す。
  画面（UI タスク）がルートを外したときは Ruby が直接知らないので、`UI::MIDIDevices._poll` が
  serial の変化でルート表とチェーンを突き合わせ、ルートの無くなったチェーンを外す
  （このとき 3.3 のノートの後始末と `teardown` も走る）。消えるまでの遅れは `_poll` の間隔
  （`UI.process` / `MIDI.sleep_ms` を回していれば数十 ms）
- **表示中のレイアウトを消すとき**: C の `ui_layout_delete()` は枠を空けるだけで、表示中の画面は
  そのまま残る。filter のレイアウトを消すときは、それが表示中なら先に `UIManager::requestScreen()` で
  **MIDI Devices 画面へ戻す**（外す操作はふつうそこで行うので。スクリプトから外したときも同じ）。
  `ScreenLayout` が中身の無くなった枠を描かないことも実装時に確認する
- レイアウトの数に上限は無い（4.3）。メモリが足りずに作れなかったときだけ**パネルなし**で付け、
  `[Filter] arp: no memory for the layout` を出す（filter 自体は動く）
- スクリプトの終了時は、VM ごとレイアウトも `picoruby_ui_cleanup()` で消える。次のモードの起動時に、
  保存済みチェーンのうちアクティブなものだけがまた作る

### 4.3 レイアウトの数の上限を外す

今は `UI_LAYOUT_MAX_LAYOUTS` = 4（C の `s_layouts[4]`、Ruby の `Layout::MAX_LAYOUTS`）で、
5 枚目の `UI.layout` は ArgumentError になる。これを外し、**メモリが許す限り**作れるようにする。

| 場所 | 今 | 変更 |
|------|----|------|
| `main/ui/ui_layout.cpp` | 固定長配列 `s_layouts[UI_LAYOUT_MAX_LAYOUTS]` を添字（slot）で引く | **ID で引く可変長の表**（`LayoutDef*` の配列を足りなくなったら倍に realloc。PSRAM 可）。ID は commit のたびに増える 16 bit の通し番号（一周したら使用中の ID を飛ばす）。すぐには再利用しないので、消えたレイアウトの古いイベントや `UI.show` が別のレイアウトに当たらない |
| `ui_common.h` の C API（`ui_layout_*(int slot, ...)`） | 引数は slot 0..3 | 引数を layout ID に読み替える（型は int のまま） |
| `UIManager::buildMenu` / `MENU_MAX_ITEMS`（16） | 固定長の `MenuItem m_items[16]` と `m_status[16][]` | 画面数 + レイアウト数ぶんを必要なときに確保し直す（Home の `enter()` とレイアウトの増減時） |
| 画面の引数（`UI_SCREEN_LAYOUT` の arg）・Back の履歴 | slot | layout ID |
| Ruby `ui_layout.rb` | `MAX_LAYOUTS = 4`、`free_slot`、コールバックのキー `slot * KEY_STRIDE + id` | 上限と `free_slot` を削除。`commit` が C から ID を受け取り、キーは `layout_id * KEY_STRIDE + widget_id` |

- 1 枚あたり約 2.5KB（`LayoutDef`、部品 32 個）。PSRAM に置けば数百枚でも問題にならないので、
  固定の上限は設けない。確保に失敗したら `UI.layout` は例外（filter のパネルは 4.2 のとおり「パネルなし」）
- XYPad / Tombola / Keys / Tempo は画面のモデルが 1 つなので「1 レイアウトに 1 つまで」は変わらない。
  表示中だけ借りるので、複数のレイアウトがそれぞれ持つのは今までどおり可
- UI_LAYOUT_DSL.md の 9 章 D 系（最大 4）と UI_NAVIGATION.md の 3.1（「最大 4」）は実装時に書き換える

### 4.4 Home

Home は今の**縦のドラッグで 1 行ずつスクロール**する仕組みのまま、タイルが増えても全部たどれる。
変えるのはタイルの数の上限だけ（4.3 の `MENU_MAX_ITEMS` を外す）。

- レイアウトの増減でタイルが変わったら、Home を表示中なら描き直す（今の `poll()` で拾う）。
  スクロール位置がタイルの行数を超えたら最後の行に合わせる

## 5. /sd/filters からの読み込み

```
/sd/filters/
  chord.rb        # MIDI.register_filter(:chord, Chord.new) まで書く
  arp.rb
  lumi.rb
  lib/lumi_sysex.rb   # 自動では読まない。lumi.rb から load する
  _wip.rb             # "_" で始まるものは読まない
```

- `/sd/filters/*.rb` を**名前順**に `load` する（サブディレクトリと `_` で始まるファイルは読まない）
- 各ファイルは自分で `MIDI.register_filter` を呼ぶ（ファイル名から自動では登録しない。1 ファイルで
  プリセット違いを複数登録できるように）
- ファイルごとに `rescue` し、`[Filter] /sd/filters/arp.rb: <message>` を出して次へ進む
- 読み込む場所: `main_task_base.rb` の各モード（UI / Script / irb）で、SD のマウント後・
  `setup_midi_devices` の前（`md.boot` が保存済みチェーンを張るときに filter が揃っているように）。
  関数 `load_filters` として 3 か所から呼ぶ
- UI モードの SD 再読み込み要求（`sd_refresh_requested?`）のときも読み直す（同名は置き換え）
- 読み込みは VM の起動ごとに走る（スクリプトの切り替えのたびにコンパイルし直す）。数が増えて
  起動が遅くなったら `.mrb` を検討する（当面は `.rb` だけ）
- **トップレベルで重い処理をしない**。ファイルはクラスを定義して登録するだけにし、デバイスへの
  送信などは `setup` に書く（`output` が決まるのは付いたときなので）
- filter の無限ループは UI モードを止める（UI モードの VM は Supervisor の強制停止の対象ではない）。
  当面は注意書きに留める

## 6. MIDI Devices 画面

### 6.1 長押しメニュー

今のチャンネル選択（All / Clock / 1〜16 / Off / Done）に**タブ**を足す。

```
┌ J-6 > AMY ─────────────── [Channel] [Filters] ┐
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐            │
│ │Chord①│ │ Arp ②│ │Minor7│ │ Lumi │   ...      │
│ └──────┘ └──────┘ └──────┘ └──────┘            │
│                                   [◀ ▶] [Done] │
└────────────────────────────────────────────────┘
```

- **Filters タブ**: 登録済みの filter をボタンで並べる（大画面 7×2 / CoreS3 7×2、溢れたらページ送り）
  - タップ: 未選択なら**チェーンの末尾に追加**、選択中なら外す（後ろの番号は詰める）。
    選んだ順番をボタンの右上に ①② で出す
  - 選択中のボタンを**長押し**: そのパッチでのパラメータ設定（6.2）
  - チェーンの長さは 4 まで
  - パッチが無いセルで filter を選んだら、全チャンネルのパッチを張ってから付ける
  - 保存済みだが未登録の filter は灰色で出す（タップで外せる）
- Channel タブは今の画面そのまま。開いたときは前回のタブ
- セルの表示: 右上の `f` を、チェーンの長さ（`f2`）に変える。スクリプトのブロックの filter だけなら従来の `f`
- 変更はその場で反映・保存する（今のチャンネル選択と同じ）

### 6.2 パラメータ設定

```
┌ Arp  (J-6 > AMY) ──────────────────────────────┐
│ Rate    [−]   8   [+]                          │
│ Mode    [ up  ▸]                               │
│ Oct     [−]   2   [+]                          │
│ Hold    [ off ]                                │
│                                [Reset] [Done]  │
└────────────────────────────────────────────────┘
```

- `:int` / `:note` / `:channel` は −/+（長押しで連続）、`:enum` はタップで次、`:bool` はトグル
- 値は C のモデルで変え、Ruby が `_poll` で拾って `set_param` を呼ぶ（`param_changed` まで走る）
- `[Reset]` はプロトタイプの値に戻す
- CoreS3 は 1 画面に 4 行。5 個以上はスクロール

### 6.3 C のモデル（ui_midi_devices.cpp）

| 持つもの | 大きさ | 書く人 |
|----------|--------|--------|
| filter の一覧（名前・ラベル・パラメータの宣言） | 16 件 × パラメータ 8 個 | Ruby（`register_filter` / `unregister_filter` のたびに） |
| パッチごとのチェーン（filter 名の並び + パラメータ値） | パッチ 16 本 × 4 段 | 画面 / Ruby（スクリプトの `route.chain`） |
| 保存済みのチェーン | 同上（キー文字列） | NVS |

- 一覧の宣言はラベルや enum の選択肢の文字列を含めて 10KB 程度になるので、**初めて登録したときに
  malloc**（.bss に置かない。MEMORY_ALLOCATION.md）
- 画面の変更はモデルの serial を上げる。Ruby の `UI::MIDIDevices._poll` がそれを見て、
  チェーンの差分（追加 / 削除 / 並べ替え / パラメータ）を `route.chain` / `set_param` で反映する
- Ruby 側での変更（スクリプトの `route.chain`、レイアウトでの `set_param`）もモデルに書く。
  画面とスクリプトの両方が同じパッチのチェーンを変えたときは、後に書いた方が勝つ

### 6.4 NVS

名前空間 `midi_devs` に `chains` を足す（`patches` とは別のキー。`patches` の書式と互換を保つため）。

```
usb_host>amy=chord(chord:1,octave:-1),arp(rate:16,mode:0);uart>amy=lumi
```

- パラメータ値は C のモデルと同じ整数（enum は添字、bool は 0 / 1）。既定値と同じものも書く
  （filter の既定値を後で変えても、ユーザーが選んだ時点の値が残るように）。宣言から消えた名前の値は読み飛ばす
- 範囲外の値は丸める。enum の添字が choices を超えたら既定値
- 上限 1KB 程度。書き込みは画面での変更は即時、レイアウト（ノブ）での変更は**最後の変更から 3 秒後**に
  まとめて（ノブを回すたびに書かない）
- パッチの保存と同じく、両端が内蔵デバイスのものだけ。スクリプトの `route.chain` はスクリプト限り
  （`md.save` で保存）

## 7. 実装の置き場所

| 場所 | 足すもの |
|------|----------|
| picoruby-midi `mrblib/midi_filter_class.rb`（新規） | `MIDI::Filter`（宣言・パラメータ・`emit` / `every` / `on_clock`）、`MIDI::FilterChain`、`MIDI.register_filter` ほか、`MIDI::Route#chain`。UI は知らない。登録の変化は `$__midi_filter_registry_hooks__` で外へ知らせる |
| picoruby-midi `mrblib/midi_filter.rb` | `_idle_ms` の待ちを `every` の期限で切る |
| picoruby-ui `mrblib/ui_midi_devices.rb` | 登録のフックを受けて C の一覧へ写す、`_poll` でチェーンを反映、`patch(filters:)`、レイアウトの `ui` オブジェクトと `param_knob` / `param_pad` |
| picoruby-ui `ports/esp32/ui.c` | `UI._mididev_filter_*`（一覧・チェーン・パラメータ値の読み書き） |
| `main/ui/ui_midi_devices.cpp` | 一覧・チェーン・NVS `chains` |
| `main/ui/screen_midi_devices.cpp` | Filters タブ、パラメータ設定、セルの `f2` |
| `main_task_base.rb` | `load_filters`、UI モードのループ（4.1） |
| `main/ui/ui_layout.cpp` / `ui_manager.cpp` / `screen_home.cpp`、picoruby-ui `mrblib/ui_layout.rb` | レイアウトの可変長の表と ID（4.3）、メニューの項目数の上限を外す（4.4） |
| `examples/filters/` | サンプル（8 章） |

- **mruby 専用**: `MIDI::Filter` はキーワード引数の `**` とクラスの宣言（クラスメソッド）を使う。
  picoruby-midi は mruby/c のバインディングも保守しているが、このファイルは mruby/c のビルドから外す
  （mruby/c はクラスメソッドとインスタンスメソッドを区別しない）。ブロックの `MIDI.filter` は従来どおり両方で動く
- ホストテスト: `host_test/filter_class_test.rb`（宣言の継承、`instantiate`、チェーンの順番、`emit` が
  後ろだけを通ること、ノートの後始末、パラメータの丸め）と、picoruby-ui の `test/midi_devices_test.rb` に
  `_poll` での差分反映

## 8. サンプル

### 8.1 コードハーモナイザ（chord.rb）

3.1 の `Chord`。`NoteMap` で note off を和音全体に広げる。パラメータを途中で変えても、押さえている
音の note off は押したときの和音へ行く。

### 8.2 アルペジエータ（arp.rb）

```ruby
class Arp < MIDI::Filter
  label "Arp"
  types :note_on, :note_off
  clock :internal
  param :rate,    :enum, choices: [:"1/4", :"1/8", :"1/16", :"1/32"], default: :"1/16"
  param :mode,    :enum, choices: [:up, :down, :updown, :random], default: :up
  param :octaves, :int,  range: 1..4, default: 1, label: "Oct"
  param :hold,    :bool, default: false

  TICKS = { "1/4": 24, "1/8": 12, "1/16": 6, "1/32": 3 }

  def setup
    @held = []      # 押さえているノート（押した順）
    @step = 0
    @playing = nil  # 今鳴らしている Message
  end

  def call(m)
    if m.type == :note_on
      @held.delete(m.note)
      @held << m.note
      @velocity = m.velocity
      @channel = m.channel
    elsif !params[:hold]
      @held.delete(m.note)
    end
    nil             # 入力そのものは鳴らさない
  end

  def on_clock(tick)
    return unless tick % TICKS[params[:rate]] == 0
    emit(@playing.with(type: :note_off, velocity: 0)) if @playing
    @playing = nil
    seq = sequence
    return if seq.empty?
    @playing = MIDI::Message.new(:note_on, @channel, seq[@step % seq.size], @velocity)
    @step += 1
    emit(@playing)
  end

  def on_stop
    emit(@playing.with(type: :note_off, velocity: 0)) if @playing
    @playing = nil
    @step = 0
  end

  def sequence
    base = @held.sort
    notes = []
    params[:octaves].times { |o| base.each { |n| notes << n + 12 * o } }
    case params[:mode]
    when :down   then notes.reverse
    when :updown then notes + notes[1..-2].to_a.reverse
    when :random then notes.shuffle
    else notes
    end
  end

  def layout(ui)
    ui.layout(cols: 4, rows: 1) do
      param_pad :rate, col: 0
      param_pad :mode, col: 1
      param_knob :octaves, col: 2
      param_pad :hold, col: 3, color: :orange
    end
  end
end

MIDI.register_filter(:arp, Arp.new)
```

MIDI Devices で J-6 > AMY に `chord`, `arp` の順に付けると、1 音押すだけで和音の分散和音になる。

### 8.3 LUMI Keys（lumi.rb、midi_to_lumi.rb の書き直し）

今の `midi_to_lumi.rb` がしていること → filter での置き場所:

| スクリプト | filter |
|------------|--------|
| IN / OUT の選択と `md.input.on` で note を LUMI へ | パッチ「鍵盤 > LUMI」自体。ノートは C から直接届くので **`call` は不要**（`types` も宣言しない = 何も Ruby に回さない、で済むなら C の filter 表も使わない） |
| 起動時の SysEx（カラーモード・色・スケール・輝度） | `setup` で `output.send_sysex`。`param :scale` / `:brightness` / `:color_mode` にし、`param_changed` で送り直す |
| Pad 1 / 2 のウェーブ（`MIDI.sleep_ms(13)` で 49 音） | レイアウトの `pad` → `every(13)` で 1 ステップずつ `emit`（**ブロックの中で sleep しない**。filter は他のパッチと VM を共有するので、止まると全部が止まる） |
| Pad 3 の Ripple（`MIDI.start!` のクロック） | `clock :internal` + `on_clock` |
| `md.on_change` で OUT が替わったら設定し直す | 不要。OUT を変える = 別のパッチなので、新しいインスタンスの `setup` が走る |

```ruby
load "/sd/filters/lib/lumi_sysex.rb"   # LumiBitArray / Lumi（今のスクリプトから移す）

class LumiLights < MIDI::Filter
  label "LUMI"
  clock :internal
  param :scale, :enum, choices: Lumi::SCALE_CMDS.keys, default: :chromatic
  param :brightness, :int, range: 0..100, default: 80, label: "Bright"

  attr_writer :ripple

  def setup
    Lumi.set_color_mode(output, 1)
    Lumi.set_color(output, 0, 0, 150, 255)
    Lumi.set_color(output, 1, 255, 80, 0)
    Lumi.set_scale(output, params[:scale])
    Lumi.set_brightness(output, params[:brightness])
    @ripple = false
  end

  def param_changed(name, value)
    Lumi.set_scale(output, value) if name == :scale
    Lumi.set_brightness(output, value) if name == :brightness
  end

  def wave(from, to)
    note = from
    step = from < to ? 1 : -1
    @wave = every(13) do
      output.trigger(note, 110, duration: 100)
      note += step
      @wave.cancel if note == to + step
    end
  end

  def on_clock(tick)
    # midi_to_lumi.rb の Ripple と同じ（@ripple のときだけ ping-pong）
  end

  def layout(ui)
    f = self
    ui.layout(cols: 4, rows: 1) do
      pad(col: 0, label: "Wave Up", color: :cyan) { f.wave(48, 96) }
      pad(col: 1, label: "Wave Dn", color: :orange) { f.wave(96, 48) }
      pad(col: 2, label: "Ripple", color: :green, type: :toggle) { |on| f.ripple = on }
      param_knob :brightness, col: 3
    end
  end
end

MIDI.register_filter(:lumi, LumiLights.new)
```

`examples/midi_to_lumi.rb` はスクリプトとしても残す（filter 版は `examples/filters/lumi.rb`）。

### 8.4 チャンネル変換（channel.rb、2026-10-25 追加）

`ChannelMap`（登録名 `:chmap`、画面の名前 "Ch Map"）。受けたチャンネルメッセージのチャンネルを書き換える
（例: ch5 → ch1）。`types` は宣言しない（全チャンネルメッセージ。クロックなどは C から直接届く）。

| パラメータ | kind | 意味 |
|------------|------|------|
| `from1`〜`from3` / `to1`〜`to3` | `:channel` | 規則 3 本（In → Out）。In が "-" の規則は使わない。Out が "-" なら In のチャンネルを捨てる |
| `other` | `:enum` `[:thru, :drop]` | どの規則にも当たらないチャンネル。`:drop` で規則に書いたチャンネルだけを通す |

- 同じ In の規則が 2 本あれば両方へ送る（レイヤー）
- note on / off は `NoteMap` を通すので、押さえている間に規則を変えても note off は note on のチャンネルへ行く
- パネル（`layout`）は 6 列 × 4 行: 規則ごとに `[-][In][+][-][Out][+]`、最後の行に `param_pad :other` と Clear。
  値のパッドの id を `param_<name>` にして、パラメータ画面での変更も `_param_shown` でラベルに出る。
  `:channel` は `param_knob` だと -1..15 の数字で出るので、"-" / 1..16 で見えるパッドにした
- ホストでは CRuby / mruby とも確認（チャンネルの書き換え・レイヤー・drop・note off の追従・パネルの操作）。**実機未確認**

## 9. 未決事項

| 項目 | 選択肢 | 推し |
|------|--------|------|
| パラメータの MIDI Learn（CC で `set_param`） | 本設計では扱わない | 後で `param ..., cc: 74` を足せるようにしておく |

（`MIDI::Clock` だけを listener で受けたとき F8 が出るかは、実装時に確認して解決した。11 章）

## 10. 段階的な進め方

0. レイアウトの数の上限を外す（4.3、Home のメニュー項目数も）。filter と独立に入れられる
1. `MIDI::Filter` / `MIDI::FilterChain` / `register_filter` / `route.chain`（picoruby-midi、ホストテストまで）
2. `load_filters` と UI モードのループの変更。この時点で `/sd/filters` + スクリプトの `route.chain` で使える
3. C の一覧・チェーンと Filters タブ、NVS `chains`（ここで画面だけで付け外しできる）
4. パラメータ設定画面
5. `layout` / `param_knob` / `param_pad`、アクティブな間だけレイアウトを持つ寿命の管理（4.2）
6. `every` / `on_clock` とサンプル 3 本（arp と lumi はここから）

## 11. 実装メモ（設計からの差分・補足）

- **レイアウト ID は Ruby が振る**（4.3 では「C が返す」としていた）。`UI::Layout.next_id` が定義順の通し番号
  （0..0x7FFF、一周したら使用中を飛ばす）を渡し、C は ID で引く可変長の表を持つ。C の API の引数は int のまま
- **`MIDI::Clock` の listener で F8 は出る**: ESP32 の port の tick は `MIDI_route_from(clock_bit)` が真のときに
  F8 を配り、`MIDI_route_from()` は filter（listener 含む）も数える。`clock :internal` の filter が 1 つでもある間、
  `MIDI.filter(MIDI::Clock のビット, nil, types: [:clock, :start, :stop, :continue])` を 1 つだけ張って各チェーンへ配る。
  tick は Start で 0 に戻る通し番号（`$__midi_clock_tick__`）
- **`types []`**: 何も Ruby に回さない filter（LUMI のように `setup` / `every` / `on_clock` だけで動くもの）。
  チェーンの types が空なら C の filter 表を使わない
- **`emit` の行き先**: 後ろの filter を通ったあと宛先へ直接送る。同じルートにスクリプトが `MIDI.filter` で付けた
  ブロックは通らない（`call` の戻り値はそれらを通る）
- **チェーンの差し替え**: 名前が同じ filter はインスタンス（と状態）を引き継ぎ、新しい値は `set_param` で入る。
  外れた filter があれば、チェーンが鳴らした音を全部止めてから外す
- **`unregister_filter` / 再登録**: チェーンの名前は残し、インスタンスだけ外す（素通し）。同じ名前を登録すると
  その場で作り直す（値は引き継ぐ）。`/sd/filters` の読み直しもこれで済む
- **Ruby と C のやりとりはテキスト**（`name(param:value,...),name2`。NVS と同じ書式）。値は C と同じ整数
  （enum は添字、bool は 0 / 1、channel は -1..15）。Ruby → C は `UI._filter_chain_set`、C → Ruby は
  `UI._filter_chains`（リビジョン付き）を `UI::MIDIDevices._poll` が読む。Ruby が適用したあと、範囲に丸めた値で
  書き戻す
- **画面の Filters タブ**: 7×3 のボタン（最後の 1 つが Done）に、登録済みの filter（最大 16）とチェーンにあるが
  未登録の名前（灰色）を並べる。合わせて 20 までなのでページ送りは無い。タブは見出しの右に **Ch / Filters**
- **パラメータ画面**: コンテンツ領域いっぱいの高さで、行数はボードの画面から決まる（収まらなければ `<` `>`）。
  −/+ は長押しで 100ms ごとに連続。値の変更は 1 秒後にまとめて NVS へ（連続で書かないように）。
  パネル（ノブ）での変更は 3 秒後
- **片端が standby のパッチ**（AMY の起動前）: 画面で選んだチェーンは保存済みのチェーンにだけ書き、
  AMY が起動したとき（`ui_mididev_activate` → `ui_filter_restore(key)`）にパッチへ載る
- **UI タスクのスタック**（main タスク、3.5KB）: チェーンや filter の宣言を UI タスクのローカルに置かない。
  画面の作業領域はヒープ（`Scratch`）、`ui_filters.cpp` の UI タスクから呼ばれる関数はその場で書き換える
- **パネルのタイトル**: `"<label> <IN>><OUT>"`（23 文字まで）。同じタイトルのレイアウトがあれば ` 2` などを付ける
- **未実装**: パネルの `param_knob` は `:int` / `:note` / `:channel`、`param_pad` は `:bool` / `:enum` のみ。
  MIDI Learn（9 章）
