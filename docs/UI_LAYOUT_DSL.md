# UI レイアウト DSL（設計ドラフト）

- 状態: **ドラフト（仕様検討中）** / 2026-10-12（主要な論点は 9 章のとおり決定済み）
- 対象: `UI.pad` / `UI.knob` / `UI::XYPad` / `UI::Tombola`、および今後追加する Sequencer など
  UI モジュールが提供する部品
- 関連: [UI_NAVIGATION.md](UI_NAVIGATION.md)（ホームメニュー。レイアウトはそのタイルとして載る）/
  [KNOBS.md](KNOBS.md) / [XYPAD.md](XYPAD.md) / [TOMBOLA.md](TOMBOLA.md) /
  [M5STACK_UI_DESIGN.md](M5STACK_UI_DESIGN.md)、`CLAUDE.md` の「UI のオフスクリーン描画は共有ストリップ」

## 1. 背景と課題

現在の UI 部品は**部品ごとに専用画面を 1 枚ずつ持つ**構成になっている。

| 部品 | 画面 | 配置 |
|---|---|---|
| `UI.pad` | Pads | 固定グリッド（CoreS3 3x2 / 大画面 4x3）。`UI_PAD_*` 定数 |
| `UI.knob` | Knobs | 固定グリッド + バンク帯。`UI_KNOB_*` 定数 |
| `UI::XYPad` | XYPad | コンテンツ領域全面 |
| `UI::Tombola` | Tombola | コンテンツ領域全面 |

このため次のことができない。

- **異なる部品を 1 画面に混在させる**（例: 左に XYPad、右にノブ 2 個、下段にパッド 4 個）。
  演奏中に画面を行き来せずに済む「演奏面」を作れない
- **部品の大きさや個数を変える**（パッドを 8 個並べる、ノブを 1 個だけ大きく置く…）。
  サイズと個数は `ui_common.h` のボード別定数で決め打ち
- Sequencer のような新しい部品を足すたびに、専用画面とボード別の定数ブロックを
  3 ボード分書くことになる

## 2. 目標と非目標

### 目標

1. Ruby スクリプトから、**グリッドの列・行番号**で部品の位置と大きさを指定できる
2. グリッドそのもの（列・行の数と比率、名前付き領域、入れ子）を指定することで、
   少ない記述で多様な配置を作れる
3. **同じスクリプトが CoreS3（320x240）/ Tab5（1280x720）/ CrowPanel（1024x600）で崩れずに動く**。
   ピクセル値を書かずに済むことを基本にする
4. 既存の API（`UI.pad(n)` / `UI.knob(n)` / `UI::XYPad.new` / Pads・Knobs 画面）は**そのまま動く**
5. Sequencer など今後の部品を、「部品（ウィジェット）を 1 種類足す」だけで載せられる

### 非目標（この設計では扱わない）

- ピクセル単位の自由配置（グリッドを細かく切れば実用上足りる）
- 画面上でのレイアウト編集（ドラッグで並べ替える UI）
- 部品の重なり・z 順・透過
- 画面遷移アニメーション
- ステータスバー / ナビバーを消す全画面モード（ステータスバーは常に表示する。ナビバーは
  Home / Back が唯一の脱出手段なので残す）
- 定義済みレイアウトへの部品の追加・削除・移動・表示切替（構造の変更は再定義で行う。9 章 D6）

## 3. 完成イメージ

```ruby
require 'midi'
require 'ui'

dev = MIDI::Device.new(MIDIDevices.usb_midi_device || MIDIDevices.sam2695)

perform = UI.layout("Perform", cols: 4, rows: 3) do
  areas <<~GRID
    xy xy xy k1
    xy xy xy k2
    p  p  p  p
  GRID

  xypad area: :xy, scale: [48, 50, 52, 55, 57, 60], y_cc: 74, device: dev

  knob area: :k1, label: "Cutoff", color: :cyan, value: 64 do |v|
    dev.control_change(74, v.to_i)
  end
  knob area: :k2, label: "Reso", color: :magenta do |v|
    dev.control_change(71, v.to_i)
  end

  # 名前付き領域 :p を 4 列の子グリッドに割る
  grid area: :p, cols: 4 do
    [36, 38, 42, 46].each_with_index do |note, i|
      pad col: i, label: "N#{note}", color: :orange do
        dev.trigger(note, 120, duration: 100)
      end
    end
  end
end

perform.show
loop do
  UI.process
  sleep_ms 5
end
```

