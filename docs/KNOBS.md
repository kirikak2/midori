# Knobs 画面

CC などの連続値を指で回して操作する、ノブのグリッド画面。Pads がワンショット
（叩く）担当なのに対し、Knobs は**持続的なパラメータ**（カットオフ、リバーブ量、
テンポ、Tombola の `rotation` …）を担当する。

- 対応ボード: M5Stack CoreS3（6 個 x 4 バンク）/ M5Stack Tab5（12 個 x 4 バンク）。Freenove は no-op
  スタブ（[main/platform/platform_freenove.c](../main/platform/platform_freenove.c)）
- 画面: [main/ui/screen_knob.cpp](../main/ui/screen_knob.cpp)（描画とタッチ）
- 値の保持: [main/ui/ui_common.cpp](../main/ui/ui_common.cpp) の `ui_knob_*`
- Ruby API: `UI.knob`（[mrbgems/picoruby-ui/mrblib/ui.rb](../mrbgems/picoruby-ui/mrblib/ui.rb)）
- サンプル: [examples/knobs.rb](../examples/knobs.rb)

## 位置づけ

| | Pads | Knobs |
|---|---|---|
| 値 | bool（押下 / トグル） | 連続値（float、既定 0〜127） |
| 操作 | タップ | ノブの周囲をなぞる（円周ドラッグ） |
| MIDI を送るのは | Ruby のブロック | Ruby のブロック（同じ） |
| 個数 | CoreS3 6 / Tab5 12 | 同じ × 4 バンク（CoreS3 24 / Tab5 48） |
| 外部入力 | — | ロータリーエンコーダから値を書き込める |

Pads と同じく **grid + index + label + color + ブロック** の構成にして、Ruby API も
`UI.pad` と対称にする（`UI.knob` / `UI.knob_label` / `UI.knob_color` /
`UI.knob_clear` …）。覚えることを増やさないため。

## 構成

```
 [PicoRuby task]                         [UI task (app_main, 10ms周期)]
  UI.knob(1, label: "Cutoff") { |v| }     ui_update()
    └ ui_knob_set_config() ──→ g_knobs ──→ ScreenKnobs::update()
                                 (dirty)      └ 差分だけ再描画
                                            ScreenKnobs::onTouchMove()
                                              ├ 外積で dθ → 値を更新
                                              └ ui_event_push()   ← 値が変わった時だけ
  UI.process ←──────────────────────────── イベントキュー
    └ UI.knob のブロック
         └ dev.control_change(74, v)     ← MIDI を送るのはここ

  UI.knob_set(1, 100)  ← エンコーダから
    ├ 値を更新 → dirty
    └ ブロックを直接呼ぶ（同じ PicoRuby タスク上）
```

**MIDI を送るのは Ruby のブロック。** C 側は値を持って絵を描き、変化を通知する
だけで、MIDI には一切関与しない。`UI.pad` と同じ役割分担なので、スクリプトから
見て「UI 部品にブロックを括りつける」という一つの覚え方で済む。

これは Tombola（発音を C++ 側でやる）とは逆の判断だが、要求が違う。Tombola で
問題になるのは**リズムのジッタ**で、`UI.process` の間隔がそのまま発音タイミングの
ばらつきになる。CC は「なめらかに追従すればよい」ので、`sleep_ms 5` で回していれば
数 ms の遅延は体感できない。代わりに、宛先・CC 番号・カーブ・複数 CC への分配と
いった**送り方の自由**がスクリプト側に丸ごと残る。

Tombola と違って**毎フレームの tick は不要**。ノブの値が動くのは
「画面上でのタッチ」か「Ruby からの書き込み」だけで、どちらもイベント駆動。

## 画面レイアウト

グリッドの右に**バンク切り替えの縦ストリップ**を置く（後述）。

### M5Stack CoreS3（320x240 / 6 個 x 4 バンク）

```
┌────────────────────────────────────────┐
│  Knobs                        [MIDI●] │
├──────────────────────────────────────┬─┤
│    ╭───╮     ╭───╮     ╭───╮        │A│ ← 選択中（白地に黒）
│   ( 064 )   ( 127 )   ( 000 )       ├─┤
│    ╰───╯     ╰───╯     ╰───╯        │B│ ← 割り当て有り（白文字）
│   Cutoff     Reso      Knob 3        ├─┤
│    ╭───╮     ╭───╮     ╭───╮        │C│ ← 空（グレー）
│   ( 032 )   ( 100 )   ( 064 )       ├─┤
│    ╰───╯     ╰───╯     ╰───╯        │D│
│   Attack    Release     Pan          │ │
├──────────────────────────────────────┴─┤
│   [◀]      [Send]  Knobs A     [▶]    │ ← タイトルにバンク名
└────────────────────────────────────────┘
```

