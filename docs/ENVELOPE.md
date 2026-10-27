# Envelope（UI::Envelope / ADSR エディタ）

ADSR エンベロープの形を画面に描き、その折れ点を指でつまんで動かして編集する部品。
AMY の `AMY::Synth`（アンプ / フィルタのエンベロープ）や `AMY::FM` のオペレータの
エンベロープを、ノブ 4 個を並べる代わりに「形を見ながら」いじるためのもの。

- 状態: 実装済み（2026-10-27）。ビルドとホストテストのみで**実機は未確認**
- 置き場所: **レイアウト DSL の部品**（`envelope`）。専用画面は持たない
  （1 枚の画面にエンベロープだけ置きたいときも、1 セルのレイアウトにする）
- 対応ボード: CoreS3 / Tab5 / CrowPanel（UI のあるボード）。Freenove はレイアウト自体が
  no-op なので、定義はできるが表示されない（値は Ruby 側に残る）
- 実装:
  - Ruby: [mrbgems/picoruby-ui/mrblib/ui_layout.rb](../mrbgems/picoruby-ui/mrblib/ui_layout.rb)
    （`Builder#envelope` と `UI::Envelope`）
  - 値の保持: [main/ui/ui_layout.cpp](../main/ui/ui_layout.cpp)（`ui_layout_add_envelope` / `ui_layout_envelope_*`）
  - 描画と座標の計算: [main/ui/ui_widgets.cpp](../main/ui/ui_widgets.cpp)（`ui_env_*`）
  - タッチ: [main/ui/screen_layout.cpp](../main/ui/screen_layout.cpp)
- テスト: `ruby test/layout_test.rb`（picoruby-ui ディレクトリで。配置と値の扱い）
- サンプル: [examples/envelope.rb](../examples/envelope.rb)
- 関連: [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md) / [KNOBS.md](KNOBS.md) / [AMY_SYNTH.md](AMY_SYNTH.md) /
  [mrbgems/picoruby-amy/README.md](../mrbgems/picoruby-amy/README.md)

## 1. 位置づけ

| | Knob | Envelope |
|---|---|---|
| 値 | float 1 個 | attack / decay / sustain / release の 4 個 |
| 操作 | ノブの周りをなぞる | 折れ点（ハンドル）をつまんでドラッグ |
| 置き場所 | Knobs 画面 / レイアウト | レイアウトのみ |
| 1 レイアウトに | いくつでも | いくつでも（アンプ用とフィルタ用を並べられる） |
| 送るのは | Ruby のブロック | Ruby のブロック、または `to:` で渡したシンセ |

Knob と同じく **C++ は値を持って絵を描き、変化を通知するだけ**で、AMY にも MIDI にも
触らない。エンベロープを AMY へ送るのは Ruby（`AMY::Synth#amp_envelope` など）。
AMY のエンベロープはブレークポイントの列を丸ごと送る API なので、Ruby で組み立てる
いまの経路をそのまま使う。

XYPad / Tombola / Keys のような「画面オブジェクトを借りる」部品（ビュー）ではなく、
pad / knob と同じ**レイアウトの中に設定を持つ部品**にした。モデルが部品ごとにあるので、
1 つのレイアウトに何個でも置ける。

## 2. Ruby API

### 2.1 レイアウトに置く

```ruby
require 'amy'
syn = AMY::Synth.new(channel: 1)

UI.layout("Synth", cols: 4, rows: 2) do
  envelope col: 0..1, row: 0, label: "Amp",    color: :green, to: syn, env: :amp
  envelope col: 2..3, row: 0, label: "Filter", color: :cyan,  to: syn, env: :filter
  knob col: 0, row: 1, label: "Cutoff" do |v| syn.cutoff = AMY.scale(v, 50, 8000, log: true) end
  # ...
end.show
```

```ruby
envelope col:, row:, area:, id:,
         label: "Env", color: :cyan,
         attack: nil, decay: nil, sustain: nil, release: nil,
         max_time: 2000,
         to: nil, env: nil do |adsr| ... end
```

| 引数 | 意味 |
|---|---|
| `col:` / `row:` / `area:` / `id:` | 他の部品と同じ（[UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md)） |
| `label:` | 左上に出す名前 |
| `color:` | 線と塗りの色（Symbol / RGB565） |
| `attack:` / `decay:` / `release:` | ms（0〜`max_time`） |
| `sustain:` | レベル 0.0〜1.0 |
| `max_time:` | A / D / R の各区間を最大まで伸ばしたときの時間（ms）。既定 2000 |
| `to:` | 値を送る先（後述）。初期値もここから読む |
| `env:` | `to:` のどのエンベロープか（`:amp` / `:filter`）。省略すると `to.envelope` |
| ブロック | 値が変わるたびに `{ attack:, decay:, sustain:, release: }` の Hash で呼ばれる |