```
CoreS3 (320x240)                      Tab5 (1280x720)
┌──────────────────────────────┐      ┌───────────────────────────────────────────┐
│ status                       │      │ status                                    │
├──────────────────────┬───────┤      ├───────────────────────────────┬───────────┤
│                      │  (◔)  │      │                               │    (◔)    │
│        XYPad         │Cutoff │      │                               │   Cutoff  │
│                      ├───────┤      │            XYPad              ├───────────┤
│                      │  (◑)  │      │                               │    (◑)    │
│                      │ Reso  │      │                               │    Reso   │
├──────┬──────┬──────┬─┴───────┤      ├──────────┬──────────┬─────────┴┬──────────┤
│ N36  │ N38  │ N42  │  N46    │      │   N36    │   N38    │   N42    │   N46    │
├──────┴──────┴──────┴─────────┤      ├──────────┴──────────┴──────────┴──────────┤
│ Home     [       ]     Back │      │ Home            [       ]              Back │
└──────────────────────────────┘      └───────────────────────────────────────────┘
```

## 4. グリッドモデル

### 4.1 座標系

- グリッドが占めるのは**コンテンツ領域**（ステータスバーとナビバーの間。
  `UI_CONTENT_Y` / `UI_CONTENT_HEIGHT` / `UI_SCREEN_WIDTH`）
- 列・行番号は **0 始まり**（左上のマスが `col: 0, row: 0`）。グリッド解決の計算と一致し、
  `i * 2` のような計算で位置を出すときに `+ 1` が要らない。
  なお `UI.pad(1)` / `pad.slot(1)` などの**部品番号**は従来どおり 1 始まりで、
  レイアウト上の部品は番号を持たない（ハンドルと `id:` で参照する。5.3）
- 位置は `col:` / `row:` に **Integer または Range** で書く。Range がそのまま「何列ぶん占めるか」になる

```ruby
pad   col: 1, row: 2                  # 1 マス
xypad col: 0..2, row: 0..1            # 3 列 x 2 行
knob  col: 3, row: 0..1               # 縦長 1 列 x 2 行
pad   col: 0...4, row: 2              # 終端を含まない Range も可（= 0..3）
```

`col:` / `row:` の省略値は 0。1 行だけのグリッドなら `row:` を書かずに済む。
Range は昇順のみ（`2..0` はエラー）。

### 4.2 トラック（列・行）の定義

`cols:` / `rows:` は次のどちらでも書ける。

| 書き方 | 意味 |
|---|---|
| `cols: 4` | 等分 4 列 |
| `cols: [3, 1]` | 比率 3:1 の 2 列（CSS の `fr` 相当） |
| `cols: [UI.px(60), 1, 1]` | 先頭 60px 固定、残りを 1:1 |

- 整数・小数は**比率（fr）**。ボード間で崩れないので、基本はこちらを使う
- `UI.px(n)` は固定ピクセル。ラベル列などどうしても固定したいときだけ使う。
  画面の大きさが 4 倍違うので、多用するとボード間で崩れる
- 比率トラックの幅 = `floor(残り幅 × 比率 / 比率合計)`。端数のピクセルは
  先頭のトラックから 1px ずつ配り、**合計がコンテンツ幅とぴったり一致**するようにする
  （右端・下端に隙間を作らない）

### 4.3 余白

| オプション | 既定 | 意味 |
|---|---|---|
| `gap:` | ボード既定（CoreS3 6 / CrowPanel 16 / Tab5 20） | セル間の間隔（px） |
| `padding:` | `gap` と同じ | グリッド外周の余白（px） |