### M5Stack Tab5（1280x720 / 12 個 x 4 バンク）

4 列 × 3 行。Pads と同じセル寸法（280x180）をそのまま使う。

```
┌──────────────────────────────────────────────────────────┬──┐
│  Knobs                                          [MIDI●] │  │
├──────────────────────────────────────────────────────────┼──┤
│   ( 064 )    ( 127 )    ( 000 )    ( 064 )              │A │
│   Cutoff      Reso      Attack     Release              ├──┤
│   ( 032 )    ( 100 )    ( 064 )    ( 010 )              │B │
│    Pan       Volume      Rev       Chorus               ├──┤
│   ( 000 )    ( 064 )    ( 127 )    ( 064 )              │C │
│   Knob 9     Knob 10    Knob 11    Knob 12              ├──┤
│                                                         │D │
├──────────────────────────────────────────────────────────┴──┤
│   [◀]                [Send]  Knobs A             [▶]        │
└─────────────────────────────────────────────────────────────┘
```

### 寸法

Pads と同じく `ui_common.h` にボード分岐で置く。

| | CoreS3 | Tab5 |
|---|---|---|
| `UI_KNOB_COUNT` | 6 | 12 |
| `UI_KNOB_COLS` / `ROWS` | 3 / 2 | 4 / 3 |
| セル | 92 x 86 | 280 x 180 |
| セル間隔 | 5 / 4 | 20 / 20 |
| グリッド開始 | (3, 22) | (20, 60) |
| リング外半径 `R` | 30 | 68 |
| リング内半径 `r` | 23 | 52 |
| 値テキスト | size 1（6x8） | size 3（18x24） |
| ラベル | size 1、セル上端 +70、15 文字 | size 3、セル上端 +150、15 文字 |
| バンクストリップ | x=292、幅 26、ボタン 26x42 | x=1215、幅 50、ボタン 50x140 |

- リング中心はセル中心より上（CoreS3: セル上端 +46,+34、Tab5: +140,+76）。
  下にラベル 1 行分を空ける
- 値テキストは 3 文字ぶん（CoreS3 で 18x8）を内円（直径 46）に置く。右詰め固定幅で
  描くので、背景色つきで上書きすれば消去がいらない
- ラベルは 15 文字まで（CoreS3 size 1 で 90px ≒ セル幅）。溢れる分は切り詰める

グリッドは Pads の `getPadRect()` と同じ計算（`index % COLS`, `index / COLS`）。

**ストリップのぶんの場所**は、Tab5 では**既存の余白にそのまま入る**
（グリッド 1180px + 左右 50px の余白があるので、グリッドを少し左に寄せるだけ）。
CoreS3 だけはセル幅を Pads の 100 → 92 に詰める必要がある。リングは R=30
（直径 60）なので、詰めても円は縮まない — 削るのはセルの左右の余白だけ。

## バンク

ノブ 4 面ぶん（CoreS3 24 / Tab5 48）を持ち、右のストリップで切り替える。
**バンクはノブ設定の丸ごと 1 セット**なので、ラベルも色も値域もバンクごとに違ってよい。
「A = シンセ、B = ドラム、C = エフェクト」といった使い方を想定している。

```c
#define UI_KNOB_BANKS  4        // A, B, C, D
```

- ストリップのボタンをタップで切り替え。**タップのみ**（ドラッグは受けない）
- 表示は 3 状態: 選択中（白地に黒）/ 割り当てのあるバンク（白文字）/
  空のバンク（ダークグレー文字）。空でもタップは受け付ける — あとから
  スクリプトが割り当てる可能性があるため
- ステータスバーとナビ中央のタイトルは `Knobs A` のようにバンク名つきにする。
  画面を離れて戻ったときにどのバンクにいるか分かるように
- 切り替えは**コンテンツ領域の全面再描画**。ユーザーの明示的な操作なので、
  ここで差分描画に凝る意味はない
- 切り替え時は**掴んでいる指をすべて離した扱いにする**（`m_touchToKnob` を
  クリア）。切り替え前のノブを掴んだままの指が、切り替え後の別のノブを
  回し続けるのを防ぐ

### スワイプで切り替えないのはなぜか

グリッド上の左右スワイプは**採用しない**。当たり判定がセル矩形全体で、
そこに触れた指はすべてノブのドラッグになるため、スワイプと区別できない。
「ノブを回したつもりがページが変わる」ほうが確実に事故になる。
ストリップの中で上下になぞるのは受けてもよい（将来）。