初期値は「引数 → `to:` から読んだ値 → 既定 `[10, 300, 0.7, 300]`」の順に決まる。

`to:` は次の約束を満たすオブジェクトなら何でもよい（AMY 専用ではない）：

- `env: :amp` なら `to.amp_envelope(attack:, decay:, sustain:, release:)` で送り、
  `to.amp_attack` / `amp_decay` / `amp_sustain` / `amp_release` で読む
- `env:` なしなら `to.envelope(...)` で送り、`to.attack` / `decay` / `sustain` / `release` で読む

| 送り先 | 書き方 |
|---|---|
| `AMY::Synth` のアンプ EG | `to: syn, env: :amp` |
| `AMY::Synth` のフィルタ EG | `to: syn, env: :filter` |
| `AMY::FM` のオペレータ 1 | `to: fm.op(1)` |

`to:` とブロックの両方を渡すと、`to:` へ送ったあとにブロックを呼ぶ（表示の更新や
他の値の連動に使う）。

定義しただけでは何も送らない（Knob と同じ）。初期値を送るなら `env.send_value` か
`layout.send_all`。

### 2.2 ハンドル（UI::Envelope）

`envelope` は `UI::Envelope` を返す（`id:` を付ければ `layout[:amp]` でも取れる）。

```ruby
env = layout[:amp]
env.attack            # => 10.0   （decay / sustain / release も）
env.values            # => { attack: 10.0, decay: 300.0, sustain: 0.7, release: 300.0 }
env.attack = 50       # 画面を更新し、変わったら to: / ブロックへ送る
env.set(decay: 800, sustain: 0.4)                 # まとめて
env.set(attack: 5, notify: false)                 # 表示だけ（送らない）
env.sync              # to: から読み直して表示だけ更新（パッチを読み込んだ後など）
env.reset             # 定義したときの値に戻す（変わったら送る）
env.send_value        # 今の値を送る
env.label = "VCA"
env.color = :orange
env.max_time          # => 2000.0
```

値は C++ 側（レイアウトの部品）が持っていて、Ruby のゲッタは毎回そこから読む。
指で動かしている途中でも読める。レイアウトが表示できないボード（Freenove）では
Ruby 側の控えを返す。

## 3. 画面と操作

```
 ┌───────────────────────────────────────────┐
 │Amp                                         │
 │       ●A                                   │
 │      ╱ ╲                                   │
 │     ╱   ╲                                  │
 │    ╱     ●D━━━━━━━━●S                      │
 │   ╱░░░░░░░░░░░░░░░░░╲                      │
 │  ╱░░░░░░░░░░░░░░░░░░░╲                     │
 │ ╱░░░░░░░░░░░░░░░░░░░░░●R                   │
 │ A 10ms   D 300ms   S 70%    R 300ms        │
 └───────────────────────────────────────────┘
```

- 曲線は 0 → 1.0（attack）→ sustain（decay）→ 一定（sustain）→ 0（release）の折れ線。
  線の下を部品の色の暗い版で塗る
- ハンドルは 4 つ:

  | ハンドル | 位置 | 横に動かすと | 縦に動かすと |
  |---|---|---|---|
  | A | attack の頂点 | attack | — |
  | D | decay の終わり（= sustain の始まり） | decay | sustain |
  | S | sustain の終わり（= release の始まり） | — | sustain |
  | R | release の終わり | release | — |

- 指を置くと**いちばん近いハンドル**をつかむ（部品の中ならどこに置いてもよい。ハンドルの
  上に正確に置く必要はない）。つかんだ瞬間に値は飛ばず、指とハンドルのずれを保ったまま
  動く。他の指がつかんでいるハンドルは選ばない（2 本指で A と R を同時に動かせる）
- つかんでいるハンドルは大きく、白い輪付きで描き、下の値表示のその項目を白くする
- 下段に 4 つの値。時間は 1000ms 未満は `ms`、以上は `s`（小数 1 桁）、sustain は `%`。
  部品が低すぎて入らないときは値の行を省く

### 3.1 時間軸

横軸は**区間ごとの対数目盛り**。A / D / R の各区間は同じ幅 `U`（最大まで伸ばしたときの
長さ）を持ち、sustain の区間は `U / 2` の固定幅。描画領域の幅 = `3.5 U`。

