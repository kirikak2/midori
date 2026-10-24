# UI ナビゲーション（ホームメニュー）設計ドラフト

- 状態: **実装済み**（2026-10-12）。ビルドのみで**実機は未確認**。実装メモは 10 章
- 実装: [main/ui/screen_home.cpp](../main/ui/screen_home.cpp)（Home）/
  [main/ui/ui_manager.cpp](../main/ui/ui_manager.cpp)（切り替え・Back・メニュー）/
  [mrbgems/picoruby-ui/mrblib/ui.rb](../mrbgems/picoruby-ui/mrblib/ui.rb)（`UI.show` ほか）
- 関連: [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md)（レイアウト画面はこのメニューの 1 項目として載る）/
  [M5STACK_UI_DESIGN.md](M5STACK_UI_DESIGN.md)
- 対象コード: [main/ui/ui_manager.cpp](../main/ui/ui_manager.cpp)（`handleTouch` / `nextScreen` /
  `prevScreen` / `setScreen`）、[main/ui/ui_common.cpp](../main/ui/ui_common.cpp)（`ui_draw_nav_bar`）、
  [mrbgems/picoruby-ui/mrblib/ui.rb](../mrbgems/picoruby-ui/mrblib/ui.rb)

レイアウト DSL とは独立して先に入れられるので、別ドキュメントにしている。

## 1. 現状と課題

ナビバーは 3 つのゾーンで、左右の `<` `>` が `ui_screen_index_t` の順に画面を 1 つずつ巡回する
（`UIManager::prevScreen` / `nextScreen`）。中央はその画面固有の操作（TAP / Send / Run / Clear …）。

```
Main → Pads → MIDI Info → Logs → Scripts → Settings → Tombola → Knobs → XYPad → (Main)
```

- 画面が 9 枚あり、今後 Layout / Sequencer で 10 枚以上になる。**Main から XYPad まで `>` を 8 回**、
  逆回りでも 2 回押す必要がある
- 並び順は enum の追加順で、使う頻度や意味のまとまりと無関係（Pads と Knobs の間に Logs や Settings が挟まる）
- 演奏中に画面を切り替える手段が指しか無い。外部の MIDI コントローラから画面を切り替えられない

コード上の問題も 2 つある（この変更で一緒に直す）。

- **`UI.set_screen` が PicoRuby タスクから `UIManager::setScreen()` を直接呼んでいる。**
  `leave()` / `enter()` が UI タスクの `update()` / `draw()` と並行して走り得る（データ競合）
- `UI_EVENT_SCREEN_CHANGE` / `:screen_change` は定義だけあって、**どこからも push されていない**

## 2. 目標と非目標

### 目標

1. どの画面からでも **2 タップ以内**（Home → 目的の画面）で任意の画面へ行ける
2. よく使う 2 画面の行き来（例: XYPad ⇄ Knobs）は **1 タップ**
3. Ruby から各画面へ移動するメソッドを用意し、**MIDI 入力などをきっかけに画面を切り替えられる**
4. スクリプトがメニューに出す項目を絞れる（演奏用スクリプトで Settings などを隠す）
5. 既存の画面番号（`UI::SCREEN_*`）と `UI.set_screen` / `UI.knobs` / `XYPad#show` / `Tombola#show` は引き続き使える

### 非目標

- スワイプによる画面切り替え（XYPad / Knobs / Tombola のドラッグと衝突する）
- 画面遷移アニメーション
- ステータスバー / ナビバーを消す全画面モード
- ハードウェアボタン（CoreS3 / Tab5 には使えるボタンが無い）

## 3. 画面構成

### 3.1 Home 画面

新しい画面 **Home** を追加する。コンテンツ領域に画面ごとのタイルを並べ、タップでその画面へ移動する。
タイルはグループごとにまとめ、グループの順に並べる。

| グループ | 画面（シンボル） | タイルの表示 |
|---|---|---|
| 演奏 | `:pads` `:knobs` `:xypad` `:tombola` `:sequencer`（将来）、スクリプトが定義したレイアウト（1 つにつき 1 枚、数の上限なし） | 画面名（レイアウトは title）+ 状態（Tombola 再生中、Knobs の現在バンクなど） |
| テンポ / 状態 | `:tempo` `:midi_devices` | Tempo は BPM、MIDI Devices は機器の一覧とパッチ（docs/UI_MIDI_DEVICES.md） |
| システム | `:scripts` `:logs` `:settings` | Scripts は実行中のスクリプト名 |

