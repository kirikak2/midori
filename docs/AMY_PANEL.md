# AMY パネル（UI::AMYPanel / AMY のパートを画面で組む）

AMY のパート（`AMY.add_fm` / `AMY.add_synth`）を画面から足し、パートごとにプリセット・
MIDI チャンネル・エンベロープ・主なパラメータをいじり、その場で 1 オクターブの鍵盤で
試し弾きする画面。スクリプトを書かずに AMY の音色を組むためのもの。

- 状態: 実装済み（2026-10-27）。Tab5 のビルドとホストテストのみで**実機は未確認**
- 置き場所: **レイアウト DSL の上に Ruby だけで組む**（C++ の新しい画面は作らない）。
  Home のタイルは 1 枚（タイトル `AMY`）
- 対応ボード: AMY のあるボード（Tab5 / CrowPanel。どちらも `UI_LAYOUT_LARGE`）
- 実装: [mrbgems/picoruby-ui/mrblib/ui_amy_panel.rb](../mrbgems/picoruby-ui/mrblib/ui_amy_panel.rb)
- 起動: UI モードでは `main_task_base.rb` の `setup_amy_panel` が Home にタイルを出す。
  スクリプトからは `UI::AMYPanel.new.show`
- サンプル: [examples/amy_panel.rb](../examples/amy_panel.rb)
- テスト: `ruby test/amy_panel_test.rb`（picoruby-ui ディレクトリで。mruby は `cat mrblib/ui_layout.rb mrblib/ui_amy_panel.rb test/amy_panel_test.rb | mruby`。タブの並び・チャンネルの
  割り当て・プリセットの送り・ページの組み立て）
- 関連: [AMY_SYNTH.md](AMY_SYNTH.md)（§6 パート）/ [ENVELOPE.md](ENVELOPE.md) /
  [UI_LAYOUT_DSL.md](UI_LAYOUT_DSL.md) / [KEYS.md](KEYS.md) /
  [mrbgems/picoruby-amy/README.md](../mrbgems/picoruby-amy/README.md)

## 1. 画面

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │[FM 1 E.PIANO 1][SY 2 Brass Swell][SY 2 Strings]                  [ + ]│ タブ
 ├──────────────────────────────────────────────────────────────────────┤
 │[<<][<][ A12 Brass Swell ][>][>>]   [Ch-][ Ch 2 ][Ch+]          [Del] │ ヘッダ
 ├────────────────────┬────────────────────┬────────────────────────────┤
 │ Amp                │ Filter             │ Cutoff  Reso  FltEnv KeyTrk│
 │     ●              │    ●               │  ◯      ◯     ◯      ◯    │
 │    ╱ ╲___●         │   ╱ ╲              │ Glide   Pan   Volume Reverb│
 │   ╱       ╲        │  ╱   ╲___●___      │  ◯      ◯     ◯      ◯    │
 ├────────────────────┴────────────────────┴────────────────────────────┤
 │[Oct-]  ┃ ┃█┃ ┃█┃ ┃ ┃█┃ ┃█┃ ┃█┃ ┃ ┃                          [Oct+]  │ 鍵盤
 └──────────────────────────────────────────────────────────────────────┘