### バンクをまたぐ操作

- 触れるのは**選択中のバンクだけ**。他のバンクの値はそのまま保持される
- スクリプトからは `bank:` を指定すれば非選択のバンクも読み書きできる。
  値は更新されブロックも呼ばれるが、画面には何も起きない（選択中のバンクの
  ときだけ dirty を立てる）
- **バンクを切り替えても MIDI は自動では出ない**。切り替えた瞬間に 12 通の CC が
  飛ぶのが嬉しいかはパッチ次第なので、必要なスクリプトが自分で書く:

```ruby
UI.on(:knob_bank) do |e|
  UI.knob_send_all      # 切り替え先のバンクの現在値を送り直す
end
```

## ノブ 1 個の見た目

```
        値 = 25%                値 = 75%              origin: :center, 値 = -30%
      ▁▁▁▁▁▁▁                 ▁▁▁▁▁▁▁                    ▁▁▁▁▁▁▁
    ▟███░░░░░░░▙            ▟█████████▙                ▟░░░███░░░▙
   ██   032   ░░█          ██   096   █░              ██   -38   ░█
    ▜░░░░░░░░░░▛            ▜█████████▛                ▜░░░░░░░░░▛
      ╲     ╱                 ╲     ╱                    ╲     ╱
       開始  終了               （下がギャップ）
```

- ゲージは **270° スイープ、下 90° がギャップ**。開始 = 左下（7時半）、
  終了 = 右下（4時半）
- 塗り（`█`）= ノブの色、未達部分（`░`）= `UI_COLOR_DARKGRAY`
- 内円は黒。中央に現在値
- ラベルはリングの下。未割り当てなら `Knob N` をグレーで
- 指が触れている間はリング外周に黄色の細枠（Pads の押下表示と同じ言語）
- `origin: :center` のときは 12 時位置（スイープの中央）から左右に伸ばす。Pan や
  ±デチューンのためのモード

### 角度

LovyanGFX の `fillArc(cx, cy, r0, r1, angle0, angle1, color)` は
**0° = 3時方向、時計回り**（[LGFXBase.cpp](../managed_components/m5stack__m5gfx/src/lgfx/v1/LGFXBase.cpp)
の `fill_arc_helper()` が境界線を `(cos, sin)` 方向で取るため。画面座標は y が
下向きなので角度も時計回りになる）。したがって:

```c
#define KNOB_ANGLE_START  135.0f   // 左下
#define KNOB_ANGLE_SWEEP  270.0f   // → 405°（右下）

angle(v) = KNOB_ANGLE_START + KNOB_ANGLE_SWEEP * (v - min) / (max - min);
```

135° → 180°（9時）→ 270°（12時）→ 360°（3時）→ 405°（右下）と回る。

## 値モデル

```c
float value;       // 現在値
float min, max;    // 既定 0.0 / 127.0
float step;        // 量子化。既定 1.0（0 で連続値）
float initial;     // リセット先。既定は min（origin: :center なら中央）
uint8_t origin;    // KNOB_ORIGIN_MIN | KNOB_ORIGIN_CENTER
```

- 内部は float。`step` で量子化してから確定する（`v = min + round((v-min)/step)*step`）
- 表示は `step >= 1.0` なら整数、それ未満なら小数 1 桁
- **ブロックを呼ぶのは量子化後の値が変わったときだけ**。0〜127 / step 1 なら
  270° を 128 分割 = 2.1°/step なので、指を 2°動かすごとに 1 回が上限になる。
  レート制限を別に入れなくてよいのはこのため
- 値は常に min/max でクランプする。角度を積算しないので、上限に張り付いたあと
  指を戻せば即座に減り始める（巻き上がりが無い）
- **CC への変換はスクリプトの仕事**。`min: 20, max: 300` の BPM ノブを CC に
  載せたければ、ブロックの中で自分でマップする

## タッチ操作

### 外積で回す

Tombola のドラッグ回転と**同じ式**（[docs/TOMBOLA.md](TOMBOLA.md) の「ドラッグで回す」）。
リング中心から指へのオフセットを `(px, py)`、その 1 サンプルぶんの移動を
`(dx, dy)` として:

```c
dtheta = (px * dy - py * dx) / (px * px + py * py);   // ラジアン
value += (max - min) * dtheta / KNOB_SWEEP_RAD * sensitivity;
```

これは `d(atan2(py, px))` そのものなので、`sensitivity = 1.0` では
**指の位置がゲージの先端に厳密に一致する**（指の下にある目盛りは指の下から
動かない）。実物のノブの縁を掴んで回すのと同じ。