```
CoreS3 (content 320x180)                Tab5 (content 1280x620)
┌──────────────────────────────┐        ┌─────────────────────────────────────────────┐
│ Home                    ●USB │        │ Home                                  ●USB  │
├──────┬──────┬──────┬─────────┤        ├─────────┬─────────┬─────────┬─────────┬─────┤
│ Pads │Knobs │XYPad │ Tombola │        │  Pads   │  Knobs  │  XYPad  │ Tombola │ ... │
│      │  A   │      │   ▶     │        │         │    A    │         │    ▶    │     │
├──────┼──────┼──────┼─────────┤        ├─────────┼─────────┼─────────┼─────────┼─────┤
│Tempo │ MIDI │Script│  Logs   │        │  Tempo  │  MIDI   │ Scripts │  Logs   │ Set │
│120BPM│ Info │ app  │         │        │ 120 BPM │  Info   │ app.rb  │         │     │
├──────┴──────┴──────┴─────────┤        ├─────────┴─────────┴─────────┴─────────┴─────┤
│ Home     [  (空き) ]    Back │        │ Home            [  (空き)  ]           Back │
└──────────────────────────────┘        └─────────────────────────────────────────────┘
```

- タイルのグリッドは CoreS3 で 4 列 x 3 行（最大 12 枚、1 枚 ≈ 76x56px）、大画面で 5 列 x 3 行。
  12 枚を超えたら Home 自体を縦スクロールする（Scripts 画面と同じ扱い）
- 色はグループごとに分ける（演奏 = 各部品の既定色系 / システム = グレー）
- タイルの状態表示は、Home を表示している間 0.5 秒ごとに差分更新する
- タイルの操作はタップだけ。**長押しには何も割り当てない**（9 章 D6）
- スクリプトが Home に足せるのは**レイアウトだけ**（9 章 D7）。任意のブロックを呼ぶタイルは作らない
  （そうした操作は Pads や レイアウト上の pad で書く）
- 将来的には Home 自体を [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md) の組み込みレイアウト
  （タイル部品のグリッド）として作り直せる。最初は他の画面と同じく C++ で直接実装する

### 3.2 ナビバー

```
┌──────────────┬────────────────────────┬──────────────┐
│   ⌂ Home     │  画面固有の操作（今と同じ） │    ↩ Back    │
└──────────────┴────────────────────────┴──────────────┘
```

| ゾーン | 動作 |
|---|---|
| 左 `Home` | Home 画面へ。Home 表示中は無効（グレー表示） |
| 中央 | 今と同じ（Tempo = TAP、Knobs = Send、Scripts = Run、Logs = Clear、Tombola = Start/Stop …）。**Home では何も置かない**（9 章 D5） |
| 右 `Back` | **直前に表示していた画面**へ戻る（Home は数えない）。押すたびに 2 画面を行き来する |

「Back = Home を除いた直前の画面」にすることで、Home 経由で XYPad → Knobs と移ったあとは
`Back` 1 タップで XYPad ⇄ Knobs を往復できる（目標 2）。履歴はスタックにせず**直前の 1 画面だけ**持つ。
戻り続けられるブラウザ型の履歴は、演奏中の操作として予測しづらいため採らない。

左右の `<` `>` による巡回は廃止する（Ruby の `UI.next_screen` / `UI.prev_screen` としては残す。4.2）。

### 3.3 画面番号

- `UI_SCREEN_HOME` を `ui_screen_index_t` の**末尾**に追加する（既存の番号は変えない。
  レイアウト画面 `UI_SCREEN_LAYOUT = 9` を先に入れるなら Home は 10）
- Home の並び順は enum の順ではなく、3.1 のメニュー定義表で決める（C++ の static な配列）
- 起動時の画面は **Home**（9 章 D1）

## 4. Ruby API

### 4.1 画面の指定方法

画面は **Symbol** で指定する。既存の `UI::SCREEN_*` 整数、`UI::Layout` インスタンスも受け付ける。