```
x(A) = 左端            + U · n(attack)
x(D) = x(A)            + U · n(decay)
x(S) = x(D)            + U / 2
x(R) = x(S)            + U · n(release)

n(t) = ln(1 + t) / ln(1 + max_time)     （t は ms、0 → 0、max_time → 1）
```

- 区間を**数珠つなぎ**にしているので、あるハンドルを動かすと右側のハンドルは一緒に
  ずれるが、つかんだハンドル自身は指の下に留まる（左隣は動かないため）
- 時間をそのまま比例で描くと、release 4 秒の横で attack 5ms がつぶれて指で
  つかめなくなる。対数目盛りなら 1ms〜数秒が同じ指の動きの範囲に収まる
  （`max_time` 2000 のとき 10ms は区間の約 32%、100ms は約 61%、1 秒は約 91%）
- 見た目は実時間に比例しない（たとえば attack 1 秒と release 1 秒は同じ幅）。
  正確な値は下段の数字で見る
- 時間は 1ms 単位、sustain は 0.01 単位に丸める

縦軸はレベル 0〜1 の線形。AMY のエンベロープは区間内を線形には補間しない場合があるが、
この部品はブレークポイントの位置を示すもので、曲線の形までは再現しない。

## 4. 実装

### 4.1 構成

```
 [PicoRuby task]                               [UI task]
  UI.layout { envelope ... }
    └ UI._layout_add_envelope ─→ ui_widget_t.env  ←─ ScreenLayout::onTouchMove()
                                   (LayoutDef)          ├ ui_env_drag(): 指 → 値
                                                        ├ dirty → paintWidget()
                                                        └ ui_widget_env_event_post()（合流）
  UI.process ←──────────────────────────────── イベントキュー
    └ UI::Envelope#dispatch
         ├ UI._lw_env_values で 4 値を読む
         ├ to.amp_envelope(...)
         └ ブロック
```

- `ui_widget_t` の union に `envelope_config_t`（ラベル・色・4 値・初期値・`max_time`・
  notify）を足す。knob_config_t より小さいので `LayoutDef` は大きくならない
- 種類は `UI_WIDGET_ENVELOPE`（= 7、`ui_widget_kind_t` の末尾）
- イベントは `UI_EVENT_WIDGET`（`kind` = envelope、`phase` = change）。値は載せない
  （4 つ載せる場所がない）。Ruby は受け取ったら `UI._lw_env_values` で今の値を読む。
  キューには**部品ごとに 1 件だけ**（knob と同じ合流。待っている間に動いた分は
  最新の値で読まれる）。指を離したときに `final` 付きで 1 件
- 描画とハンドルの座標計算は `ui_widgets.cpp` の `ui_env_geom_for()` / `ui_env_points()` /
  `ui_env_paint()`。ドラッグの計算（指の位置 → 値）も同じ座標計算を使う `ui_env_drag()`
- 共有ストリップで描く（pad / knob と同じ `paintWidget()` の経路）。毎回部品全体を描き直す
- Ruby からの変更（`env.attack =` など）は `ui_layout_envelope_set()` で値を書き換えて
  dirty にするだけで、描画は UI タスク

### 4.2 C API（ui_common.h）

```c
int  ui_layout_add_envelope(int x, int y, int w, int h, const char* label,
                            uint16_t color, float attack, float decay,
                            float sustain, float release, float max_time,
                            bool notify);
bool ui_layout_envelope_get(int slot, int id, float out[4]);
// 負の値はその項目を変えない。変わったら true（dirty も立つ）
bool ui_layout_envelope_set(int slot, int id, float attack, float decay,
                            float sustain, float release);
bool ui_layout_envelope_reset(int slot, int id);
void ui_widget_env_event_post(int layout, uint8_t id, bool final);
```

Ruby からは `UI._layout_add_envelope` / `UI._lw_env_values` / `UI._lw_env_set` /
`UI._lw_env_reset`。ラベルと色は既存の `_lw_set_label` / `_lw_set_color` が扱う。

### 4.3 未対応・今後

- 専用画面（Knobs のような固定画面）は作らない。レイアウトで足りる
- ステージが 4 つより多いエンベロープ（DX7 の 4 レート 4 レベルなど）は扱わない。
  `AMY::FM` のオペレータは今の API どおり ADSR に要約したものを編集する
- 曲線（指数カーブ）の編集、ループ、ディレイ / ホールド区間
- ベロシティ / キートラックによるエンベロープの変化の表示