| 指の動き | ノブの左側 | ノブの右側 |
|---|---|---|
| 上へ | 値が増える | 値が減る |
| 下へ | 値が減る | 値が増える |

| 指の動き | ノブの上側 | ノブの下側 |
|---|---|---|
| 左へ | 値が減る | 値が増える |
| 右へ | 値が増える | 値が減る |

- **中心付近は分母をクランプする**（`KNOB_DRAG_MIN_RADIUS` = `R * 0.25`）。r² で
  割るので、クランプが無いと中心 3px の位置で 10px 動かしただけでフルレンジを
  何往復もする
- **中心から遠いほど、同じ角度に対する指の移動距離が長い**＝細かく合わせられる。
  セル内なら円の外でも掴めるようにするので、隅を持てば実質的な微調整になる
- 押した瞬間に値は飛ばない（相対操作なので絶対位置を拾わない）。ピックアップ
  モードのような仕掛けは不要
- `sensitivity` は既定 1.0。0.5 にすると 540° でフルレンジ（微調整向き）、
  2.0 なら 135° で振り切る

### 当たり判定とマルチタッチ

- 当たり判定は**バンクストリップが先、次にセル矩形全体**。円の外周ぎりぎりを
  狙わせない
- `m_touchToKnob[MAX_TOUCH_POINTS]` で指ごとに掴んだノブを覚える（Pads の
  `m_touchToPad` と同じ）。Tab5 なら 4〜5 個のノブを同時に動かせる
- 同じノブを 2 本の指が掴んだ場合は**先に掴んだ指を優先**し、あとの指は無視する
  （2 本ぶんの角速度が足し合わされると暴れるため）
- `Screen::onTouchMove()` は Tombola のために既にある。「離した時のイベントは
  押し始めた場所で判定する」修正も入っているので、ナビバーまで指が流れても
  ドラッグはきちんと終わる。**ノブを掴んだ指がストリップの上へ流れても
  バンクは切り替わらない**（指はノブに属したままなので）
- タップ（動かさずに離す）は**何もしない**。長押しリセットは将来（後述）

## 値の配送

MIDI を送るのがブロックである以上、**スクリプトが `UI.process` を回さない限り
何も鳴らない**。ここは Pads と同じ制約だが、ノブは 1 本のドラッグで数十回
発火するぶん、キューの扱いを決めておく必要がある。

```ruby
loop do
  UI.process       # ← これを回さないとブロックは呼ばれない
  sleep_ms 5
end
```

### ノブごとに 1 通へまとめる

キューに積むのは**ノブごとに最大 1 通**。既に未処理のイベントがそのノブに
あるなら、新しいイベントを足さずに値だけ差し替える（指の軌跡の履歴に意味は
無く、欲しいのは常に最新値だから）。

この結果:

- 未処理のノブイベントは最大 `UI_KNOB_COUNT` 通。**溢れて捨てられることが無い**
  （キューに積むのはタッチと `[Send]` だけで、どちらも選択中のバンクにしか
  触れない。Ruby 側の `knob_set` / `knob_send_all` はブロックを直接呼ぶので
  キューを通らない）
- `UI.process` の間隔が空いても、ブロックが呼ばれる回数が増えるだけで
  遅れは蓄積しない（ポーリングが遅い＝間引かれる、という素直な劣化）
- 指を離した最後の値（`final: true`）は必ず届く。ここを取りこぼすと音源が
  中途半端な値のまま取り残されるため

### 遅延

`sleep_ms 5` のループなら、指を動かしてから CC が出るまで最悪 5ms + トランス
ポートの送信時間。DIN MIDI の 1 メッセージが約 1ms なので、実用上は問題にならない。
逆に `sleep_ms 100` のループでは 10 回/秒しか CC が出ず、階段状に聞こえる。
**ノブを使うスクリプトのポーリングは 5〜10ms** を推奨として書いておく。

## 描画

Pads と同じ dirty マスク方式。`ui_knob_*` のセッターは PicoRuby タスクから
呼ばれるので LCD には触れず、`ui_knob_mark_dirty(index)` を立てるだけ。
`ScreenKnobs::update()` が `ui_knob_take_dirty()` を取って描く。

### 差分だけ描く

リング全面の再描画はやらない。前回描いた値 `m_drawn[i]` を覚えておいて、
**変化したぶんの扇形だけ**塗る:

```
値が増えた: fillArc(cx, cy, r, R, angle(m_drawn), angle(v), color)      ← ノブ色で塗る
値が減った: fillArc(cx, cy, r, R, angle(v), angle(m_drawn), DARKGRAY)   ← track 色で消す
```