| シンボル | 画面 | 既存の定数 |
|---|---|---|
| `:home` | Home | `UI::SCREEN_HOME`（新設） |
| `:tempo`（別名 `:main`） | Tempo（BPM / TAP / Sync。旧 Main） | `UI::SCREEN_TEMPO`（新設。`UI::SCREEN_MAIN` と同じ値 0） |
| `:pads` | Pads | `UI::SCREEN_PADS` |
| `:knobs` | Knobs | `UI::SCREEN_KNOBS` |
| `:xypad` | XYPad | `UI::SCREEN_XYPAD` |
| `:tombola` | Tombola | `UI::SCREEN_TOMBOLA` |
| `:keys` | Keys（画面の鍵盤、docs/KEYS.md） | `UI::SCREEN_KEYS`（2026-10-18 追加。値 11） |
| `:layout` | 現在のレイアウト | `UI::SCREEN_LAYOUT`（新設） |
| `:midi_devices`（旧 `:midi_info` も可） | MIDI Devices | `UI::SCREEN_MIDI_DEVICES`（= `UI::SCREEN_MIDI_INFO`） |
| `:scripts` | Scripts | `UI::SCREEN_SCRIPTS` |
| `:logs` | Logs | `UI::SCREEN_LOGS` |
| `:settings` | Settings | `UI::SCREEN_SETTINGS` |

### 4.2 メソッド

**汎用:**

```ruby
UI.show(:knobs)            # Symbol / SCREEN_* / UI::Layout を受け付ける
UI.show(:knobs, bank: 2)   # 画面ごとの追加引数（4.3）
UI.screen                  # => :knobs  （現在の画面。移動要求の反映待ちならその行き先）
UI.home                    # = UI.show(:home)
UI.back                    # ナビバーの Back と同じ
UI.next_screen             # Home のメニュー順で次の画面（表示中の項目だけ。Home は飛ばす）
UI.prev_screen             # 同じく前の画面
```

**画面ごとの短縮形**（すべて `UI.show(sym, ...)` と同じ）:

```ruby
UI.home
UI.tempo                   # 旧 Main 画面。UI.main も別名として残す
UI.pads
UI.knobs(bank: nil)        # 既存。bank: を追加
UI.xypad
UI.tombola
UI.layout_screen           # ※ UI.layout はレイアウト定義に使う（UI_LAYOUT_DSL.md）
UI.midi_devices
UI.scripts
UI.logs                    # ※ UI.log(text) とは別物（引数なし）
UI.settings
```

インスタンス側の `show` も今までどおり使える（中身は `UI.show(:xypad)` 等）。

```ruby
pad = UI::XYPad.new(...); pad.show
t = UI::Tombola.new(...); t.show
perform = UI.layout("Perform") { ... }; perform.show
```

既存の `UI.set_screen(index)` / `UI.current_screen`（整数を返す）は互換のため残す。
新しく書くスクリプトには `UI.show` / `UI.screen` を勧める。

**戻り値と失敗時:** `UI.show` は移動を要求できたら `true`、
存在しない画面 / メニューで無効化された画面（4.4）/ レイアウト未定義の `:layout` なら `false`
を返して何もしない（例外にしない。MIDI 入力のハンドラから雑に呼べるように）。

### 4.3 画面ごとの追加引数

| 画面 | 引数 | 意味 |
|---|---|---|
| `:knobs` | `bank: 1..4` | 表示するバンク（`UI.knob_bank=` と同じ） |
| `:layout` | `layout` を Symbol の代わりに渡す | 複数レイアウトのどれを出すか（UI_LAYOUT_DSL.md） |
| `:scripts` | `select: "app.rb"` | カーソルを合わせておくファイル（任意。後回しでよい） |

その他の画面は引数なし。知らないキーワードは無視せず `ArgumentError`（打ち間違いに気づけるように）。

### 4.4 メニューのカスタマイズ

```ruby
UI.menu(hide: [:settings, :midi_devices, :tombola])   # Home のタイルと next/prev から外す
UI.menu(only: [:xypad, :knobs, :layout, :tempo])    # これだけを出す
UI.menu(reset: true)                               # 既定に戻す
```

- 隠した画面は Home に出ず、`next_screen` / `prev_screen` でも飛ばす。
  ただし `UI.show(:settings)` のような**明示的な移動は `false` を返して拒否**する（隠した意味を保つため）
- **`:home` と `:scripts` は隠せない**（スクリプトを止めて別のスクリプトに切り替える手段が無くなるため）
- スクリプト停止時（`picoruby_esp32_midi_cleanup()` の経路）に既定へ戻す

### 4.5 画面切り替えイベント

画面が変わるたびに（タッチでも Ruby からでも）`:screen_change` を発行する。
今は定義だけで発行されていないものを実際に流す。