既定値はいまの Pads 画面の `UI_PAD_MARGIN` 相当。`gap: 0` で隙間なし。
ピクセル指定だがボード既定に任せる前提なので、通常は書かない。

### 4.4 名前付き領域（areas）

ASCII で「どの領域がどこを占めるか」を描く。CSS の `grid-template-areas` と同じ考え方。

```ruby
UI.layout("Mix", cols: 4, rows: 3) do
  areas <<~GRID
    seq seq seq seq
    k   k   k   .
    p   p   p   p
  GRID
  sequencer area: :seq, ...
end
```

- 空白区切りのトークン 1 つが 1 マス。`.` は空きマス
- 同じ名前は**長方形**を成していなければならない（L 字などはエラー）
- 行数・列数は `rows:` / `cols:` と一致していなければならない。`rows:` / `cols:` を
  省略した場合は areas から決める（比率は等分）
- 部品は `area: :name` で置く。`col:` / `row:` と同時には指定できない

### 4.5 入れ子グリッド

`grid` で、あるセル（または領域）をさらにグリッドに割る。中の `col:` / `row:` は
**子グリッド内の座標**。

```ruby
grid col: 3, row: 0..2, rows: 4 do      # 右端の 1 列を縦 4 段に
  4.times do |i|
    knob row: i, label: "K#{i + 1}" do |v| ... end
  end
end
```

子グリッドの `gap` / `padding` の既定は親の `gap` / `0`（外周は親のセルの縁にそろえる）。
入れ子の深さに上限は設けないが、C 側に渡るのは解決後の矩形だけなので実行時コストは無い。

### 4.6 ボード差の吸収

比率トラックだけで多くは吸収できるが、CoreS3 で 4x4 のパッドは 1 個 70x35px 程度と
指で押せない大きさになる。そこで画面の情報を Ruby に出し、**素の Ruby の `if` で分岐**させる。
専用の構文（`variant :small do`）は作らない。

```ruby
d = UI.display    # => { width: 320, height: 240, content: [0, 20, 320, 180], large: false }

UI.layout("Drums", cols: d[:large] ? 4 : 2, rows: d[:large] ? 4 : 2) do
  ...
end
```

部品ごとに**最小サイズ**（4.8）を持ち、下回ったら**警告を `UI.log` に出して、そのまま配置する**
（エラーにも自動縮退にもしない。9 章 D7）。収まらないときの組み替えはスクリプトが `UI.display` を見て書く。

### 4.7 検証

レイアウトの確定時（ブロックを抜けたとき）に Ruby 側で検査し、違反は `ArgumentError` を出す。

- 範囲外（4 列グリッドで `col: 4`。有効なのは 0〜3）
- **部品同士の重なり**（タッチの振り分けが曖昧になるので禁止）
- areas の形が長方形でない / 行・列数の不一致 / 未定義の area 名
- 1 レイアウトに 1 個までの部品（4.8）の重複
- 部品数の上限超過（6.4）
- レイアウト数の上限（4）超過。同じ title の再定義は置き換えなので数えない

最小サイズ未満（4.6）はここに含めない。警告のみ。

### 4.8 部品ごとの制約

| 部品 | 1 レイアウト内の数 | 最小サイズの目安 | 備考 |
|---|---|---|---|
| `pad` | 上限内で任意 | 40x32px | |
| `knob` | 上限内で任意 | 56x56px | リング半径はセルの短辺から決める |
| `xypad` | **1** | 96x64px | C 側のモデル（5 スロット）がシングルトン |
| `tombola` | **1** | 96x96px | 物理モデルがシングルトン |
| `sequencer` | 未定（当面 1） | 未定 | 7 章 |
| `label`（追加候補） | 任意 | — | 静的テキスト / 値の表示用 |

最小サイズは**当面この仮値で進め**、実機（特に CoreS3）で指で操作しながらあとで調整する（9 章 D13）。
調整しやすいよう、値は Ruby 側の定数表 1 か所にまとめる。