```

上から 4 段（重み 1 : 1 : 4 : 2）。

### 1.1 タブ

- タブ 1 枚 = パート 1 個（`AMY.parts` の synth 番号順）。ラベルは
  `種類 チャンネル 音色名`（`FM 1 E.PIANO 1` / `SY 2 Brass Swell`。チャンネルは 1 始まりで表示）。
  幅に入らない分は C++ のパッドが切り詰める
- 選んでいるタブは種類の色（FM = マゼンタ / Synth = シアン）、ほかは灰色
- タブは最大 **8 枚**（`MAX_TABS`）。AMY 自体は 16 パートまで持てるが、画面の幅と
  レイアウトの部品数（32）で 8 に絞る。スクリプトが 9 個以上足したときは先頭の 8 個だけ出す
- 右端の **`+`** を押すとタブの段が選択肢に変わる: `[+ FM] [+ Synth] [Cancel]`。
  選ぶとパートを足して、そのタブを選ぶ。8 枚あるときは `+` が灰色の `Full` になり押しても何もしない
- 足すパートの既定: チャンネルは**どのパートも使っていない一番小さいチャンネル**（全部埋まって
  いれば 1）、`voices: 6`、FM は `patch: 128`（DX7 の最初）、Synth は `patch: nil`（初期音色）
- パートが 1 個もないときはヘッダから下に「`+` でパートを追加」とだけ出す（部品は置かない）

### 1.2 ヘッダ

| 部品 | 動作 |
|---|---|
| `<<` `<` `>` `>>` | プリセットを −10 / −1 / +1 / +10。端で回り込む |
| 音色名 | 今のプリセット名（Synth の初期音色は `Init`）。押しても何もしない |
| `Ch-` `Ch +` と `Ch n` | MIDI チャンネル 1〜16 を ±1（回り込む）。`part.channel =` |
| `Del` | 1 回目で赤い `Sure?` になり、もう 1 回でパートを消す（`part.remove`）。ほかの操作をすると元に戻る |

プリセットの範囲は種類で違う:

| 種類 | 範囲 | 並び |
|---|---|---|
| FM | 128〜255（DX7） | 128 → … → 255 → 128 |
| Synth | Init（`init_voice`）と 0〜127（Juno-60） | Init → 0 → … → 127 → Init |

プリセットを変えるとエンベロープとノブは**パートから読み直して表示だけ**更新する（送り返さない。
パッチが持つ値をそのまま見せる）。

### 1.3 エンベロープとパラメータ

**Synth**（`AMY::Synth`）

- エンベロープ 2 つ: `Amp`（`to: part, env: :amp`）/ `Filter`（`to: part, env: :filter`）、
  `max_time: 3000`
- ノブ 8 個（4 × 2）:

  | ノブ | 範囲 | 送り先 |
  |---|---|---|
  | Cutoff | 100〜8000 Hz（対数） | `cutoff=` |
  | Reso | 0.7〜8.0 | `resonance=` |
  | FltEnv | 0〜6 oct | `filter_env=` |
  | KeyTrk | 0〜1 | `key_track=` |
  | Glide | 0〜500 ms | `glide=` |
  | Pan | 0〜1（中央起点） | `pan=` |
  | Volume | 0〜1 | `volume=` |
  | Reverb | 0〜1 | `reverb=`（**出力バス共通**。全パートに効く） |

**FM**（`AMY::FM`）

- エンベロープ 1 つ: 選んでいるオペレータの ADSR（`to: part.op(n)`）。左上の `Op n` パッドで
  1 → 6 → 1 と切り替える。切り替えるとエンベロープと `OpLvl` / `Ratio` ノブがそのオペレータを読み直す
- DX7 のエンベロープは 4 段より多いので、オペレータの ADSR は `AMY::FM::Operator` の要約
  （[ENVELOPE.md](ENVELOPE.md) §4.3）を編集する。触った時点でそのオペレータは ADSR になる
- ノブ 8 個:

  | ノブ | 範囲 | 送り先 |
  |---|---|---|
  | Algo | 1〜32（整数） | `algorithm=` |
  | Feedbk | 0〜1 | `feedback=` |
  | OpLvl | 0〜1 | `op(n).level=` |
  | Ratio | 0.5〜16（対数） | `op(n).ratio=` |
  | Cutoff | 100〜8000 Hz（対数）。右端 = フィルタなし | `filter_freq=` / `filter = :none` |
  | Reso | 0.7〜8.0 | `resonance=` |
  | Volume | 0〜1 | `volume=` |
  | Reverb | 0〜1 | `reverb=`（出力バス共通） |

ノブの目盛りは 0〜127（Algo だけ 1〜32）。実際の値への変換は `AMY.scale` / `AMY.unscale`
（examples/amy_synth.rb と同じ）。パートが値を知らない項目（パッチを読んだ直後の `volume` など）は
既定値の位置に置く。

### 1.4 鍵盤

- `UI::Keys`（`octaves: 1`、13 鍵）。**選んでいるタブのチャンネル**で、AMY のトランスポートへ直接弾く
  （`device: AMY::Transport`）。同じチャンネルの別パートも一緒に鳴る（それが AMY のパートの
  レイヤーなので、実際に鳴る音を試せる）
- `Oct-` / `Oct+` で最低音を 1 オクターブずつ（C1〜C7）。既定 C4（60）
- 発音は C++ 側（Keys の既存の仕組み）なので Ruby のループ間隔に左右されない

## 2. Ruby API

```ruby
panel = UI::AMYPanel.new                 # タイル "AMY" を作る（AMY はまだ起動しない）
panel.show
panel.refresh                            # パートの一覧を読み直す（_poll も毎回見ている）
panel.select(part)                       # そのパートのタブへ
panel.part                               # 選んでいるパート（なければ nil）
panel.layout                             # UI::Layout
```

```ruby
UI::AMYPanel.new(title: "AMY", engine: nil, low: 60)
```

| 引数 | 意味 |
|---|---|
| `title:` | レイアウトのタイトル（Home のタイル） |
| `engine:` | AMY のトランスポートを返す Proc。既定は `AMY::Transport.instance`。UI モードは `MIDIDevices.amy`（MIDI Devices 画面に AMY を登録する経路）を渡す |
| `low:` | 鍵盤の最低音 |

- AMY は**最初にパートを足すとき**に起動する（それまでは Home にタイルがあるだけ。AMY は
  オーディオ・I2S・Tab5 の USB パッドに触るので、UI モードでは standby のまま待つ）
- パネルが作ったパートは普通の `AMY::FM` / `AMY::Synth`。スクリプトから `panel.part.cutoff = 900`
  のように触ってもよい（ノブの表示はタブを選び直すと合う）
- スクリプトが `AMY.add_fm` などでパートを足す / 消すと、次の `UI.process` でタブが増減する
  （`_poll` が `AMY.parts` を前回と比べる）
- インスタンスは 1 つにする（`UI::Keys` も Tombola 同様プロセスに 1 つなので）。
  `UI::AMYPanel.current` で取り直せる

## 3. 実装

### 3.1 レイアウトを組み直す

レイアウト DSL は**構造の変更 = 再定義**（UI_LAYOUT_DSL.md D6）。パネルは次の場合に同じタイトルで
`UI.layout` を呼び直してページを丸ごと作る:

- タブの切り替え・パートの追加 / 削除・`+` の選択肢の表示 / 取り消し
- FM のオペレータの切り替え（エンベロープの `to:` が変わるため）

部品の初期値は選んでいるパートから読む（`Envelope` の `to:`、ノブの `value:`）。表示中の再定義は
全画面を 1 回合成し直すだけなので、ちらつかない。鍵盤は同じ `UI::Keys` を `keys:` で渡し直す
（押している音・最低音を保つ）。

値だけが変わる操作（プリセット送り・チャンネル）は再定義しない:
`Envelope#sync`、`Knob#set_value(v, notify: false)`、`Pad#label=` / `color=` で書き換える。