```ruby
UI.on(:screen_change) do |e|
  e[:screen]    # => :knobs
  e[:from]      # => :xypad
  e[:source]    # => :touch / :script / :system（レイアウトが消えて Home に戻ったとき）
  e[:layout]    # => 表示したレイアウトの UI::Layout（レイアウト画面以外では nil）
end
```

`source: :script` を区別するのは、スクリプト自身の `UI.show` で起きた切り替えに反応して
また `UI.show` する、というループを書いたときに気づけるようにするため。

## 5. MIDI 入力からの画面切り替え

**従来の `MIDI::Input#on` をそのまま使い**、そのハンドラから 4.2 のメソッドを呼ぶ（9 章 D8）。
`UI.midi_nav` のような専用のヘルパや、`MIDI.route` のような C 側の直結は作らない
（画面切り替えは数十 ms 遅れても問題にならず、どのメッセージで何をするかはスクリプトごとに違うため）。

```ruby
require 'midi'
require 'ui'

ctl = MIDI::Input.new(MIDI::Device.new(MIDIDevices.usb_midi_host))

# プログラムチェンジ 0〜3 で演奏画面を選ぶ
SCREENS = [:xypad, :knobs, :pads, :layout]
ctl.on(:program_change) do |e|
  s = SCREENS[e[:program]]
  UI.show(s) if s
end

# CC 102 / 103 のボタン（値 > 0 で押下）で前後、104 で Back
ctl.on(:control_change) do |e|
  next if e[:value] == 0
  case e[:cc]
  when 102 then UI.prev_screen
  when 103 then UI.next_screen
  when 104 then UI.back
  end
end

# ノートで Knobs のバンクを直接選ぶ（C1〜D#1）
ctl.on(:note_on) do |e|
  bank = e[:note] - 24 + 1
  UI.knobs(bank: bank) if bank >= 1 && bank <= 4
end
```

ハンドラは `MIDI.bpm_loop` か `MIDI.sleep_ms` の中で Ruby タスクから呼ばれる
（`Kernel#sleep_ms` では呼ばれない）。`UI.show` は（6.1 のとおり）要求を積んで即座に戻る。

## 6. 実装方針

### 6.1 画面切り替えは UI タスクだけが行う

画面の切り替え要求を**すべて UI タスク上で反映する**ようにする。

```
 [PicoRuby task]                    [UI task: UIManager::update()]
  UI.show(:knobs, bank: 2)
    └ ui_request_screen(KNOBS, arg=1)
        └ pending に書く（spinlock）──→ 先頭で pending を取り出し
                                          ├ setScreen()（leave / enter）
                                          ├ Back 用の直前画面を更新
                                          ├ ui_event_push(SCREEN_CHANGE)
                                          └ 全画面の再合成
 [UI task 自身: タッチ]
  Home のタイル / Home / Back ──→ 同じ setScreen() を直接
```

- pending は 1 件だけ（連打されたら最後の要求が勝つ）。キューにしない
- `UI.screen` は pending があればその行き先を返す（`UI.show` 直後に読んでも食い違わないように）
- `UI.set_screen` / `XYPad#show` / `Tombola#show` / `UI.knobs` もこの経路に乗せ替える。
  これで 1 章の「Ruby タスクから `leave()` / `enter()` を呼ぶ競合」が解消する

### 6.2 UIManager の変更

| 変更 | 内容 |
|---|---|
| `m_previousIndex` | Back 用。`setScreen()` で、移動元が Home 以外なら記録 |
| `requestScreen(index, arg)` | 6.1 の pending。C API `ui_request_screen()` |
| `handleTouch()` | ナビバー左右を `prevScreen` / `nextScreen` から Home / Back に変更 |
| `nextScreen` / `prevScreen` | メニュー順（表示中の項目のみ）で巡回するよう変更。Ruby からのみ使う |
| メニュー定義 | `{ index, group, label }` の static 配列 + 表示 / 非表示のビットマスク |
| `ui_draw_nav_bar()` | 左右に `Home` / `Back` を描く。無効時はグレー |
| `ScreenMain::getTitle()` | `"Tempo"` を返す。クラス名・ファイル名・`UI_SCREEN_MAIN` は変えない（差分を小さくするため） |

`Screen` 基底クラスに `virtual void onShowArg(int arg)`（4.3 の追加引数を受ける）と、
Home のタイル用に `virtual void getTileStatus(char* buf, int len)` を足す。どちらも既定は何もしない。