1 ステップぶんなら 2°程度の扇形（CoreS3 で 30 画素弱）で済む。
Tombola のようなスプライト／PPA は要らない: 毎フレーム全面が変わるわけではなく、
「消してから描く」中間状態も生じないのでちらつかない。

- 全面描画するのは `enter()` と、ラベル / 色 / min / max / origin が変わったとき
  だけ（`m_fullRedraw` フラグ）
- 値テキストは固定幅・背景色つきで上書き。消去のための塗りつぶしは不要
- 触れている / 離したときの黄色枠は `drawCircle` 1 本
- 誰も触っていなくて dirty も無ければ**何も描かない**。Knobs 画面を開きっぱなしに
  してもコストがかからない

### メモリ

静的に持つのは**バンク 1 のぶんだけ**（Tab5 で 12 x 約 44 byte ≒ 530 byte）。
4 バンクを丸ごと static に置くと Tab5 で 2.1KB になり、
[docs/MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) の「static に数百 byte 以上の
inline buffer を置くと内部 DRAM の .bss レイアウトが変わり、最悪 LCD が真っ暗に
なる」に正面からぶつかる。

そこで **バンク 2〜4 は初回使用時に malloc する**（1 バンク約 530 byte を 1 回、
`ui_knob_clear_all()` まで解放しない）。未確保のバンクは「全ノブ未割り当て」と
同じ扱いで、ストリップではグレー表示になる。スプライト等の溜め込みバッファは
この画面には持たせない。

## Ruby API

```ruby
require 'midi'
require 'ui'

dev = MIDI::Device.new(MIDIDevices.sam2695)

UI.knob(1, label: "Cutoff", color: :cyan, value: 64) do |v|
  dev.control_change(74, v.to_i)
end

UI.knob(2, label: "Reso", color: :magenta) do |v|
  dev.control_change(71, v.to_i)
end

UI.knob(3, label: "Pan", color: :green, origin: :center, value: 64) do |v|
  dev.control_change(10, v.to_i)
end

# CC でなくてもよい。ブロックの中身は完全に自由
UI.knob(4, label: "Rotation", color: :orange, min: -60, max: 60) do |v|
  tombola.rotation = v
end

# 1 つのノブから複数の宛先へ、カーブをかけて送る
UI.knob(5, label: "Filter", color: :yellow) do |v|
  dev.control_change(74, v.to_i)
  usb.control_change(74, (v * v / 127).to_i)
end

UI.knob_send_all   # 定義しただけでは何も送られない。初期値をここで送る
UI.knobs           # Knobs 画面へ

loop do
  UI.process
  sleep_ms 5
end
```

ブロックは `|value|` を 1 個だけ受ける（`UI.pad` の `|pressed|` と同じ）。
どのノブが動いたかも要るなら、生イベント `UI.on(:knob_change)` を使う（後述）。
そちらには `bank` と `index` が入っている。

### メソッド

| メソッド | 内容 |
|---|---|
| `UI.knob(index, **opts, &block)` | ノブを設定（index は 1 始まり。Pads と同じ） |
| `UI.knob_value(index)` | 現在値（Float） |
| `UI.knob_set(index, value)` | 値を設定。表示更新 + **ブロックを呼ぶ** |
| `UI.knob_set(index, value, notify: false)` | 値と表示だけ更新（ブロックを呼ばない） |
| `UI.knob_label(index, str)` | ラベル変更 |
| `UI.knob_color(index, sym)` | 色変更 |
| `UI.knob_reset(index)` / `UI.knob_reset_all` | `initial` へ戻す（ブロックも呼ぶ） |
| `UI.knob_send_all` | 全ノブのブロックを現在値で呼ぶ（初期送信・再同期用） |
| `UI.knob_clear(index)` / `UI.knob_clear_all` | 割り当て解除 |
| `UI.knob_bank` | 選択中のバンク（1〜4） |
| `UI.knob_bank = n` | バンクを切り替える（画面も変わる） |
| `UI.knob_count` | このボードの 1 バンクあたりのノブ数（6 or 12） |
| `UI.knob_banks` | バンク数（4） |
| `UI.knobs` | Knobs 画面へ切り替え（`UI.set_screen(UI::SCREEN_KNOBS)`） |

`knob_value` / `knob_set` / `knob_label` / `knob_color` / `knob_reset` /
`knob_clear` / `knob_send_all` は `bank:` を取る。**既定は選択中のバンク**
（エンコーダのループが「いま見えているノブ」を触るのが自然なため）。
`UI.knob` の定義先も同じ既定なので、複数バンクを使うスクリプトは
`bank:` を明示して定義する:

