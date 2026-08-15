# XYPad 画面

指で触れた位置から音程（X）と CC（Y）を連続的に送る演奏面。
Pads がワンショット、Knobs が持続パラメータ、Tombola が
自動発音だとすると、XYPad は**指でその場で弾く**担当になる。

名前は `UI::XYPad` にした。

- 対応ボード: M5Stack CoreS3 / M5Stack Tab5（画面が必要）。Freenove は他の画面と
  同じく no-op スタブ（[main/platform/platform_freenove.c](../main/platform/platform_freenove.c)）
- 画面: [main/ui/screen_xypad.cpp](../main/ui/screen_xypad.cpp)（描画とタッチ）
- モデル: `ui_xypad_*`（[main/ui/ui_common.cpp](../main/ui/ui_common.cpp)）
- Ruby API: `UI::XYPad`（[mrbgems/picoruby-ui/mrblib/ui.rb](../mrbgems/picoruby-ui/mrblib/ui.rb)）
- サンプル: [examples/xypad.rb](../examples/xypad.rb)

## 実装状況（2026-08-15）

以下を除き、このドキュメント通りに実装済み。**実機での操作感はまだ試していない**
（ESP-IDF がこの環境に無く、ビルド確認もできていない。手元でのビルド/実機確認が必要）。

- スロットの空きが無いタッチを画面上に薄く出す、という案は見送った。
  何も描かず完全に無視する（Tombola の満杯時と同じ「静かに諦める」）
- 中央ナビの `Hold: ON/OFF` ステータス表示は入れていない。中央ナビは他の
  素の画面と同じくタイトルを表示するだけ
- スロットの当たり判定/描画にセル境界線・ノート名の背景は元から入れていない
  （前掲の設計変更どおり）

## 位置づけ

| | Pads | Knobs | Tombola | XYPad |
|---|---|---|---|---|
| 値 | bool | 連続値 1 個 | — (物理シム) | 連続値 2 個（X, Y）+ ゲート |
| 操作 | タップ | 円周ドラッグ | 多角形をドラッグ回転 | 平面をタッチ＆ドラッグ |
| MIDI を送るのは | Ruby のブロック | Ruby のブロック | C++ 側（既定） | Ruby のブロック（既定は内蔵ハンドラ） |
| 個数 | CoreS3 6 / Tab5 12 | 同 × 4 バンク | 同時 1 台（シングルトン） | 同時 1 台、**最大 5 スロット** |
| 同時発音 | — | — | ボール数ぶん | 最大 5（スロット数の上限） |
| スロットごとの独立設定 | — | ノブごとに独立（Knobs はもともとこの形） | ボールごとに一部独立 | **X/Y の意味づけ・送信先まで全部独立** |

Tombola と同じく **C++ 側の状態はグローバルなシングルトン**なので、XYPad も
同時に 1 つ。ただしその中身は Knobs のバンクに近く、**5 本の指それぞれに
対応する「スロット」が独立した設定を持つ**（後述）。`UI::XYPad.new` は
既定値へリセットしてから引数を全スロットへ適用する。

## なぜこの設計か（前提の整理）

この手の X-Y タッチパッドには大きく二系統ある。既存の音にエフェクトをかける
機材と、本体がオシレータを持つ音源として使う機材。Midori はどちらでもなく
**MIDI を送るだけ**なので、両方とも実装上は同じもの（タッチの on/off + X/Y
2 軸の連続値）に収束する。違うのは軸の意味づけ（音階にスナップさせるか、
生の CC か）だけなので、**X・Y それぞれ独立にモードを選べる**ようにする。
Y は最初から CC 専用（Knobs と同じ発想）だったが、X も `x_mode: :cc` で
同じ扱いにできる（`:note`, 既定）。「2 軸とも生の CC、音は固定ノートで
ゲートするだけ」というエフェクトコントローラ的な使い方と、「X は音階、
Y は CC」という音源的な使い方の両方を、同じ画面・同じ内蔵ハンドラでカバーする。