### 6.3 Ruby 側

- `UI.show` / 短縮形 / `UI.screen` / `UI.back` / `UI.menu` は `ui.rb` に書く。
  C に足す関数は `_request_screen(index, arg)` / `_back` / `_next_screen(dir)` /
  `_menu_set_mask(mask)` 程度
- Symbol ⇄ 画面番号の表は Ruby に持つ（`UI::SCREEN_*` と同じ並び）

### 6.4 Freenove

画面が無いので no-op スタブ。`UI.show` は `false`、`UI.screen` は `nil` を返す。

## 7. 互換性

| 既存 | 変更後 |
|---|---|
| ナビバー `<` `>` | 廃止。`Home` / `Back` に置き換え |
| 中央ボタン | 変更なし |
| `UI.set_screen(i)` / `UI.current_screen` | 残す（内部は 6.1 の経路） |
| `UI.knobs` | 残す（`bank:` を追加） |
| `XYPad#show` / `Tombola#show` | 残す |
| `UI::SCREEN_*` の値 | 変更なし（追加のみ） |
| 起動時の画面 | Main → Home |
| Main 画面の名前 | タイトル・タイルとも **Tempo**。`:main` / `UI::SCREEN_MAIN` / `UI.main` は別名として残す |

ユーザが体で覚えている「`>` を何回」という操作は使えなくなる。
README / M5STACK_UI_DESIGN.md の画面遷移の説明を更新する。

## 8. 段階的な進め方

| 段階 | 内容 |
|---|---|
| 1 | 6.1 の UI タスク経由の切り替え + `:screen_change` の発行（見た目の変更なし。競合の修正） |
| 2 | Home 画面 + ナビバーの Home / Back。`UI.show` / 短縮形 / `UI.back` / `next_screen` / `prev_screen` |
| 3 | `UI.menu`、タイルの状態表示、`UI.knobs(bank:)` などの追加引数 |
| 4 | Layout 画面の追加に合わせてメニューへ載せる（UI_LAYOUT_DSL.md） |

## 9. 決定事項

すべて決定済み（2026-10-12）。

| # | 論点 | 決定 | 反映先 |
|---|---|---|---|
| D1 | 起動時の画面 | **Home** | 3.3 / 7 章 |
| D2 | Back の履歴 | **直前の 1 画面**（Home は数えない。押すたびに 2 画面を往復） | 3.2 |
| D3 | 短縮形のメソッド名 | **画面名そのまま**（`UI.knobs` / `UI.xypad` / `UI.logs` …） | 4.2 |
| D4 | Main の名前 | **Tempo に改名**。`:main` / `UI::SCREEN_MAIN` / `UI.main` は別名として残す | 3.1 / 4.1 / 4.2 / 6.2 / 7 章 |
| D5 | Home の中央ボタン | **何も置かない** | 3.2 |
| D6 | タイルの長押し | **何もしない** | 3.1 |
| D7 | スクリプトが Home に足せる項目 | **レイアウトのみ**（1 レイアウト 1 タイル。2026-10-24 から数の上限なし） | 3.1 |
| D8 | MIDI 入力からの切り替え | **従来の `MIDI::Input#on` を流用**。専用ヘルパは作らない | 5 章 |

## 10. 実装メモ

- タイルは CoreS3 4 列・大画面 5 列 × 3 行。収まらないとき（CoreS3 でレイアウトを定義したとき）は
  縦ドラッグで 1 行ずつスクロールし、右端に位置バーを出す。タップは指を離したときに
  押した場所と同じタイルの上なら成立する
- タイルの文字サイズは、最も長い固定名（"MIDI Info"）が収まる大きさで全タイル共通。
  長いレイアウト名だけ小さくなり、それでも入らなければ末尾を切る
- 状態表示: Tempo = BPM、Knobs = 現在のバンク、Tombola = 再生中、Scripts = 実行中の
  スクリプト名、MIDI Info = 接続中。`Screen::getTileStatus()` で各画面が返す
- `UI.screen` は保留中の要求があればその行き先を返す。Back / next / prev も要求した時点で
  行き先を決めて積む
- ナビバーの Home は Home 表示中、Back は戻り先が無い（または隠された・消えた）ときに灰色
- `UI.knobs(bank:)` で Knobs 画面を表示中なら、画面はそのままバンクだけ切り替わる