```ruby
UI.knob(1, bank: 1, label: "Cutoff", color: :cyan) { |v| dev.control_change(74, v.to_i) }
UI.knob(1, bank: 2, label: "Rev",    color: :blue) { |v| dev.control_change(91, v.to_i) }

UI.knob_send_all(bank: :all)   # 全バンクぶん初期送信
UI.knob_bank = 1               # 表示は A から
```

`UI.knob` のキーワード引数:

| 引数 | 既定 | 内容 |
|---|---|---|
| `label:` | `"Knob N"` | 表示ラベル（15 文字を超える分は表示時に切り詰め） |
| `color:` | `:gray` | ゲージ色。`UI.color_to_rgb565` の色名を共用 |
| `value:` | `min`（`origin: :center` なら中央） | 初期値。設定時にブロックは呼ばない |
| `min:` / `max:` | 0 / 127 | 値域 |
| `step:` | 1 | 量子化幅。0 で連続 |
| `origin:` | `:min` | `:min` か `:center` |
| `sensitivity:` | 1.0 | 1.0 で指の位置＝ゲージ先端 |

`cc:` や `device:` は**持たせない**。宛先も CC 番号もブロックの中にあるほうが、
「1 つのノブから 2 つの音源へ」「CC ではなく Pitch Bend へ」「値にカーブを
かける」がすべて素直に書けるため。

### 初期値の送信

`UI.knob` の `value:` はブロックを呼ばない（定義中に副作用を起こさないため）。
起動時に音源を画面と揃えたいなら、定義し終えてから明示的に:

```ruby
UI.knob(1, ...) { |v| ... }
UI.knob(2, ...) { |v| ... }
UI.knob_send_all              # 選択中のバンクぶん
UI.knob_send_all(bank: :all)  # 全バンクぶん
```

中央ナビの `[Send]` も同じことをする（C 側は全ノブに `final: true` の変更
イベントを積むだけで、実際に送るのはやはりブロック）。**送るのは選択中の
バンクだけ**。外部音源を後から繋いだときの再同期用。

### エンコーダからの書き込みとフィードバック

`UI.knob_set` は**ブロックを呼ぶ**。エンコーダで動かしたときも CC が出ないと
意味が無いため。呼び出しは PicoRuby タスク上で同期的に行う（`knob_set` を
呼んだその場でブロックが走る。キューを経由しない）。

ループにはならない。ブロックがエンコーダの LED リングを書き戻しても、次の
ポーリングで読める値は同じなので `knob_set` は「変化なし」で何もしないから。
**発火の条件が『量子化後の値が変わったとき』だけ**であることがここで効いている。

それでも切りたい場合（画面の表示だけ他の状態に追従させたい等）は
`notify: false`。

### イベント

`UI.knob` のブロックを使わず、Pads と同じ生イベントとしても受けられる:

```ruby
UI.on(:knob_change) do |e|
  # e => {type: :knob_change, bank: 1, index: 1, value: 64.0, final: false}
end

UI.on(:knob_bank) do |e|
  # e => {type: :knob_bank, bank: 2}
end
```

`final: true` は指を離した最後の 1 通。値の確定を待って重い処理をしたいとき用。
`:knob_bank` はストリップでの切り替えと `UI.knob_bank =` の両方で飛ぶ。

## ロータリーエンコーダから動かす

[examples/tombola.rb](../examples/tombola.rb) の `EncoderKnob` がすでに
「エンコーダ 1 台 = パラメータ 1 つ」の形になっているので、setter を
`UI.knob_set` に向けるだけで画面のノブと繋がる。

```ruby
require 'dfrobot_rotary_encoder'

i2c = I2C.new(unit: "ESP32_I2C0", sda_pin: 53, scl_pin: 54, frequency: 100_000)
enc = DFRobotRotaryEncoder.new(i2c: i2c, address: 0x54)
enc.gain = 51                       # 1 ディテント = LED 1 個

UI.knob(1, label: "Cutoff", color: :cyan) do |v|
  dev.control_change(74, v.to_i)
end
enc.value = (UI.knob_value(1) / 127.0 * 1023).to_i   # ← LED リングを実値に合わせる

# 指で動かしたぶんをエンコーダへ書き戻す（LED リングを実値に保つ）
UI.on(:knob_change) do |e|
  enc.value = (e[:value] / 127.0 * 1023).to_i if e[:index] == 1
end

loop do
  v = enc.value
  UI.knob_set(1, v * 127 / 1023) if v   # ← ここでブロックが呼ばれ CC が出る
  UI.process
  sleep_ms 10
end
```