さらに、**その選択は指ごとに違ってよい**。5 本のタッチを「同じ楽器の5音」
として使うだけでなく、「1 本目は鍵盤、2 本目はフィルターの CC コントローラ、
3 本目は別の音源へ」のように、**指 1 本 1 本を別のミニ XYPad として** 使える
ようにする。これが今回の主眼で、チャンネルだけを指ごとに変えられる旧設計
（[下記「指ごとの独立性について」](#指ごとの独立性についてなぜチャンネルだけでは足りなかったか)参照）
では表現力が足りなかった。

`x_mode: :note`（既定）で採用したのは「着地はスケールにスナップ、そこからの
スライドはピッチベンドで滑らかに」という ROLI Seaboard の Glide に近い挙動
（[examples/seaboard_blocks.rb](../examples/seaboard_blocks.rb) が MPE 入力
として同じ語彙を扱っている）。タッチダウンの瞬間に音を外さず、かつ理論値
だけのカクカクしたスナップにもならない。

### 指ごとの独立性について（なぜチャンネルだけでは足りなかったか）

最初の設計では「5 本の指 = チャンネルプールから 1 本ずつ割り当てるだけ」
だった。しかし実際に欲しいのは、チャンネル以外の**すべて**（スケールか CC か、
どの CC 番号か、どの音源に送るか、Hold するか）も指ごとに変えられることだった。
そこで内部モデルを Knobs に近づけ、**「スロット」という、指が触れるたびに
中身が入れ替わる独立したミニ設定単位**を 5 個（既定）用意した。指 1 本の
挙動を変えたければ、そのスロットだけ設定すればよい。

## スロットという単位

- 最大 5 個（`max_touches` で調整、ハード上限は `MAX_TOUCH_POINTS` = 5）
- 各スロットは **x_mode・scale・glide_range・x_cc/x_range・y_cc/y_range・
  y_invert・note（ゲート用固定ノート）・velocity・channel・hold・device・
  auto_midi** をすべて独立に持つ。実質「ノブ 1 個ぶんの設定」を X・Y 2 軸に
  拡張したものだと思うとよい
- `UI::XYPad.new(**opts)` は `opts` を**全スロットへ同じ内容で適用**する
  （今まで通りの「5 本とも同じ楽器」という使い方がそのまま既定になる）
- `pad.slot(n, **opts)` は**スロット `n` だけ**を上書きする（Tombola の
  `add_ball(note:, channel:, ...)` が既定を個別に上書きできるのと同じ発想）
- タッチダウンのたびに、**空いている最小番号のスロット**が割り当てられる
  （物理的な指の同一性ではなく「今何本目に触れたか」で決まる。旧設計の
  チャンネルプールと同じ割り当て規則だが、対象がチャンネル 1 個から
  スロット全体に広がった）
- 空きスロットが無いタッチは無視される（Tombola の `add_ball` が満杯で
  `-1` を返すのと同じ「静かに諦める」方針）

## 構成

Knobs と同じ役割分担：**C++ 側はタッチ追跡・スケールへのスナップ・スロット
割り当てまでを持ち、実際に MIDI を送るのは Ruby**。Tombola（C++ 側が発音）とは
逆の判断だが理由も同じで、Tombola の問題は物理衝突のジッタだが、XYPad の
CC/ベンドは「なめらかに追従すればよい」ので `UI.process` を 5〜10ms で回して
いれば遅延は体感できない。

```
 [PicoRuby task]                          [UI task (app_main, 10ms周期)]
  UI::XYPad.new(**opts)                    ui_update()
    └ 全スロットへ opts を適用
  pad.slot(3, x_mode: :cc, ...)            ScreenXYPad::onTouch/onTouchMove/onTouchUp
    └ UI._xypad_set_slot() ──→ g_xypad[3] ──→ ├ 空きスロットを割り当て（タッチダウン）
                                              ├ そのスロットの x_mode で分岐
                                              │   :note → X をスケールへスナップ→着地音確定、
                                              │           以後は bend_semitones
                                              │   :cc   → note は固定値、X は絶対座標 → x
                                              ├ Y: 絶対座標 → y（そのスロットの y_min〜y_max）
                                              └ ui_event_push()  ← down/up は必ず、moveは間引き
  UI.process ←──────────────────────────── イベントキュー
    └ 内蔵ハンドラ（既定 ON）
         └ device.note_on / pitch_bend / control_change / note_off  ← MIDI はここ
                                                                       送信先・CC 番号・
                                                                       ベロシティはスロットの
                                                                       設定を見て決める
    └ pad.on_touch ブロック（任意）        ← 生イベントを見たい／自分で送りたいとき
```

Tombola の `sound` / `on_hit` / `device` の三点セットと対応させてある:

| Tombola | XYPad | 役割 |
|---|---|---|
| `device=` | スロットごとの `device:` | MIDI の宛先（スロットごとに違ってよい） |
| `sound=` (既定 true) | スロットごとの `auto_midi:`（既定 true） | 内蔵の送信を行うか |
| `on_hit` | `on_touch` | 生イベントを Ruby でも受け取る（記録・カスタム送信用。全スロット共通の 1 個） |

違いは発音の主体だけ：Tombola は C++ が鳴らし Ruby は観測するだけだが、XYPad は
**内蔵ハンドラも Ruby 側**にある（`ui.rb` の中で完結する）。C++ は一切 MIDI を
知らない。

## Ruby API（想定）

### 基本形：5 本とも同じ楽器

```ruby
require 'midi'
require 'ui'

dev = MIDI::Device.new(MIDIDevices.sam2695)

pad = UI::XYPad.new(
  x_mode: :note,           # 既定。省略可
  scale: [36, 38, 40, 41, 43, 45, 47, 48],  # X 着地点のスナップ先（Tombola と同じ規約）
  glide_range: 2,          # 全幅スライドで何半音ベンドするか（既定 2 = GM 標準、RPN 不要）
  y_cc: 74,                 # Y 軸が送る CC 番号
  y_range: 0..127,          # Y の値域
  channel_base: 0,          # スロット 1 のチャンネル既定値。以降 +1（スロットごとに上書き可）
  max_touches: 5,           # スロット数（ハード上限が5）
  hold: false,
  device: dev
)
pad.show

sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  UI.process
  sleep_ms 5
end
```

ここまでは旧設計（チャンネルだけがスロットごとに違う）と体感は同じ。
`opts` が 5 スロットすべてに同じ内容で適用されているだけ。

### 指ごとに別の楽器にする

```ruby
fx = MIDI::Device.new(MIDIDevices.usb_midi_device)

# 1本目・2本目：既定のまま（scale + glide、SAM2695、CC74）
# 3本目：エフェクトボックスへの生 CC コントローラに変える
pad.slot(3,
  x_mode: :cc, x_cc: 70, x_range: 0..127,
  y_cc: 71,    y_range: 0..127,
  note: 60, velocity: 100,
  channel: 5,
  device: fx
)

# 5本目：常にラッチ（Hold ボタンを使わずこの指だけドローン的に）
pad.slot(5, hold: true)

# 生イベントはどのスロットで起きたかが t[:slot] でわかる
pad.on_touch do |t|
  # t => {slot: 3, phase: :down, channel: 5, note: 60, x: 40.0, y: 64.0}
  UI.log("slot #{t[:slot]} #{t[:phase]}")
end
```

`pad.slot(n)`（キーワード引数なし）は、そのスロットの現在の設定をハッシュで
返す（別スロットへコピーする、現在値を確認する、といった用途）。

内蔵ハンドラを使わず完全に自前で送りたい場合は、そのスロットだけ
`auto_midi: false` にして `on_touch` で組む（Tombola の `sound = false` +
`on_hit` と同じ形。スロット単位で `auto_midi` を切れるので、一部の指だけ
自前送信にすることもできる）。

`scale:` / `glide_range:` はそのスロットの `x_mode` が `:cc` のとき無視される。
ゲート（note_on/note_off）自体はどちらのモードでも変わらず、鳴る音が
「X で決まる」か「固定」かだけが違う。

### パラメータ

`UI::XYPad.new(**opts)` と `pad.slot(n, **opts)` は同じキーワードを受け取る。
前者は全スロットへ、後者は指定したスロット 1 個だけへ適用される。

| メソッド | 既定値 | 内容 |
|---|---|---|
| `x_mode` | `:note` | `:note`（スケール+グライド）か `:cc`（生の CC。Y と同じ扱い） |
| `scale` | `[36,38,40,41,43,45,47,48]` | `x_mode: :note` のとき、X 軸の着地点。最大 16 音（Tombola と同じ上限） |
| `glide_range` | 2.0 | `x_mode: :note` のとき、全幅スライドで何半音ベンドするか。変更するとそのスロットのチャンネルへ RPN でベンドレンジを再設定する |
| `x_cc` | 70 | `x_mode: :cc` のとき、内蔵ハンドラが X を送る CC 番号 |
| `x_range` | 0..127 | `x_mode: :cc` のときの X の値域 |
| `note` | 60 | `x_mode: :cc` のとき、ゲートとして送る固定ノート。`x_mode: :note` では無視（着地音が使われる） |
| `y_cc` | 74 | 内蔵ハンドラが Y を送る CC 番号 |
| `y_range` | 0..127 | Y の値域（`min..max`。Knobs の `min:`/`max:` と同じ発想） |
| `y_invert` | false | true でタッチ座標の上下を反転（既定は上が `y_range` の大きい方） |
| `velocity` | 100 | note_on のベロシティ（タッチの強さは取れないため固定値） |
| `channel` | スロット番号 - 1 + `channel_base` | そのスロットが使う MIDI チャンネル |
| `hold` | false | true の間にこのスロットの指を離すと、鳴りっぱなしになる |
| `auto_midi` | true | このスロットの内蔵ハンドラで MIDI を送るか |
| `device` | — | このスロットの内蔵ハンドラの送信先 |

`UI::XYPad.new` だけが取るパド全体のオプション:

| メソッド | 既定値 | 内容 |
|---|---|---|
| `max_touches` | 5 | スロット数。`MAX_TOUCH_POINTS`（5）が実質上限 |
| `channel_base` | 0 | `channel` の既定値を計算する基準（スロット `n` の既定は `channel_base + n - 1`） |

`x_mode` を実行中に切り替えることもできる。切り替えた瞬間に既にそのスロットが
タッチ中の場合、そのタッチは離すまで**切り替え前のモードのまま**進行する
（着地音の有無が宙に浮かないようにするため）。次にそのスロットへ触れたときから
新しいモードが適用される。

`hold` はパッドやトグルノブに割り当てるのが本来の使い方（Tombola の
`rotation` などと同じ）。**引数無しで代入すると全スロットへブロードキャスト**
される（旧設計と同じ書き味を保つため）:

```ruby
UI.pad(1, label: "Hold", type: :toggle) { |on| pad.hold = on }        # 全スロット
UI.pad(2, label: "Hold 5", type: :toggle) { |on| pad.slot(5, hold: on) } # 5本目だけ
```

## タッチ操作

### X（`x_mode: :note`、既定）: スケールへのスナップ＋グライド

タッチダウンの瞬間、X 座標をコンテンツ幅で `scale.size` 等分したセルに
割り当て、そのセルの `scale[i]` を**着地音**として即座に `note_on`。以後
指を動かしても着地音は変わらない（ノートの再スナップはしない）。`scale` は
そのタッチが割り当てられた**スロット**のものが使われる。

指を動かした量は、タッチダウン地点からの**相対 X 距離**として測り、
コンテンツ幅いっぱいのスライドが `glide_range` 半音になるよう線形に
マップして `bend_semitones` を更新する。両端で `±glide_range` にクランプ
（Knobs の値クランプと同じ「巻き上がりが無い」動作）。

```c
bend_semitones = clamp((x - touchdown_x) / content_width * glide_range,
                        -glide_range, glide_range);
```

**タッチダウンからの相対値**であって絶対位置ではない点が Y 軸と違う。
どのセルで指を下ろしても、そこからのスライド量は同じだけベンドする
（実物の鍵盤 + ピッチベンドホイールと同じ理屈）。

`bend_semitones` を実際の 14bit pitch bend 値に変換するのは Ruby 側
（`MIDI::Device#pitch_bend` は生の ±8192 単位を取る）:

```ruby
raw = (bend_semitones / glide_range * 8192).to_i.clamp(-8192, 8191)
dev.pitch_bend(raw, channel: ch)
```

内蔵ハンドラはこれを自動でやる。`on_touch` で自分から送る場合はこの変換が
必要になる。

### X（`x_mode: :cc`）/ Y: 絶対位置 → CC

`x_mode: :cc` のときの X は、Y とまったく同じ扱いになる。タッチダウン相対
ではなく**そのときのタッチの絶対座標**を毎回 `x_range`（または `y_range`）
へ線形マップする（Knobs の値そのもの、CC への変換は Ruby 任せという発想も
同じ）。タッチダウンの記憶やチャンネルごとの RPN 設定も要らない
（ベンドを送らないので）。

ゲート（note_on/note_off）は `note:` の固定ノートで代替する。タッチダウンで
`note_on(note, velocity)`、リリースで `note_off(note)`。「音は鳴らさず 2 つの
CC だけ動かしたい」場合は `auto_midi: false` にして `on_touch` から
`control_change` だけ送ればよい（note_on/off を送るかどうかも自分で決められる）。

この手のパッドは「上が値大」の配置が多いので Y の既定もそれに合わせる
（`y_invert` で反転可）。X は左が小さい値という素直な向きが既定
（反転が要るなら同様に `x_invert` を足す想定）。

### マルチタッチ＝スロット割り当て

`bend_semitones` はチャンネル単位（MIDI Pitch Bend はチャンネル単位のメッセージ）
なので、複数指を同時にグライドさせるには**指ごとに別チャンネル**が要る。
スロットはそもそも指ごとに独立した設定を持つので、これは特別扱いではなく
「各スロットが自分の `channel` を使う」という当たり前の帰結になる。

- タッチダウンのたびに、空いている最小番号のスロットが割り当てられる
  （物理的な `touchId` そのものではない。`touchId` は再利用時に飛び飛びに
  なりうるため、詰めて使えたほうがスロット消費が少ない）
- 埋まっている状態で新しいタッチが来た場合は**無視する**（画面上には
  触れた点を薄く出すが音は出ない）
- リリース（または Hold 解除）でスロットが空く
- `x_mode: :note` のスロットのみ、その `glide_range` を初めて使うとき・
  変更したときに、そのスロットの `channel` へ RPN でベンドレンジを送る
  （`examples/seaboard_blocks.rb` の `setup_pb_range` と同じ手順を内蔵
  ハンドラ側に持つ）。`x_mode: :cc` のスロットではベンドを送らないので不要
- **2 つのスロットに同じ `channel` を割り当てるのはスクリプトの自由**だが、
  両方が `:note` で `glide_range` が異なる場合、RPN の送信が競合する
  （後勝ち）。意図的に共有する場合を除き、スロットごとに別チャンネルを
  使うのが無難

Y（および `x_mode: :cc` のときの X）の CC も同じチャンネルへ送るので、
**和音の中の 1 音だけフィルターを開ける**といった表現がチャンネルを介して
自然にできる（Seaboard の Slide（CC74）が per-note になるのと同じ効果）。

### Hold

指を離しても音や CC を保持できる、スロット単位のラッチ:

- `hold = true` の**間に**そのスロットの指を離すと、note_off / bend リセット
  を送らず、スロットを握ったまま鳴り続ける（“latched”）
- 既に latched の状態で `hold = false` にすると、そのスロットを note_off +
  bend センターへ戻し、空ける
- `hold` を切り替えた瞬間にまだ指を置いているタッチには何もしない
  （その指が離れたときの hold 状態で決まる）
- 新しいタッチがスロットの空きを必要とするとき、latched なスロットは
  「使用中」として数える（明示的に離す＝Hold を切るまで再利用しない）
- `pad.hold = ` （引数無し代入）は全スロットへの一括設定。`pad.slot(n, hold:)`
  で個別のスロットだけ固定することもできる

画面内に専用の Hold ボタンは v1 では置かない（後述）。パッドかトグルノブに
割り当てるのがスクリプト側の仕事。

### 当たり判定

コンテンツ領域全体が XYPad の当たり判定（Tombola やナビ以外の Pads の
セルと違い、余白なくフル領域を使う）。`Screen::onTouchMove()` は Tombola /
Knobs で既に汎用化してあるものをそのまま使う。「離した時のイベントは
押し始めた場所で判定する」修正も両画面と共有しているので、ナビバーまで
指が流れても意図せず取り残されることはない。

## 画面レイアウト

指ごとに `scale` や `x_mode` が違いうる以上、背景に「音階レーン」を固定で
描くのは意味を持たない（スロット 1 と 3 で違う音階を使っていたら、どちらの
レーンを描けばよいか決められない）。そこで背景はプレーンな XY 領域のみとし、
**アクティブな各タッチの近くに、そのタッチが今どう解釈されているかを短い
テキストで添える**。

### M5Stack CoreS3（320x240、コンテンツ 180px 高）

```
┌────────────────────────────────────────┐
│  XYPad                        [MIDI●] │
├────────────────────────────────────────┤
│                                        │
│      ●E4                              │ ← スロット1: :note、着地音を表示
│           ○C4                         │ ← Hold で鳴りっぱなし（輪郭のみ）
│                    ●CC70:82           │ ← スロット3: :cc、X の値を表示
│                                        │
├────────────────────────────────────────┤
│   [◀]                              [▶]  │ ← 中央ナビはステータス表示のみ
└────────────────────────────────────────┘
```

### M5Stack Tab5（1280x720、コンテンツ 620px 高）

寸法が大きいだけでレイアウトは同じ。ラベルがより見やすくなる。

### 描画要素

| 要素 | 内容 |
|---|---|
| アクティブタッチ | 塗りつぶし円。色はスロット順（Tombola のボール色パレットを流用） |
| タッチのラベル | 円のそばに小さく表示。`:note` は着地音の音名（`E4`）、`:cc` は
  `CC<番号>:<値>`（X が cc モードのときのみ。Y は省略して画面が煩雑になるのを防ぐ） |
| ベンドの向き | 円から着地点の中央へ向かう短い線（`:note` のみ、動いた分だけ伸びる） |
| Hold 中の点 | 輪郭だけの円（塗りつぶさない）。指が既に離れていることを示す |

Knobs / Tombola と同じく、値が変わった範囲だけ差分描画する（動くたびに
描き直すのは各タッチの円・ラベル・ベンド線だけ）。Tab5 では描画の最後に
`M5.Lcd.display()` を呼ぶのを忘れないこと（[docs/TOMBOLA.md](TOMBOLA.md)
参照。Knobs 画面で一度これを落として画面が乱れた実績がある）。

## 値の配送とキューの扱い

Knobs と同じ「スロットごとに高々 1 通」だが、**down / up は間引かない**。
down/up はゲート（note_on/note_off）の根拠になる遷移なので、取りこぼすと
音が鳴りっぱなし・出ないままになる。move だけを間引く:

- 同じスロットの未処理イベントが move なら、新しい move で値を差し替える
  （Knobs と同じ）
- down / up は差し替えず、常に別エントリとしてキューに積む
- 最悪ケース（5 スロット同時 down → 5 スロット同時 up が 1 poll 間隔に収まる）
  でもキューは `MAX_TOUCH_POINTS * 2 + MAX_TOUCH_POINTS`（down/up 分 +
  move 1枠分）程度で足りる

`UI.process` を回さない限り何も送られないのは他の画面と同じ制約。
**ポーリングは 5〜10ms を推奨**（Knobs と同じ理由：DIN MIDI 1 メッセージ
約 1ms、`sleep_ms 100` では音がカクつく）。

## C 側（想定）

### データ構造

スロットは「設定」と「今の状態」を両方持つ 1 個の struct にまとめる
（Knobs の `knob_config_t` が値も設定も一緒くたに持つのと同じ発想）。

```c
#define UI_XYPAD_MAX_SCALE   16    // Tombola と同じ上限

typedef enum {
    XYPAD_XMODE_NOTE = 0,      // X: スケールへスナップ + グライド
    XYPAD_XMODE_CC,            // X: Y と同じ絶対値 CC
} xypad_xmode_t;

typedef struct {
    // --- 設定（pad.slot() が書く） ---
    uint8_t  x_mode;             // xypad_xmode_t
    uint8_t  scale[UI_XYPAD_MAX_SCALE];
    uint8_t  scale_len;
    float    glide_range;
    float    x_min, x_max;       // :cc のときの X の値域
    float    y_min, y_max;
    bool     y_invert;
    uint8_t  gate_note;          // :cc のときのゲート用固定ノート
    uint8_t  channel;
    bool     hold;

    // --- 実行時状態（タッチのライフサイクルで変わる） ---
    bool     active;             // 指がまだ触れている
    bool     latched;            // Hold で鳴りっぱなし（active は false）
    int      touch_id;           // 割り当て中の touchId。空きスロットは -1
    uint8_t  note;                // 鳴っているノート（:note は着地音、:cc は gate_note の写し）
    float    bend_semitones;     // -glide_range .. +glide_range（:note のみ使用）
    float    x;                   // x_min .. x_max（:cc のみ使用）
    float    y;                   // y_min .. y_max
    int16_t  touchdown_x;         // 相対ベンド計算用（:note のみ使用）
} xypad_slot_t;

extern xypad_slot_t g_xypad_slots[MAX_TOUCH_POINTS];
extern uint8_t      g_xypad_max_touches;   // 有効なスロット数 (<= MAX_TOUCH_POINTS)
```

MIDI に関する項目（CC 番号・ベロシティ・トランスポート）は Knobs と同じ理由で
**持たせない**。`x_cc` / `y_cc` / `velocity` / `device` / `auto_midi` は Ruby 側
（内蔵ハンドラ、スロットごとに Ruby 側の配列で保持）だけが知っている。C 側が
持つのは値域（`x_min/x_max`、`y_min/y_max`）と、ゲートに使う固定ノート
（`gate_note`）、チャンネル、Hold のフラグまで。

### API

```c
void  ui_xypad_reset(void);              // 全スロットを既定へ、進行中のタッチも解除
void  ui_xypad_set_max_touches(uint8_t n);

// スロット単位の設定。index は 0 始まり
void  ui_xypad_set_slot_scale(uint8_t index, const uint8_t* notes, uint8_t len);
void  ui_xypad_set_slot_f(uint8_t index, xypad_param_t param, float value);
void  ui_xypad_set_slot_i(uint8_t index, xypad_param_t param, int value);
float ui_xypad_get_slot_f(uint8_t index, xypad_param_t param);
int   ui_xypad_get_slot_i(uint8_t index, xypad_param_t param);

// Screen 側から呼ばれる。空きスロットを探すのもここ
void  ui_xypad_touch_down(int touch_id, int x, int y);
void  ui_xypad_touch_move(int touch_id, int x, int y);
void  ui_xypad_touch_up(int touch_id);
```

イベント側:

```c
UI_EVENT_XYPAD_TOUCH,   // ui_event_type_t の末尾に追加

typedef enum {
    XYPAD_PHASE_DOWN = 0,
    XYPAD_PHASE_MOVE,
    XYPAD_PHASE_UP,
} xypad_phase_t;

struct {
    uint8_t slot;               // 0-4
    uint8_t phase;              // xypad_phase_t
    uint8_t channel;
    uint8_t note;
    float   bend_semitones;     // x_mode: :note のときだけ意味を持つ
    float   x;                   // x_mode: :cc のときだけ意味を持つ
    float   y;
} xypad;                        // ui_event_t の union に追加
```

`picoruby_ui_event_t` はフラットな struct なので `xypad_slot` /
`xypad_phase` / `xypad_channel` / `xypad_note` / `xypad_bend` / `xypad_x` /
`xypad_y` を足す（Knobs の `knob_*` フィールドと同じやり方）。Ruby 側は
そのスロットの `x_mode` を知っているので、イベントを受けたら
`bend_semitones` と `x` のどちらを見ればよいか判断できる。

キューへの積み方は専用の `ui_xypad_event_post()`：`phase == MOVE` なら
その slot の未処理 move を上書き、`DOWN`/`UP` は常に新規に積む
（前述の「down/up は間引かない」の実装）。

### 画面インデックス

```c
typedef enum {
    UI_SCREEN_MAIN = 0,
    ...
    UI_SCREEN_TOMBOLA,     // 6
    UI_SCREEN_KNOBS,       // 7
    UI_SCREEN_XYPAD,       // 8  ← 末尾に追加
} ui_screen_index_t;
#define UI_SCREEN_COUNT 9
```

Ruby 側 `UI::SCREEN_XYPAD = 8` を追加。既存の番号は振り直さない
（SD カードのスクリプトが数値を焼き込んでいるため）。

### スクリプト停止時のリセット

Supervisor の `reset_ui_state()`（[docs/PICORUBY_SUPERVISOR.md](PICORUBY_SUPERVISOR.md)）に
`ui_xypad_reset()` を足す。Hold で latched なノートが次のスクリプトへ
持ち越されるのはまずいので、**リセット時に latched な全スロットの
チャンネルへ note_off を送る**必要がある（唯一 C 側が MIDI に触れる例外に
なる。All Sound Off で代替できるならそちらでもよい — Supervisor の既存
クリーンアップと合わせて検討）。

## 初版に入れないもの

- **画面内蔵の Hold ボタン**。パッド/ノブに外から割り当てる運用で様子を見る
- **`x_mode: :note` 内でのスナップ方式切り替え**（完全再トリガー版）。今回は
  グライド一本化。`x_mode`（`:note`/`:cc`）とは別軸の話で、必要になったら
  `x_note_mode: :snap` のような形で足す
- **`x_invert`**（`:cc` のときの X 反転）。要望が出たら `y_invert` と対にする
- **タッチの強さ（圧力）を velocity や別 CC に使う**。M5Stack の静電容量
  タッチはそもそも圧力を取れない
- **ゲート/リトリガーレート**（一定間隔で再トリガーするリズムスライス機能）。
  C++ 側にタイマー駆動のロジックが要り、Tombola の `retrigger_ms` に近い
  ボリュームの実装になる
- **タッチの軌跡を録音してループ再生する**。別画面・別ドキュメントで
  検討する規模
- **MPE 準拠の厳密な実装**（MPE Zone の Manager Channel、Configuration
  Message 等）。ここでの「指ごとにチャンネル」は MPE に似せているだけで、
  MPE 対応シンセ以外にも刺さるようベロシティ・CC は普通のチャンネル
  ボイスメッセージのまま
- **タッチと物理的な指の同一性の保証**。スロットは「今何本目に触れたか」で
  決まるので、5 本のうち途中の 1 本だけ離して同じ場所に触れ直すと、別の
  スロット（別の設定）に化ける可能性がある。気になる場合はスクリプト側で
  `touch_id` に相当する情報を `on_touch` から拾って自前管理する
- Freenove 用の代替 UI（no-op のまま）

## 実機で確かめたいこと

Knobs のときと同様、決め打ちで進めて触ってから直すべきものを先に挙げておく:

1. **`glide_range` の既定 2 半音**。Seaboard の例では ±48 まで使っていた
   （Slide の表現力を優先する機材だから）。XYPad は「音を外さない」が
   主眼なので控えめにしてあるが、狭すぎて表情が付かない可能性がある
2. **スロットが「今何本目に触れたか」で決まる**という設計そのもの。実際に
   使ってみて、指を 1 本ずつ出し入れする奏法だと「さっきまで鍵盤だった
   スロットが、離してまた触れたら CC コントローラになっている」ような
   混乱が起きるかもしれない。その場合は「最後に触れていた物理位置に近い
   スロットを優先する」といった別の割り当て規則を検討する
3. **X のグライドが「タッチダウンからの相対」**という設計そのもの。実際に
   触ってみて「絶対位置に対する連続ベンド」（着地音の概念を捨てて
   theremin 的にする）のほうが気持ちよい可能性はある
4. **Y の絶対マッピング**とチャンネルを共有しての per-note CC が、和音を
   弾いたときに音源側でどう聞こえるか（音源が CC74 を per-voice で
   解釈するかは音源依存）
5. **Hold の「離したときの状態で決まる」ルール**が直感的か。実機のように
   「Hold を押した瞬間に触れているものを即座にラッチ」のほうが自然かも
   しれない
6. **`x_mode: :cc` の固定ゲートノート**という設計そのもの。エフェクト
   コントローラとして使う場合はそもそもノートという概念が要らないので、
   「触れるたびに同じ音が鳴る」のが冗長に感じられる場合、`note: nil` で
   note_on/off 自体を送らず CC 2 本だけにするモードを足す可能性がある
7. **背景に音階レーンを描かないことで見た目の手がかりが減った**点。5 スロット
   とも同じ `scale` を使う一番よくある使い方では、以前の固定レーン表示の
   ほうが「どこを押せばどの音か」が事前にわかって弾きやすかった可能性が
   ある。全スロット同一設定を検出して、そのときだけレーンを描く、という
   折衷案もありうる