ラベルの文字サイズ、ノブのリング半径、値表示の位置といった**見た目は部品の矩形から自動で決める**
（`text_size:` のような指定は設けない。9 章 D9）。決め方は 6.3。

## 5. Ruby API

### 5.1 レイアウトの定義と表示

```ruby
layout = UI.layout(title, cols:, rows:, gap: nil, padding: nil) { ... }  # => UI::Layout
layout.show          # レイアウト画面を前面に（= UI.show(layout)。UI_NAVIGATION.md）
layout[:cutoff]      # id: を付けた部品のハンドル
layout.delete        # レイアウトを削除（Home のタイルも消え、C 側の実体も解放）
```

- `title` はステータスバーとナビバー中央に出る名前
- ブロックは**引数なしなら `instance_eval`、引数ありならビルダを渡す**。
  前者は短く書ける、後者はブロック内でスクリプトのメソッド・`self` をそのまま使える

```ruby
UI.layout("A", cols: 2) { pad col: 1 }                 # instance_eval
UI.layout("A", cols: 2) { |l| l.pad col: 1 }           # yield
```

- レイアウトは**複数定義できる**（上限 4）。5 つ目の定義は `ArgumentError`。それぞれが Home のタイルになり、
  `layout.show` / `UI.show(layout)` / Home のタップで切り替える
- 切り替えを Ruby の応答待ちにしないため、C 側は定義済みレイアウトの部品表を**すべて保持**する
  （1 レイアウト数 KB）。部品の状態（ノブの値など）もレイアウトごとに残る
- **構造の変更は再定義だけ**で行う。同じ title で `UI.layout` を呼び直すと、そのレイアウトを
  丸ごと置き換える（部品の状態は、`id:` が同じ部品も含めて定義の初期値に戻る。表示中なら全画面を 1 回合成し直す）。
  部品を 1 個だけ足す・消す・動かす・隠す API は作らない。ハンドル（5.3）で変えられるのは
  ラベル・色・値のように**配置を変えないもの**だけ

### 5.2 部品の定義

すべての部品は**配置キーワード + 既存 API と同じキーワード + ブロック**で書く。
既存 API と覚え方を揃え、移行時に引数をそのまま持ってこられるようにする。

| 配置キーワード | 意味 |
|---|---|
| `col:` / `row:` | Integer または Range（4.1） |
| `area:` | 名前付き領域（4.4） |
| `id:` | 後から `layout[id]` で引くための Symbol（任意） |

```ruby
pad   col:, row:, label:, color:, type: (:trigger | :momentary | :toggle) { |state| }
knob  col:, row:, label:, color:, value:, min:, max:, step:, origin:, sensitivity: { |v| }
xypad col:, row:, **UI::XYPad.new と同じキーワード          # => UI::XYPad
tombola col:, row:, **UI::Tombola.new と同じキーワード      # => UI::Tombola
grid  col:, row:, cols:, rows:, gap:, padding: { ... }       # 入れ子
```

ブロックは `do ... end` で書く（`label: "x" { }` は `{}` が引数側に結びつき構文エラーになる）。ブロックの引数は既存と同じ（pad は type ごとに既存の `dispatch_pad_event` と同じ規則、
knob は値）。knob のバンクはレイアウトには持ち込まない（ページの切り替えで代替する）。

### 5.3 部品ハンドル

部品定義の戻り値はハンドルオブジェクト。既存の `UI.pad_label(n, ...)` 系に相当する
操作をメソッドとして持つ。

```ruby
cutoff = knob col: 3, label: "Cutoff" do |v| ... end
cutoff.value             # => 64.0
cutoff.value = 100       # ブロックも呼ぶ（UI.knob_set と同じ。notify: false で表示のみ）
cutoff.label = "LPF"
cutoff.color = :yellow

kick = pad col: 0, type: :toggle do |on| ... end
kick.state               # => true / false
```