### 3.2 組み直しは UI.process の後で

パッドのブロックの中で再定義すると、同じ `UI.process` で後に並んでいるイベント
（直前まで回していたノブの最後の値など）が、**同じ id の新しい部品**へ配られてしまう
（再定義でも部品の id は 0 から振り直す）。そのためパッドのブロックは「やること」を
`@pending` に積むだけにし、`UI::AMYPanel._poll`（`UI.process` の最後）で実行する。

`main_task_base.rb` の `UI.process` の上書きに `UI::AMYPanel._poll` を足す
（`UI::MIDIDevices._poll` と同じ扱い。パネルがなければ何もしない）。

### 3.3 部品数

レイアウトは 32 部品まで。Synth のページ:

| 段 | 部品 | 数 |
|---|---|---|
| タブ | タブ 8 + `+` | 9 |
| ヘッダ | `<<` `<` 名前 `>` `>>` / `Ch-` `Ch n` `Ch+` / `Del` | 9 |
| 本体 | エンベロープ 2 + ノブ 8 | 10 |
| 鍵盤 | `Oct-` 鍵盤 `Oct+` | 3 |
| | | **31** |

FM はエンベロープ 1 + `Op n` パッド 1 で同じ 31。

### 3.4 UI モード

- `setup_amy_panel`（`main_task_base.rb`、`BoardConfig::HAS_AMY` のときだけ）が
  `UI::AMYPanel.new(engine: Proc.new { MIDIDevices.amy })` を作る。`setup_midi_devices` の後
- UI モードのループは `MIDI.sleep_ms(100)` で回っているので、パネルを表示している間は 10 ms にする
  （ノブを回したときに 100 ms 遅れて聞こえないように）
- **UI モードで組んだパートはスクリプトを実行すると消える**（スクリプトの終了時に
  `picoruby_esp32_midi_cleanup()` が `AMY_GEM_reset()` を呼び、VM も作り直すため）。保存は未対応（§4）
- MIDI Devices 画面で外部鍵盤 → AMY をパッチしておけば、パネルで組んだパートをそのまま外部鍵盤で弾ける
  （チャンネルが合うパートが鳴る）

### 3.5 テスト

ホストで動かせるよう、デバイスに触る部分（`UI.layout` / `AMY`）と、純粋な計算
（タブの並びとラベル、空きチャンネル、プリセットの送り、ノブ ↔ 値の変換、ページの部品の並び）を分ける。
後者はクラスメソッド（`UI::AMYPanel.next_patch` など）にして `test/amy_panel_test.rb` で確かめる。
ページの組み立ては `UI::Layout.resolve` に通して、部品数が 32 以下でセルが重ならないことを確かめる。

## 4. 未対応・今後

- パネルの内容（パートとその設定）の保存と、UI モードに戻ったときの復元（SD の `.rb` か JSON）
- Synth のオシレータ（波形・レベル・オクターブ・デチューン）、LFO、エコーのページ
  （ヘッダに `Main / Osc / LFO` のページ切り替えを足す想定）
- FM のオペレータ 6 個を一覧するページ（レベル・比率をまとめて）
- `voices` の変更（パッチを読み直すので、確定操作のある UI にしたい）
- 9 個目以降のパートのタブ（タブの段のスクロール）
- CoreS3 など小さい画面（AMY 自体がないので対象外）