- 画面のノブとエンコーダは**どちらも同じ値の別の窓**。どちらから動かしても
  ブロックが 1 回呼ばれる
- `bank:` を省いた `knob_set` / `knob_value` は選択中のバンクを指すので、
  **エンコーダは黙って表示中のバンクに追従する**。切り替えた直後はノブの値と
  エンコーダの物理位置がずれるため、`:knob_bank` で書き戻す:

```ruby
UI.on(:knob_bank) do
  enc.value = (UI.knob_value(1) / 127.0 * 1023).to_i
end
```
- エンコーダは 0〜1023 固定なので、ノブの min/max へのマップはスクリプト側で行う。
  C 側に持ち込むと「どのエンコーダがどのノブか」を C が知る必要が出てしまうため
- Tab5 の Port A (53/54) は SAM2695 の UART と同じピン。エンコーダと SAM2695 は
  同時に使えない（Tombola と同じ制約）

## C 側

### データ構造（`ui_common.h`）

```c
typedef enum {
    KNOB_ORIGIN_MIN = 0,              // ゲージは左下から伸びる
    KNOB_ORIGIN_CENTER,               // 12時から左右へ伸びる
} knob_origin_t;

typedef struct {
    bool     assigned;
    char     label[16];
    uint16_t color;                   // RGB565（pad_color_t と同じ値を使う）
    float    value, min, max, step, initial;
    float    sensitivity;
    uint8_t  origin;                  // knob_origin_t
    bool     notify;                  // Ruby にイベントを流すか（ブロック登録時）
} knob_config_t;

// バンク 1 は static、2〜4 は初回使用時に確保（NULL = 未使用）
extern knob_config_t  g_knob_bank0[UI_KNOB_COUNT];
extern knob_config_t *g_knob_banks[UI_KNOB_BANKS];
```

MIDI に関する項目（CC 番号・チャンネル・トランスポート）は**持たない**。

### API（`ui_common.h`）

バンクは全 API の第 1 引数（0 始まり。Ruby 側で 1 始まりへ直す）。

```c
void ui_knob_set_config(uint8_t bank, uint8_t index, const char* label,
                        uint16_t color, float min, float max, float step,
                        float value, uint8_t origin, float sensitivity,
                        bool notify);
void ui_knob_clear(uint8_t bank, uint8_t index);
void ui_knob_clear_all(void);            // 全バンク。確保した分は解放する

float ui_knob_get_value(uint8_t bank, uint8_t index);
// クランプ + 量子化。**実際に値が動いたときだけ** true を返し、そのときだけ
// dirty を立て、イベントを積む
bool  ui_knob_set_value(uint8_t bank, uint8_t index, float value, bool notify);
bool  ui_knob_reset(uint8_t bank, uint8_t index);
void  ui_knob_notify_all(uint8_t bank);  // [Send] の実体

void ui_knob_set_label(uint8_t bank, uint8_t index, const char* label);
void ui_knob_set_color(uint8_t bank, uint8_t index, uint16_t color);
// 未確保バンク / 範囲外は NULL（＝未割り当てとして描く）
const knob_config_t* ui_knob_get_config(uint8_t bank, uint8_t index);
bool ui_knob_bank_in_use(uint8_t bank);  // ストリップのグレー表示判定

uint8_t ui_knob_get_bank(void);
void    ui_knob_set_bank(uint8_t bank);  // イベントも積む。全面再描画を要求
```

再描画のマスクは **2 本**ある。値が動いただけなら扇形 1 枚で済むのに対し、
ラベルや色が変わるとセルごと描き直しになるので、混ぜると常に高いほうの
コストがかかってしまう:

```c
void     ui_knob_mark_dirty(uint8_t index);    // 値が動いた
uint32_t ui_knob_take_dirty(void);
void     ui_knob_mark_repaint(uint8_t index);  // 見た目が変わった
uint32_t ui_knob_take_repaint(void);
```

`min > max` で定義された場合は `ui_knob_set_config` が**入れ替えて**保持する。
ゲージは span で割り、ドラッグは両端でクランプするので、下流に符号の分岐を
持ち込まないため。

イベント側:

```c
UI_EVENT_KNOB_CHANGE,                 // ui_event_type_t の末尾に追加
UI_EVENT_KNOB_BANK,

struct {
    uint8_t bank;
    uint8_t index;
    bool    final;                    // 指を離した最後の1通
    float   value;
} knob;                               // ui_event_t の union に追加
```