| ハンドル | 主なメソッド |
|---|---|
| `UI::Layout::Pad` | `state` / `label=` / `color=` |
| `UI::Layout::Knob` | `value` / `value=(v, notify: true)` / `reset` / `send` / `label=` / `color=` |
| `xypad` | 既存の `UI::XYPad` インスタンスそのもの（`slot` / `hold=` / `on_touch`） |
| `tombola` | 既存の `UI::Tombola` インスタンスそのもの |

`layout.send_all` は全ノブのブロックを現在値で呼ぶ（`UI.knob_send_all` 相当）。
定義しただけでは送らないのは Knobs 画面と同じ。

### 5.4 既存 API との関係

- `UI.pad(n)` / `UI.knob(n)` は**従来どおり Pads / Knobs 画面の部品を指す**。
  レイアウト上の pad / knob は**別の実体**で、番号空間も状態も共有しない（9 章 D3）。
  そのため Pads 画面の個数（CoreS3 6 / 大画面 12）に縛られずに並べられる
- `UI::XYPad.new` / `UI::Tombola.new` も従来どおり専用画面で使える。
  レイアウトの `xypad` は内部で `UI::XYPad.new` を呼び、表示先を部品の矩形にする。
  シングルトンなので、レイアウトに置いた XYPad と XYPad 画面は**同じモデルを映す**
- `UI::SCREEN_LAYOUT = 9` を追加する（`ui_screen_index_t` の末尾に追加。既存番号は変えない）。
  画面番号は 1 つで、どのレイアウトを映すかは画面内の状態として持つ。`UI.show(:layout)` は
  最後に表示したレイアウトを出す

## 6. 実装方針

### 6.1 全体構成

```
 [PicoRuby task]                                [UI task]
  UI.layout { ... }
    ├ グリッド解決（Ruby）: col/row/area → 画面ピクセルの矩形
    ├ 検証（4.7）
    └ _layout_begin / _layout_add_*(kind, x, y, w, h, 設定) / _layout_commit
                                    │
                                    └─ pending ─→ ScreenLayout::update()
                                                   ├ 差し替え → 全画面の再合成
                                                   ├ タッチ → 部品へ振り分け
                                                   └ ui_event_push(WIDGET, id, ...)
  UI.process ←──────────────────────────────────── イベントキュー
    └ 部品のブロック → MIDI 送信
```

**グリッドの解決は Ruby、C++ が受け取るのは矩形のリストだけ**にする。

- グリッドの機能（areas、入れ子、px トラック…）を増やしても C 側は変わらない
- 解決はレイアウト定義時に 1 回だけなので、Ruby で計算しても実行時コストは無い
- グリッド解決はピュア Ruby なので、ホスト（Linux）上でテストできる

C 側に必要な画面情報は `UI._content_rect` / `UI._layout_defaults`（gap 等）で Ruby に出す。

新しく足す C 関数のバインディングは **mruby 版（`src/mruby/ui.c`）だけ**に書く。
picoruby-ui は MIDoRI 専用 gem で、MIDoRI は mruby しかビルドしないため（9 章 D8）。
`src/mrubyc/ui.c` には追加しない（既存の mruby/c バインディングはあとで別作業として削除する）。

### 6.2 C++ 側: Widget と ScreenLayout

部品の描画とタッチ処理を、画面クラスから**矩形を受け取るウィジェット**に切り出す。

```cpp
class Widget {
public:
    int16_t x, y, w, h;            // 画面座標
    uint8_t id;                     // Ruby に返すイベント用
    virtual void draw() = 0;        // 矩形全体を描く（合成中は帯に入る。ui_gfx() に絶対座標で）
    virtual void update() {}        // 差分描画 / アニメーション
    virtual bool animated() const { return false; }
    virtual void onDown(int touchId, int px, int py) {}
    virtual void onMove(int touchId, int px, int py) {}
    virtual void onUp(int touchId) {}
};

class ScreenLayout : public Screen {
    Widget*  m_widgets[UI_LAYOUT_MAX_WIDGETS];
    int8_t   m_touchOwner[MAX_TOUCH_POINTS];   // touchdown で捕まえた部品
    ...
};
```

- **タッチの捕捉**: 押した瞬間に矩形で当たり判定し、その指の move / up は離すまで同じ部品へ送る
  （指が矩形の外へ出ても。Knobs の `m_touchToKnob` と同じ考え方）
- **描画**: 全面の再描画は既存の `ui_strip::compose()` で帯ごとに `draw()` を呼ぶだけで済む
  （各部品は `ui_gfx()` に絶対座標で描く。CLAUDE.md の方針どおり）。
  アニメーションする部品（XYPad / Tombola / Sequencer の再生位置）は
  `update()` で**自分の矩形だけ** `ui_strip::paint()` する
- **差し替え**: Ruby の `_layout_commit` は新しいウィジェット表を pending に置くだけ。
  UI タスクが次の `update()` で古い表と入れ替えて解放し、全画面再描画を要求する
  （Ruby タスクから描画中の表を触らない）

### 6.3 既存の描画コードの部品化

| 部品 | いまの実装 | 部品化で必要なこと |
|---|---|---|
| Pad | `ScreenPads::drawPad()` が `getPadRect()` で位置を引く | 矩形と `pad_config_t` を受け取る関数に分ける。Pads 画面もそれを使う |
| Knob | `UI_KNOB_R_OUT` などの定数で描く | リング半径・ラベル位置・文字サイズを**矩形から計算する構造体**にする。Knobs 画面はボード定数からその構造体を作る |
| XYPad | `contentRect()` がコンテンツ全面を返す | 部品の矩形を返すようにする。`ui_xypad_touch_*` はもともと `content_x/y/w/h` を引数に取るのでモデルは無改造 |
| Tombola | 正規化座標で計算し描画時にピクセル化 | 描画時の基準を部品の矩形にする。物理は無改造 |

見た目の寸法は**矩形から自動で計算する**（9 章 D9）。目安:

- 文字サイズ: 部品の高さに応じて 1〜3 の段階で選び、ラベルが幅に収まらなければ 1 段下げる。
  それでも収まらなければ末尾を切る
- ノブ: リング外径 = `min(w, h − ラベル行の高さ) × 0.45`、内径はその 0.77 倍
  （いまの `UI_KNOB_R_OUT` / `UI_KNOB_R_IN` の比率）
- 計算式は「既存画面のセル寸法を入れると、いまのボード別定数とほぼ同じ値になる」ように決める

既存の画面の見た目を変えないことを、部品化の受け入れ条件にする。

### 6.4 部品の状態とメモリ

- レイアウトの部品は `g_pads` / `g_knob_bank*` とは**別の実体**を持つ（5.4）
- ウィジェット表と部品の状態は `_layout_commit` 時に**ヒープに確保**し、`delete` / 再定義 /
  スクリプト停止で解放する。static な大きい配列は置かない（[MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md)）
- 上限 `UI_LAYOUT_MAX_WIDGETS` は 32（仮）。knob 1 個 ≈ 64B、pad ≈ 32B 程度なので数 KB に収まる
- スクリプト停止時（`picoruby_esp32_midi_cleanup()` の経路）にレイアウトを破棄し、
  レイアウト画面を表示中なら Home に戻す

### 6.5 イベント

部品イベントは新しい種別 `UI_EVENT_WIDGET` に統一する。

```c
struct {
    uint8_t id;        // ウィジェット id
    uint8_t kind;      // pad / knob / ...
    uint8_t phase;     // press / release / change / final
    bool    state;     // pad
    float   value;     // knob
} widget;
```

- ノブの変化は**部品ごとに 1 通に間引く**（`ui_knob_event_post` と同じ上書き規則）
- XYPad / Tombola はシングルトンなので既存の `UI_EVENT_XYPAD_TOUCH` / `UI_EVENT_TOMBOLA_HIT`
  をそのまま使う
- Ruby 側は `$ui_widget_callbacks[id]` からブロックを引いて呼ぶ。例外は既存と同じく
  `UI.log` に出して握りつぶす