`picoruby_ui_event_t`（[mrbgems/picoruby-ui/include/ui.h](../mrbgems/picoruby-ui/include/ui.h)）は
フラットな struct なので `knob_bank` / `knob_index` / `knob_final` / `knob_value`
を足してある。

「ノブごとに 1 通へまとめる」のは `ui_event_push()` ではなく専用の
`ui_knob_event_post()`。キュー内に同じノブの未処理イベントがあれば値を差し替え、
無ければ積む（`final` は一度立ったら下げない）。

Ruby 側のブロック保管はバンクを含めた**平坦なキー**
（`$ui_knob_callbacks[bank * UI::KNOB_KEY_STRIDE + index]`、stride = 16）。
mrubyc の Hash に配列キーを持たせるより素直で、`UI.knob_clear_all` での掃除も
1 行で済む。

### 画面インデックス

```c
typedef enum {
    UI_SCREEN_MAIN = 0,
    ...
    UI_SCREEN_TOMBOLA,     // 6
    UI_SCREEN_KNOBS,       // 7  ← 末尾に追加
} ui_screen_index_t;
#define UI_SCREEN_COUNT 8
```

Ruby 側 `UI::SCREEN_KNOBS = 7` を追加。**既存の値は絶対に振り直さない**
（SD カードのスクリプトが数値を焼き込んでいるため。`ui_common.h` のコメントの通り）。

### スクリプト停止時のリセット

Supervisor がスクリプトを止めるときに `reset_ui_state()` がパッド設定と
イベントキューをクリアしている（[docs/PICORUBY_SUPERVISOR.md](PICORUBY_SUPERVISOR.md)）。
ここに `ui_knob_clear_all()` を足す。ノブは音を出しっぱなしにしないので
All Sound Off のような後始末は不要だが、**前のスクリプトのラベルが残る**のは困る。

## 検証

実装後に確かめたのは以下。**実機での操作感はまだ試していない。**

- Tab5 / CoreS3 / Freenove の 3 ボードでビルド（Freenove は no-op スタブ経路）
- レイアウト不変条件をホスト側で総当たり検証（`ui_common.h` を直接 include）:
  セルが重ならない・コンテンツ領域とストリップの内側に収まる・リングと
  ハイライト枠とラベルがセルからはみ出さない・値テキストの消去矩形が内円に
  収まる（Tab5 で対角 46.6 < 52、CoreS3 で 15.5 < 23）・バンクボタン 4 個が
  コンテンツ高にちょうど収まる
- ドラッグの厳密性: 指の軌跡を 4000 分割して外積を積分し、`atan2` の端から端まで
  の差と比較。リム上・半径 2 倍・らせん・逆回しのいずれも一致（誤差 1e-7 rad
  台、らせんだけ離散化由来の 6.6e-4）。270° 掃くとちょうどフルレンジ動く

## 初版に入れないもの

- **バンクのスワイプ切り替え** / ストリップ上での上下ドラッグ
- **バンク名のカスタマイズ**（`UI.knob_bank_label(2, "Drums")`）。A/B/C/D 固定
- **長押しで `initial` へリセット**。押下時刻の保持と、ドラッグとの排他が要る
- **MIDI Learn**（外部から受けた CC でノブが動く / 割り当てが決まる）
- **値の NVS 保存**（電源を切っても残る）
- 指針（ポインタ線）とスムージング。ゲージの先端で足りるはず
- Freenove 用の代替 UI（no-op のまま）

## 実機で確かめたいこと

実装では素直な既定値を選んである。触ってみて違ったら変えるべきもの:

1. **`sensitivity` の既定 1.0**（指の位置 = ゲージ先端）。CoreS3 は R=30px で
   270° が短く、1 ステップ 2.1° ≒ 1.1px になる。細かすぎるようなら 0.5
   （540° でフルレンジ）へ
2. **セル内どこでも掴める**ようにしてある。隣のノブと密着するので、誤爆が
   多いようならリング外周 ±10px に絞る
3. **未割り当てノブをグレーで並べている**（Pads と同じ）。Tab5 で 1〜2 個しか
   使わないパッチだと寂しいので、割り当て済みだけを大きく並べる案もある
4. **バンクは 4 面**。CoreS3 のストリップは 4 ボタンでコンテンツ高 180px を
   ちょうど使い切るため、増やすならボタンを小さくするかスクロールが要る
5. **ストリップは右**。左のほうが押しやすい説と、右利きの指でグリッドを
   隠さない説がある
6. **中央ナビは `[Send]`**（現在値の再送信）。`[Reset]`（`initial` へ戻す）の
   ほうを使うなら入れ替える