### 6.6 対象外のボード

Freenove（画面なし）は他の UI と同じく no-op スタブ。`UI.layout` は Ruby 側で
グリッド解決と検証まで行い（スクリプトの誤りは画面が無くても分かる）、C 呼び出しは何もしない。

## 7. Sequencer を載せるための要件

Sequencer の仕様は別ドキュメントで決めるが、レイアウト側は次を満たしておく。

- **矩形を受け取って自分で描く**汎用の Widget インタフェース（6.2）。ステップ数・トラック数に応じた
  セル分割は部品の中で行う（グリッド DSL の入れ子で 16 個の pad を並べる方式は取らない。
  再生位置の描画とステップの一括編集を部品の中で完結させたいため）
- **アニメーション部品**として毎フレーム `update()` を呼ばれること（再生位置）
- 発音タイミングがリズムに直結するので、**発音は C++ 側**（Tombola と同じ判断。
  [TOMBOLA.md](TOMBOLA.md)）。部品から MIDI クロック / スケジューラに触れる経路が要る
- 最小サイズが大きい（16 ステップを並べる）ため、CoreS3 では横 1 行を占有する前提

想定する記述:

```ruby
seq = sequencer area: :seq, steps: 16, tracks: 4,
                notes: [36, 38, 42, 46], device: dev
seq.pattern(1, "x...x...x...x...")
```

## 8. 段階的な進め方

| 段階 | 内容 | 完了条件 |
|---|---|---|
| 1 | Ruby のグリッド解決 + 検証（ホストでテスト） | トラック分割・areas・入れ子・重なり検出のテストが通る |
| 2 | `ScreenLayout` + Pad / Knob 部品。Pads / Knobs 画面を同じ描画関数へ移行 | 既存 2 画面の見た目が変わらない。3 ボードでレイアウト例が動く |
| 3 | XYPad / Tombola 部品 | 部品の矩形で XYPad / Tombola が動き、専用画面も従来どおり |
| 4 | Sequencer 部品 | 7 章 |
| 5（任意） | Pads / Knobs / XYPad 画面を「組み込みレイアウト」として再実装 | 画面ごとのボード別ジオメトリ定数を削減 |

## 9. 決定事項と未決事項

### 決定事項（2026-10-12）

| # | 論点 | 決定 | 反映先 |
|---|---|---|---|
| D1 | 列・行番号 | **0 始まり** | 4.1 |
| D2 | 位置の書き方 | **Range**（`col: 0..2`。終端を含まない `0...3` も可） | 4.1 / 5.2 |
| D3 | レイアウト上の pad / knob と既存の番号 | **共有しない。別実体** | 5.4 / 6.4 |
| D4 | 複数レイアウト | **複数定義できる**。各レイアウトが Home のタイルになる（[UI_NAVIGATION.md](UI_NAVIGATION.md)） | 5.1 |
| D5 | ステータスバー | **常に表示する** | 2 章（非目標） |
| D6 | 実行時の変更 | **再定義のみ**。ハンドルで変えられるのは配置を変えない属性だけ | 5.1 / 5.3 |
| D7 | 小さい画面で収まらないとき | **警告して配置する** | 4.6 / 4.7 |
| D8 | mruby/c バインディング | **作成しない**（mruby のみ） | 6.1 |
| D9 | 部品の見た目・ラベルの文字サイズ | **自動**（矩形から計算） | 4.8 / 6.3 |
| D10 | 定義できるレイアウトの上限数 | **4** から始める | 5.1 / 4.7 |
| D11 | 再定義時の部品の状態 | **初期値に戻す**（`id:` が同じでも引き継がない） | 5.1 |
| D12 | 既存の `src/mrubyc/ui.c` | **あとで削除する**（このレイアウト DSL の作業には含めない） | 6.1 |
| D13 | 部品ごとの最小サイズ | **4.8 の仮値で実装を進める**。実機（CoreS3 など）で操作しながらあとで調整する | 4.8 |

### 未決事項

現時点で無し。
