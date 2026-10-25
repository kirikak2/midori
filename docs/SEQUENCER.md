# Sequencer（UI::Sequencer）

> **状態: 段階 1〜6 を実装（2026-10-25）**。エンジン（grid / realtime の再生、パターンの切り替え、チェーン、
> 和音の取り込み、リアルタイム録音）、トランスポート、Ruby API、保存と自動保存、画面とレイアウト部品、
> 既定パッチと `on_*` イベントまで。段階 6 は Tab5 の実機で `on_step` / `on_pattern` / `on_edit`（examples/sequencer_events.rb）と
> 既定パッチ Tempo → Sequencer の Clock が付くことを確認（2026-10-25。外したパッチが再起動後も外れたままかは未確認）。
> 録音（段階 5）は Tab5 の実機でリアルタイム入力の録音と再生を確認（2026-10-25）。
> 保存（段階 3）はビルドとホストのテストのみで、実機では未確認。
> Tab5 の実機で画面の表示と Play / Stop による再生を確認（2026-10-25）。それ以外（編集操作の細部、外部クロック同期、
> CoreS3 / CrowPanel）は未確認。
> 改訂: 和音・16 ライン・保存・1/2 ページ送り → パターン（スロット + チェーン）・自動保存（いずれも 2026-10-25）

## 実装状況（2026-10-25）

| 段階 | 状態 |
|---|---|
| 1. エンジン + grid + Ruby API | 済。`main/ui/seq_engine.{h,c}`、`main/ui/ui_sequencer.cpp`、`mrblib/ui_sequencer.rb` |
| 2. パターンとチェーン | 済（エンジンと Ruby API。画面の操作は段階 4） |
| 3. 保存と自動保存 | 済（実機未確認）。`.mseq` の読み書きは `seq_engine.c`（`seq_song_clone` / `seq_dump_*` / `seq_load_*` / `seq_engine_adopt`）、自動保存の判定と画面の要求は `ui_sequencer.cpp`、ファイル I/O は `mrblib/ui_sequencer.rb`（`save` / `load` / `songs` / `to_h` / `restore` / `_setup` / `_poll`）、起動時は `main_task_base.rb` の `setup_sequencer`、画面は File ボタンとシート |
| 4. 画面 + レイアウト | 済（下記の一部を除く）。`main/ui/screen_sequencer.{h,cpp}`、`UI_SCREEN_SEQUENCER`（= 12）/ `:sequencer` / `UI.sequencer` / `seq.show`、レイアウトの `sequencer`（`UI_WIDGET_SEQUENCER` = 6） |
| 5. リアルタイム録音 | 済（Tab5 の実機で録音と再生を確認）。エンジンは `seq_engine.c` の「Realtime recording」（`seq_set_armed` / `seq_engine_input_at`）、時刻の換算は `ui_sequencer.cpp`、画面は REC ボタン・ラインの arm の印・設定シートの Quant / Overdub・Replace、Ruby は `Line#arm` / `disarm` / `armed?`、`seq.armed` / `disarm` / `recording?`。下記「リアルタイム録音」の実装メモ |
| 6. 仕上げ | 済（Tab5 の実機で確認）。MIDI Devices の内蔵デバイス `seq`、既定パッチ Tempo → seq（Clock。`ui_midi_devices.cpp` の `DEFAULT_PATCHES`、docs/UI_MIDI_DEVICES.md）、`on_step` / `on_pattern` / `on_edit`（下記「状態・イベント」） |

- 段階 4 の残り（2026-10-25 に追加、ビルドとホストテストのみで実機未確認）:
  - **スロットの長押し**（狭い画面は `P3` の長押し）でパターンのメニュー: **Edit here / Follow play**
    （再生中のまま編集の切り替え）、**Copy**、**Paste N**（Copy した**番号**を覚えておき、貼るときにその時点の
    中身を写す。使用中のスロットへは 2 回押し）、**Clear**（2 回押し）、**Length**（auto → 1 → 2 → 4 → 8 小節）、**Close**
  - **Chain の長押し**で Chain シート（タップは従来どおり有効 / 無効）: 左にエントリ（`P3 x2`）、右に
    Add（選んだエントリの後ろに、いま画面にあるパターンを 1 回で）/ Delete / Pat ± / Rep ± / Up / Down /
    Clear（2 回押し）/ Turn on・off / Close
  - ラインの設定シートの空いていた 2 マスに **Copy** / **Paste p.l**（そのラインの notes と steps / rate / bars を
    別のライン・別のパターンへ。2 回押し。エンジンは `seq_pline_copy`、Ruby は `Line#copy_to(index, pattern:)`）
  - ヘッダの **Chord** をタップすると、選択中のラインのチャンネルで和音を 0.4 秒鳴らす（`MIDI_Note_trigger`、
    Sequencer の送信先とルートの宛先へ）
- **まだ無いもの**: ライン名・ファイル名の入力（文字入力の仕組みが要る。名前は Ruby の `l.name =` /
  `seq.save("name")` で付ける。画面からの保存はいまの名前か `SONGnnn`）
- 画面は 1 フレームに 1 回 `ui_seq::snapshot()` で見えている範囲だけをヒープ（PSRAM）の View に写して描く
  （描画中にエンジンのロックを持たない）。曲が変わったら全体、再生位置は出入りしたマスだけを描き直す
- テスト: `make -C main/ui/host_test`（C、ASan / UBSan 付き）、`ruby test/sequencer_test.rb`（picoruby-ui ディレクトリで）
- サンプル: [examples/sequencer.rb](../examples/sequencer.rb)（Pads 画面で Play / パターン / チェーン）、
  [examples/sequencer_events.rb](../examples/sequencer_events.rb)（`on_step` で拍のランプと周回、`on_pattern` でスクリプトによる曲構成、`on_edit` で画面の編集の表示）
- `ui_seq_init()` は UIManager の初期化で `ui_tempo_init()` の直後に呼ぶ。トランスポートのビットは Clock の次に取るので、
  Keys のビットは以前より 1 つ後ろにずれる（MIDI Devices のパッチは名前で保存しているので影響なし）
- スクリプト終了時（`UIManager::scriptCleanup`）は停止して `device:` を外すだけで、ソングは残る。
  `UI::Sequencer.new` の一時的なソングは、UI モードの `setup_sequencer` が自動保存のソングに戻す
- **UI モードはスクリプトが終わるたびに新しい VM で起動し直す**ので、自動保存を読むのは C の `restored`
  （電源投入後に一度読んだか）が 0 のときと、`temporary`（一時的なソング）が 1 のときだけ。それ以外は C の
  ソングをそのまま使う（読み直すと画面で作業中のソングを古い自動保存で上書きしてしまう）
- 書き出しは `ui_seq_dump_begin` がロックを取ってソングを丸ごとコピー（PSRAM、最大 70KB 程度）し、以後は
  コピーから 1 チャンクずつ渡す（Ruby がチャンクを書いている間の編集でファイルが食い違わないように）。
  読み込みは別のソングに組み立て、CRC と中身を検証してから `seq_engine_adopt` で差し替える
- `.mseq` のテスト: 往復（1 byte ずつ〜一括で読ませる）、途中で切れたファイル、1 byte でも壊れたファイル、
  違う種類 / 版のファイル、知らないチャンクの読み飛ばし、上限いっぱい（8192 ノート）のソング

複数のライン（トラック）を持つステップ / リアルタイム兼用のシーケンサー。
Keys と同じく **picoruby-midi のトランスポート**で、`MIDI.route` の送信元（midi out）にも
宛先（midi in）にもなる。外部の MIDI クロック（既定は `UI::Tempo` = `MIDI::Clock`）に従って進み、
レイアウト DSL で他の部品と同じ画面に並べられる。1 曲（ソング）に最大 16 パターンを持ち、
チェーンでつないで曲にできる。ソングは SD カードに保存でき、編集は自動保存される。

- 対応ボード: M5Stack CoreS3 / Tab5、CrowPanel（画面が必要）。Freenove は no-op スタブ
- 画面: `main/ui/screen_sequencer.cpp`（描画・タッチ・シート）
- モデル / 再生エンジン / トランスポート: `main/ui/ui_sequencer.cpp`（`ui_seq_*`）
- 再生・録音・直列化のロジック: `main/ui/seq_engine.c`（ESP-IDF 非依存、ホストテスト対象）
- Ruby API: `UI::Sequencer`（`mrbgems/picoruby-ui/mrblib/ui_sequencer.rb`）
- レイアウト: `sequencer`（`mrblib/ui_layout.rb`）

## 用語

| 用語 | 意味 |
|---|---|
| **ソング** | Sequencer が持つもの全部。ファイル 1 つ（`.mseq`）= ソング 1 つ |
| **ライン** | トラック。名前・出力チャンネル・入力方式などを持つ。**ソング全体で共有**（最大 16） |
| **パターン** | 全ラインのノートと長さの組。ソングに最大 **16 スロット** |
| **チェーン** | パターンの並び（と繰り返し回数）。曲の構成 |

## 位置づけ

| | Keys | Tombola | Sequencer |
|---|---|---|---|
| 操作 | 鍵を押す | 多角形を回す | グリッドに置く / 演奏を録る |
| MIDI を送るのは | C++（UI タスク） | C++（UI タスク） | **C++（シーケンサータスク）** |
| 時間の基準 | 指 | フレーム | **受け取った MIDI クロック（F8）** |
| トランスポート | 送信元 + 宛先 | — | **送信元 + 宛先** |
| 個数 | 1（シングルトン） | 1 | 1（シングルトン） |
| スクリプト終了後 | 既定値に戻る | 止まる | **残る**（後述「モデルの寿命」） |

発音を Ruby に置かない理由は Keys / Tombola と同じ（`UI.process` のポーリング間隔に
リズムを左右させない、`MIDI.route` の送信元は C で完結している必要がある）。
加えてシーケンサーはクロックの 1 tick ごとに動くので、UI タスク（フレーム単位）にも置けない。

## 基本の考え方: すべて「宛先として受け取ったもの」で動く

Sequencer の入力は 1 本のトランスポート（`send_packet`）だけで、そこに届くものを種類で振り分ける。

| 届くもの | 用途 |
|---|---|
| Timing Clock（F8） | 再生位置を進める |
| Start（FA）/ Continue（FB）/ Stop（FC）/ Song Position（F2） | 再生の開始・再開・停止・位置合わせ |
| Note On / Off | グリッドに置くノート（**最後に入力された和音**）の取り込みと、リアルタイム録音 |
| その他 | 無視 |

したがって「何に同期するか」「どこから演奏を受けるか」は **`MIDI.route` で Sequencer へ何を張るか**
だけで決まる。Sequencer 自身はクロックの送信元を区別しない（`send_packet` には送信元が渡らない）。

```ruby
MIDI.route(UI::Tempo, seq)                                   # Tempo のテンポ・Play / Stop に従う（既定）
MIDI.route(MIDIDevices.usb_midi_host, seq, channel: :clock)  # 外部機器のクロックに従う
MIDI.route(kb, seq, channel: (0..15).to_a)                   # 外部鍵盤のノートを入力に（クロックは通さない）
MIDI.route(keys, seq)                                        # 画面の鍵盤を入力に（Keys はクロックを出さない）
```

**クロックは 1 つの送信元からだけ届くようにする**（2 本から F8 が来ると 2 倍速で進む）。
`clock:` / `input:` はこれを守るように選択（`MIDI_ROUTE_SEL_*`）を組む:
`clock:` の送信元へは `:clock`、`input:` の送信元へはチャンネルのみ、両方が同じ機器なら
1 本のルートに `[0..15, :clock]` を載せる。

### BPM と Play / Stop

- テンポは送られてくる F8 の間隔そのもの。Tempo からルートしていれば Tempo の BPM
  （Tempo が Sync 中なら外部クロックに追従した BPM）になる。Sequencer は BPM を持たない
- **再生するのは Start / Continue を受けてから Stop を受けるまで**。Tempo は Start していなくても F8 を送り続ける
  （`docs/TEMPO.md`）ので、F8 だけでは進まない
- Tempo の ▶ Play、`UI::Tempo.start`、外部機器の Start のどれでも同じ
- **Sequencer 画面の Play / Stop**（ヘッダ 2 段目の右端、2026-10-25）は、クロックが Tempo から来ているなら Tempo の
  Start / Stop を送る（= Tempo 画面の Play と同じ。Tempo の他の宛先も一緒に動く）。別の機器からなら Sequencer だけに
  Start / Stop を渡し、その機器の F8 で進む。Sequencer にクロックを送るルートがどこにも無いとき（スクリプトなしで
  Home から開いた場合など）は、押した時点で Tempo → Sequencer のクロックだけのルートを張る（`ui_seq::ensure_clock()`。
  スクリプト終了時のルート消去で消え、次の Play でまた張る）
- Start: 位置を 0 に戻し、**Start の次の F8 を 1 拍目**として tick 0 のイベントを鳴らす（MIDI の規定どおり）。
  チェーンが有効ならチェーンの先頭から
- Stop: 鳴らしているノートをすべて note off（note on を送った宛先・チャンネルへ）。位置は保持
- Continue: 保持していた位置から再開
- Song Position Pointer（F2）: 停止中に受けたら位置を `SPP × 24 tick`（= 16 分音符単位）に合わせる。
  チェーンが有効ならチェーン全体の中の位置として解釈する（どのエントリの何周目のどこか）
- **各ラインはパターンの中で自分の長さでループする**（ラインの末尾に来たら先頭へ）。
  長さの違うラインを並べればポリメーターになる。パターンの末尾（後述）で全ラインが先頭にそろう

## データモデル

### 時間の単位

| 名前 | 値 | 備考 |
|---|---|---|
| tick | **96 PPQN**（F8 1 回 = 4 tick） | 内部の時間の最小単位 |
| step | `rate` で決まる（1/16 = 24 tick） | グリッドの 1 マス |
| bar | 384 tick（**4/4 固定**） | リアルタイムラインとパターンの長さの単位 |

F8（24 PPQN = 120 BPM で約 21ms）のままではリアルタイム録音の再現がもたつくので、内部は 96 PPQN で
持つ。F8 と F8 の間の 3 tick は、直近の F8 間隔から時刻を補間して刻む（後述「エンジン」）。

`rate` は `4`（1/4）/ `8` / `16`（既定）/ `32` と、3 連符 `:"8t"` / `:"16t"`。

### ソング

| 項目 | 内容 |
|---|---|
| `lines` | ライン（トラック）の設定。**既定 8 本、最大 16 本**（`SEQ_MAX_LINES`。MIDI のチャンネル数に合わせる） |
| `patterns` | パターン 16 スロット（`SEQ_MAX_PATTERNS`）。空のスロットは何も確保しない |
| `chain` | チェーン: `(パターン番号, 繰り返し 1..16)` の並び、最大 64 エントリ。`chain_on` で有効 |
| `name` | ソング名（保存したファイル名。未保存なら空） |
| `bpm` | 保存時の BPM（参考値。読み込み時に適用するかは選べる） |

### 実行時の状態（保存しない）

| 項目 | 内容 |
|---|---|
| `chord` | **最後に入力された和音**（ノートとベロシティの組、最大 8 音）。グリッドに置くノート。既定 `[60]` / 100 |
| `out_mask` | `device:` の送信先（Keys と同じ。`MIDI.route` の宛先とは別に OR される） |
| `running` / `song_tick` | 再生中か / Start からの tick 数（停止中も保持） |
| `play_pattern` / `pattern_tick` | 再生中のパターンと、その先頭からの tick |
| `next_pattern` | 切り替え予約（-1 で無し） |
| `chain_pos` / `chain_repeat` | チェーンのどのエントリの何周目か |
| `edit_pattern` | 画面・Ruby の編集対象（既定は `play_pattern` に追従） |
| `armed_line` | 録音対象のライン（1 本だけ。-1 で無し） |
| `dirty` / `autosave_dirty` | 明示的な保存以降の変更 / 自動保存以降の変更 |
| `selected_line` / `window` / `view` / `follow` | 画面の状態（後述） |

#### 「最後に入力された和音」の決め方

入力の Note On / Off で押鍵数を数え、**押鍵数が 0 の状態から最初に来た Note On で和音をリセット**し、
以後、全部離されるまでに来た Note On をすべて和音に加える（最大 8 音、超えた分は捨てる）。
離しても和音は残る（次に弾き始めるまで）。単音を弾けば 1 音の和音になる。

- チャンネルは見ない（入力ルート側で絞る）
- ノート番号の昇順で持つ。ベロシティはノートごと
- 録音中も同じように更新する
- Ruby / 画面から直接設定もできる（`seq.chord = [60, 64, 67]`）

### ライン（ソング全体で共有）

ラインの「何を・どこへ・どう入力するか」はパターンが変わっても同じなので、ソングに 1 組だけ持つ。

| 項目 | 内容 |
|---|---|
| `mode` | `:grid` / `:realtime`。切り替えても他方のデータは消さない（戻せば復活する） |
| `channel` | 出力チャンネル 0..15。**ライン間で重複してよい**。追加したラインの既定は他のラインが使っていない最小のチャンネル（全部使っていれば 0） |
| `mute` | true の間は鳴らさない（録音はできる） |
| `name` | 表示名（8 文字まで） |
| `velocity` | grid: `:input`（置いたときの和音のベロシティ、既定）か固定値 1..127 |
| `gate` | grid: 発音長の割合 1..100%（既定 100） |
| `quantize` | realtime: 録音時に開始位置を丸める単位（nil = しない、既定）。長さは丸めない |
| `rec_mode` | realtime: `:overdub`（既定）/ `:replace` |

#### ラインの追加・削除

- 起動時・`UI::Sequencer.new` の既定は **8 本**（チャンネル 0..7）
- 9 本目以降は**ユーザーの操作で追加する**: 画面のライン一覧の末尾の「＋ Line」行（16 本未満のときだけ出る）、
  または Ruby の `seq.add_line(...)`
- 削除はラインの設定シートの「Delete」（確認あり）か `seq.remove_line(i)`。**全パターンからそのラインのノートが消える**。
  後ろのラインが詰まって番号が変わる。鳴っているノートはその場で note off。最後の 1 本は消せない

### パターン

| 項目 | 内容 |
|---|---|
| `length` | パターンの長さ。`:auto`（既定。いちばん長いラインの長さ）か小節数 1..8 |
| ラインごと: `steps` | grid: **4 / 8 / 16 / 32 / 64** |
| ラインごと: `rate` | grid: 1 ステップの長さ |
| ラインごと: `bars` | realtime: ループの長さ 1 / 2 / 4 / 8 小節 |
| ラインごと: `program` / `bank_msb` / `bank_lsb` | パターンが始まるときにラインのチャンネルへ送る Program Change と Bank Select（各 0..127 / なし。後述「Program Change」） |
| ラインごと: `grid` / `rt` | ノート配列（下記） |

- 長さ（`steps` / `rate` / `bars`）をパターンごとにしたのは、フィルのパターンだけ短くする・Aメロだけ 32 ステップ、
  といった使い方のため
- ラインの長さ（tick）は grid なら `steps × step_ticks`、realtime なら `bars × 384`
- ライン内の位置は `pattern_tick mod ライン長`。パターンの長さに達したら `pattern_tick` を 0 に戻す
  （`length: :auto` なら最長のラインがちょうど 1 周したところ。短いラインは何周かしている）
- 空のスロットを再生すると、パターンの長さ 1 小節の無音として扱う

### ノートの保持形式（RAM と保存ファイルで共通）

grid に和音を置けるので、「ステップごとに固定個のノート枠」ではなく、**パターン × ラインごとの
ソート済みノート配列**で持つ。grid と realtime で同じ形にし、違うのは位置と長さの単位だけ。

```c
typedef struct {
    uint16_t pos;      // grid: ステップ 0..63 / realtime: tick 0..3071（8 小節 × 384）
    uint16_t len;      // grid: ステップ数 1..64 / realtime: tick 数 1..3072
    uint8_t  note;     // 0..127
    uint8_t  velocity; // 1..127
    uint8_t  flags;    // 予約（0）
    uint8_t  reserved; // 0
} seq_note_t;          // 8 byte。static_assert(sizeof(seq_note_t) == 8)

typedef struct {
    seq_note_t *v;     // (pos, note) の昇順
    uint16_t    n;
    uint16_t    cap;   // 32 個ずつ伸ばす。上限 SEQ_MAX_NOTES = 512
} seq_notes_t;

typedef struct {
    uint8_t     steps, rate, bars, flags;
    seq_notes_t grid, rt;
} seq_pattern_line_t;  // 20 byte（ポインタ 32bit）

typedef struct {
    uint8_t             length;   // 0 = auto、1..8 小節
    uint8_t             used;     // スロットが使われているか
    seq_pattern_line_t  lines[SEQ_MAX_LINES];
} seq_pattern_t;       // 約 330 byte。使われているスロットだけ確保
```

#### この形にした理由

| 案 | 長所 | 短所 |
|---|---|---|
| ステップ × 固定和音数の 2 次元配列（例 `[64][8]`） | ステップの参照が O(1) | 1 パターン 16 ライン分で 32KB を常に確保（16 パターンで 512KB）。和音数に上限が張り付く。realtime と別形式になる |
| **ソート済みのノート配列（採用）** | 使った分だけ。grid / realtime / 保存ファイルが同じ形。1 ステップに何音並べてもよい | ステップの参照は二分探索（64 ステップ × 数音なので無視できる） |
| note on / off のイベント列（SMF 風） | MIDI ファイルとそのまま対応 | 長さの編集・ドラッグで on と off の対応を探す必要がある。grid の「ステップと長さ」が直接表せない |

- **grid の位置・長さをステップ単位で持つ**ので、`rate` を変えてもパターンの形は変わらない（速さだけ変わる）。
  再生時に `pos × step_ticks` で tick に直す
- realtime の位置・長さを tick（96 PPQN）で持つのは、録音のタイミングをそのまま残すため。
  8 小節 = 3072 tick なので 16 bit に収まる

#### メモリ

- ノート配列とパターンは PSRAM（`heap_caps_malloc(MALLOC_CAP_SPIRAM)`、無ければ内部 RAM）。空の配列・スロットは何も確保しない
- 上限は配列ごと 512 ノート（和音は 8 音まで）に加え、**ソング全体で 8192 ノート（64KB）**（`SEQ_MAX_SONG_NOTES`）。
  配列ごとの上限だけだと 16 パターン × 16 ライン × 2 方式で最悪 2MB になるため。
  典型的なソング（8 ライン × 16 ステップ × 1〜3 音 × 数パターン）なら合計 10KB 前後
- どちらかの上限に達したら追加を拒否し、`:full` イベントを出す（録音はそこで止まる）
- ソング全体の固定部分（ライン設定 16 本・チェーン 64 エントリ・状態）は約 1KB。初めて使われたときに calloc する
  （.bss に置かない: `docs/MEMORY_ALLOCATION.md`）

#### 不変条件

- 配列は常に `(pos, note)` の昇順
- grid: `pos < 64`、`pos + len <= 64`（`steps` を超える分は再生・表示しないだけで保持する。`steps` を戻せば復活）
- grid: **同じ音程のノートは重ならない**。同じ音程のノートの途中に置くと、前のノートの長さをそこで切る。
  音程が違えば重なってよい（和音・ずらした重ね）
- realtime: `pos < bars × 384`。`len` はループの末尾をまたいでよい（再生では先頭へ回り込む）。
  overdub で同じ音程が重なることはあり得る（再生時に処理する。後述）

## Program Change（2026-10-25）

音色はパターン × ラインの設定として持つ（`seq_pattern_line_t` の `program` / `bank_msb` / `bank_lsb`、
なしは `SEQ_PROGRAM_NONE` = 0xFF）。A メロとサビでラインの音色を替える、といった使い方のため。
エンジンとファイルはホストのテスト済み、実機は未確認。

- **送るとき**: Start / Continue、別のパターンに切り替わったとき（予約・チェーン・小節頭）、曲を読み込んだとき。
  そのパターンの最初の tick に、**ノートより先に** CC#0（MSB）→ CC#32（LSB）→ Program Change の順で、
  そのラインのチャンネルへ（設定されているものだけ）。送り先はノートと同じ（`device:` とルートの宛先）
- **送らないとき**: 同じパターンがループしたとき・チェーンで同じパターンを繰り返したとき（音源によっては
  PC で音が切れるため）。`program` がなしのラインは何も送らない（前の音色のまま）
- **再生中のパターンの値を変えると、その場で送る**（画面やスクリプトで音色を試しやすいように）
- ミュート中のラインも送る（ミュートを解いたときに音色が合っているように）
- ラインのコピー（`seq_pline_copy` / `Line#copy_to`）とパターンのコピーで一緒に写る
- 画面: ラインの設定シートの 4 行目 **Prog -** / **Prog N**（タップで +10、127 を超えたらなし）/ **Prog +** /
  **Bank**（タップで MSB を +1、127 の次はなし。LSB は Ruby から）。ライン名の右に `P81` のように出す。
  **画面の番号は 1〜128**（音源の表示に合わせる）、Ruby とファイルは 0〜127（MIDI の値そのもの）
- Ruby: `l.program = 80`（nil でなし）、`l.bank = 3` / `[msb, lsb]` / nil、`l.program` / `l.bank`、
  `line(n, program:, bank:)`。`to_h` / `restore` にも入る

## パターンの切り替えとチェーン

### 切り替え

- **停止中**に別のパターンを選ぶと即座に切り替わる
- **再生中**は予約（`next_pattern`）になり、**今のパターンの末尾**で切り替わる。予約中のスロットは画面で点滅する。
  `switch: :bar` を選ぶと次の小節の頭で切り替わる（パターンの末尾まで待たない）
- 切り替わった瞬間に鳴っているノートはそのまま長さどおり鳴らす（note off の予定は発音中のノート表が持っている）
- 録音の arm は切り替え後のパターンの同じラインに引き継ぐ
- チェーンが有効なときに手でパターンを選ぶと、チェーンは**オフ**になる（手動が優先）

### チェーン

- エントリは `(パターン番号, 繰り返し回数)`。最大 64 エントリ。同じパターンが何度出てきてもよい
- 有効なら、パターンの末尾ごとに繰り返し回数を数え、尽きたら次のエントリへ。**最後のエントリが終わったら先頭へ戻る**
- Start でチェーンの先頭から。Continue は途中から。SPP はチェーン全体の中の位置（各エントリの長さ × 回数を足していく）
- 空のスロットを指すエントリは 1 小節の無音

### 編集対象のパターン

- 既定では画面・Ruby の編集対象は**再生中のパターン**（チェーンで移れば画面も移る）
- 再生中に次のパターンを準備したいときは、スロットを長押しして「Edit」を選ぶと、そのパターンだけを編集対象にできる
  （`edit_pattern` が固定され、ヘッダに「Edit 3 / Play 1」。再生位置の線は出さない）。もう一度再生中のスロットを
  選べば追従に戻る
- コピー・ペースト・クリアはスロットの長押しメニューから（パターン全体）。ラインの設定シートからは
  「このラインだけ」のコピー・ペースト・クリア

## 保存と自動保存

### 何を保存するか

| 保存する | 保存しない |
|---|---|
| ソング名・BPM（参考値） | ルート（MIDI Devices のパッチは NVS に別途保存されている） |
| ライン設定（本数・名前・チャンネル・方式など） | 和音・arm・再生位置・切り替え予約 |
| 全パターン（長さ・ラインごとの steps / rate / bars・grid / realtime 両方のノート） | 画面の状態（選択中のライン・表示位置・view・Follow） |
| チェーンとその有効 / 無効、最後に選んでいたパターン | |

### ファイル形式（`.mseq`）

**独自のバイナリ・チャンク形式**にする。

| 候補 | 判断 |
|---|---|
| JSON / Ruby リテラル | 人が読めるが、最大で数百 KB になり、mruby で一度にパースするとヒープを大きく食う。却下（スクリプトから組む用途は Ruby API の `to_h` / `restore` で別に用意） |
| **独自バイナリ（採用）** | RAM の `seq_note_t` 配列をそのまま書ける。読み込みは配列への `memcpy` + 検証だけ。チャンクに分けて版を足しやすくする |

全体はリトルエンディアン。チャンクは `id(4 byte ASCII) + size(u32) + payload`。
**知らないチャンクは size で読み飛ばす**（新しい版のファイルを古いファームでも読める範囲で読む）。

```
offset  size  内容
0       4     "MSEQ"
4       2     version (u16) = 1
6       2     reserved = 0
8       ...   チャンクの並び（HEAD → LINE* → CHAN → { PATN → PLIN* → GRID* → RTEV* }* ）
末尾    12    "CRC " + size(=4) + CRC32（先頭からこのチャンクの直前まで）
```

| チャンク | 個数 | payload |
|---|---|---|
| `HEAD` | 1 | `ppqn u16`(=96)、`line_count u8`、`flags u8`（bit0 = chain_on）、`bpm_x100 u16`（0 = 不明）、`current_pattern u8`、`reserved u8`、`name[16]`（NUL 埋め） |
| `LINE` | ライン数 | `index u8`、`mode u8`、`channel u8`、`flags u8`（bit0 = mute）、`velocity u8`（0 = `:input`）、`gate u8`、`quantize u8`（0 = なし）、`rec_mode u8`、`name[8]`（計 16 byte） |
| `CHAN` | 0..1 | `count u8`、`reserved u8`、`(pattern u8, repeat u8) × count` |
| `PATN` | 使っているスロット数 | `index u8`、`length u8`（0 = auto） |
| `PLIN` | PATN ごとに 0..ライン数 | `pattern u8`、`line u8`、`steps u8`、`rate u8`、`bars u8`、`flags u8`、`program u8`、`bank_msb u8`、`bank_lsb u8`（各 0xFF = なし。Program Change を足す前の 6 byte の PLIN は「なし」として読む） |
| `GRID` | PATN ごとに 0..ライン数 | `pattern u8`、`line u8`、`count u16`、`seq_note_t[count]` |
| `RTEV` | 同上 | 同上（realtime のノート） |

- 構造体を丸ごと書くのは `seq_note_t` の配列だけ（8 byte・パディングなし・リトルエンディアンの
  ESP32 / ホストで同じ並び）。他のチャンクはフィールドごとに読み書きして、コンパイラのパディングに依存しない
- 空のスロット・既定値の `PLIN`・空の配列は書かない
- 典型的なソングで数 KB、最大でも約 70KB（ソング全体の上限 8192 ノート × 8 byte + ヘッダ類）
- 読み込みの検証: マジック・版・CRC、各値の範囲、配列のソートと不変条件、ソング全体のノート数。
  崩れていれば**何も変えずに**失敗を返す（読み込みは別のモデルに組み立ててから差し替える。下記）

### 読み書きの経路

ファイル I/O は PicoRuby の VFS でしか `/sd` に届かない（C の `fopen` は不可。`CLAUDE.md`）。そのため:

- **直列化・検証は C**（`seq_engine.c`。ホストテスト可能）、**ファイル I/O は Ruby**
- 保存: Ruby が `UI._seq_dump_begin` → `_seq_dump_chunk` を繰り返し（1 回 = 1 本の String、ノート配列は 1 本ずつ）→
  ファイルへ追記。ピークは 1 配列分（最大 4KB）
- 読み込み: Ruby がチャンク単位で読み、`UI._seq_load_chunk(str)` で C の「読み込み中のモデル」に積む。
  最後に `_seq_load_commit` で検証して差し替える（失敗したら `_seq_load_abort`、元のソングはそのまま）
- 差し替えは再生中でもよい。鳴っているノートは送った宛先へ note off してから差し替え、`current_pattern` の先頭から続ける
- **書き込みの手順**（FAT の `f_rename` は既存のファイルへ上書きできないため）:
  1. `name.mseq.tmp` に全部書いて閉じる
  2. `name.mseq` があれば `name.mseq.bak` へ rename
  3. `name.mseq.tmp` を `name.mseq` へ rename
  4. `name.mseq.bak` を unlink

  読み込みで `name.mseq` が無く `.bak` があれば `.bak` を読む（2 と 3 の間で電源が落ちた場合）

### 置き場所と画面からの操作

- 既定のディレクトリは **`/sd/patterns/`**（無ければ作る）。SD が無ければ littlefs の `/patterns/`
- ヘッダ 2 段目の **File** ボタン（ソング名を表示。未保存なら `groove*`、保存の結果などのメッセージは 3 秒間ここに出る）で
  シートを開く: 左にファイル一覧（Ruby が `Dir.open` で列挙して C に渡す。`_` で始まるファイルは出さない。
  いまのソングは黄色）、右にボタン **Save**（いまの名前で上書き。名前が無ければ `SONG001` から空いている番号）、
  **Save new**（新しい `SONGnnn`）、**Load**、**New**（既定の空ソング）、**Delete**（2 回押しで確定）、**Close**
- 画面の操作は C 側に「要求」（操作とファイル名）を 1 件積むだけで、実際の I/O は Ruby の
  `UI::Sequencer._poll` が行う。`_poll` は `$__midi_filter_hooks__` に載せてあり、`MIDI.run_filters` の先頭で
  呼ばれる（`MIDI.sleep_ms` が 50ms ごとに呼ぶ。`UI::MIDIDevices._poll` と同じ）。UI モードのループは
  `MIDI.sleep_ms(100)` を回しているので、スクリプトが無いときも動く。スクリプト実行中は、そのスクリプトが
  `MIDI.sleep_ms` か `MIDI.run_filters` を回している間だけ動く
- 未保存の変更があるまま Load / New しようとしたら確認を出す（自動保存があるので作業は失われないが、
  名前付きのファイルには入っていないことを知らせる）

### 自動保存

電源を切っても作業を失わないよう、**現在のソングを `_autosave.mseq`**（同じディレクトリ）に自動で書く。

- **名前付きのファイルには自動で書かない**（試しに崩した状態で元のファイルを上書きしないため）。
  名前付きへは明示的な Save だけ。自動保存のファイルにはソング名と「明示的な保存以降に変更があるか」（`dirty`）も
  入れるので、復元後もヘッダの `groove*` がそのまま出る（`HEAD.flags` bit1 = dirty）
- **いつ書くか**（`autosave_dirty` が立っているとき）:
  - 最後の変更から **5 秒**何も変わらなかったとき
  - 変更が続いていても、前回の自動保存から **60 秒**経ったとき（録音中など）
  - Stop を受けたとき
- **起動時**、どのモードでも `main_task_base.rb` の `setup_sequencer` が `_autosave.mseq` を読む
  （`load_filters` と `setup_midi_devices` の並び。読めなければ既定の空ソング）。`.bak` の扱いは通常の読み込みと同じ
- 書き込みは Ruby（`_poll`）が行うので、`MIDI.sleep_ms` / `MIDI.run_filters` を回していないスクリプトの実行中は書かれない。
  スクリプトが終われば UI モードのループで書かれる。**その間に電源を切ると、最後の自動保存以降の変更は失われる**
- SD への書き込み中も再生は止まらない（seq タスクは VM とは別のタスクで、ロックを持つのは書き出しの最初に
  ソングをコピーする間だけ）
- 書き込みに失敗したら（SD が抜けたなど）File ボタンに「Autosave failed」を出し、10 秒おきに再試行する
- 判定は C（`autosave_due`）: Ruby が見に来るたびに編集の回数を比べ、変わっていれば時刻を記録する

#### スクリプトと自動保存

スクリプトがコードでソングを組む（`UI::Sequencer.new`）たびに自動保存されると、ユーザーが画面で作っていた
ソングが自動保存ファイルから消えてしまう。そこで:

- **`UI::Sequencer.new` で作ったソングは一時的なもの**として扱い、自動保存しない（`autosave: true` で自動保存する）。
  スクリプトが終わったら、自動保存ファイルからユーザーのソングを読み直す
- **`UI::Sequencer.current`** はユーザーのソングをそのまま使う（スクリプトから編集しても自動保存される。終了後も残る）
- スクリプトが明示的に `seq.save` / `seq.load` したものは、その時点からユーザーのソングとして扱う（自動保存の対象に戻る）

## モデルの寿命

保存したソングを UI から読んで鳴らせるよう、Sequencer は **Tempo と同じく常にある内蔵の部品**にする。

- モデルは初めて使われたときに作る（起動時の `setup_sequencer`）。中身は自動保存のソングか、既定の 8 ラインの空ソング
- **スクリプト終了時に既定値へ戻さない**。止まって（クリーンアップの Stop / All Notes Off で）、ソングは残る。
  ただし `UI::Sequencer.new` の一時的なソングだった場合は、上記のとおり自動保存のソングに戻る
- Home にタイルを置く（スクリプトが無くても開ける）。クロックの入力は MIDI Devices の内蔵デバイスのパッチ
  （既定で **Tempo → Sequencer の Clock** を張る。初めて起動したときに 1 回だけ保存済みパッチに足すので、
  外せば外したまま。docs/UI_MIDI_DEVICES.md の「既定パッチ」）。スクリプトの `clock:` /
  `input:` で張ったルートはスクリプト限りで、終了後は保存済みのパッチに戻る（`setup_midi_devices` が張り直す）

## エンジン（シーケンサータスク）

```
 [MIDI::Clock esp_timer]   [MIDI 入力タスク]     [UI タスク / PicoRuby タスク]
   F8/FA/FC を dispatch      ノートを route         編集・読み込み（ui_seq_*）
        │                       │                        │
        └──── send_packet ──────┘                        │（ロックして書く）
              （時刻を付けてキューへ積むだけ）            │
                    ▼                                    ▼
            [seq タスク  Core 1 / 優先度 5]  ←── モデル
              ├ F8 → 補間 tick のタイマを張る（esp_timer one-shot → タスク通知）
              ├ tick ごとに各ラインの note off / note on を集める
              ├ パターンの末尾で切り替え予約・チェーンを処理
              ├ MIDI_route_dispatch(seq_bit, out_mask | MIDI_route_targets(...))
              └ 和音の取り込み・録音（Note On/Off を補間 tick に換算して確定）
```

- **`send_packet` は時刻（`esp_timer_get_time()`）を付けてキューに積んで返るだけ**。呼ぶのは
  クロックの esp_timer タスク（Tempo）や入力タスク（外部機器）で、そこから他のトランスポートへ送ると
  ルート処理の中で再びルート処理を呼ぶことになる。キュー経由にして入れ子を避け、呼び出し元も待たせない
- seq タスクは Core 1、**優先度 5**（入力タスク 4 より上。F8 → 発音の遅れを最小に）。1 tick の処理は短い
  （各ラインのカーソルから今の tick のノートを拾うだけ）が、待ちが 0 tick になる経路を作らないこと
  （`CLAUDE.md` の入力タスクの教訓）
- **補間 tick**: F8 を受けたら直近の F8 間隔 T（移動平均）から `T/4, 2T/4, 3T/4` 後に esp_timer の one-shot で
  tick を進める（`CONFIG_FREERTOS_HZ=100` なので `vTaskDelay` では刻めない）。次の F8 が早く来たら、
  残っている補間 tick をその場で全部処理してから F8 の tick に進む。**F8 より先には進まない**。どのイベントも飛ばさない
- 1 tick の処理順: (1) パターンの末尾なら切り替え（予約 / チェーン）してカーソルを取り直す →
  (2) その tick で終わるノートの note off → (3) その tick で始まるノートの note on。
  **note off を先、note on を後**にするので、同じ音が続いても切れ目が出る
- 再生のカーソル: ラインごとに「次に鳴らすノートの添字」を持ち、ラインの末尾で 0 に戻す。
  編集・読み込み・切り替えでカーソルは二分探索で取り直す
- 発音中のノート表（全体で 128 件）: （ライン, 宛先, チャンネル, ノート, off の tick）。
  note off は **note on を送ったときの宛先・チャンネル**へ送る（再生中に `channel` や `device:` を変えても、
  パターンが切り替わっても音が残らない）。同じライン・チャンネル・音程がすでに鳴っていたら、off を送ってから on し、
  前の off の予定は捨てる（realtime の重なり）
- 送信は自分で送る送信元なので `MIDI_route_dispatch()` を使う（filter / listener に届くように）
- 編集は短時間のミューテックスで守る。再生中の編集は次にその位置を通るときから効く。
  鳴っている最中のノートを消したら、その場で note off を送る

### リアルタイム録音

1. 録音するラインを選んで **REC を arm**（`armed_line`。1 本だけ）
2. 再生中に入力の Note On を受けると、その時刻を補間 tick に換算して「開いたノート」にする
   （入力のチャンネルは見ない。出力はラインの `channel`）。録音先は**再生中のパターン**
3. 同じノートの Note Off（velocity 0 の Note On を含む）で長さを確定し、配列に挿入する
4. ループの末尾をまたいで押し続けたノートは、長さが末尾を越えたまま確定する（再生では先頭へ回り込む）
5. 停止・disarm・パターンの切り替えの時点で開いているノートはそこで閉じる
6. `:replace` は arm 後に再生位置が通過した範囲の既存ノートを消しながら録る（1 周したら `:overdub` と同じ）

- 録音中もそのラインの既存ノートは鳴る。**いま押さえていて録音中のノートと同じ音は、再生位置が
  そのノートに来ても鳴らさない**（二重発音を防ぐ）
- **停止中は録音しない**（カウントインは入れない）
- 入力をそのまま音源へ流すのは Sequencer の仕事ではない（Keys と同じく、受けたものは転送しない）。
  弾いた音も聞きたいなら `MIDI.route(kb, synth)` を別に張る

#### 実装メモ（2026-10-25）

- **arm できるのは realtime のラインだけ**（`seq_set_armed` は grid のラインに false。画面は File ボタンに
  「Rec: realtime lines」、Ruby は ArgumentError）。arm 中のラインを grid にすると disarm する。arm は保存しない
  （ファイルの読み込み・`reset` で外れる）。スクリプトの終了では外さない（停止で開いているノートは閉じる）
- **時刻**: 入力は「最後に鳴らした tick」に置く。seq タスクは Note On / Off を F8・補間 tick と同じキューから
  順に取り出すので、tick 単位の順序は保たれる。そのうえで受信時刻が最後の tick から **tick の半分**
  （F8 周期 / 8）を過ぎていれば次の tick に置く（`seq_engine_input_at(..., next)`）。Start の後、最初の F8 の前に
  来たものは tick 0
- 開いたノートは最大 16（`SEQ_REC_OPEN`）。超えた分と配列・ソングの上限で入らなかったものは `rec_dropped` に数える
  （`:full` イベントはまだ無い）
- 長さは note off までの tick 数（最小 1）。**1 周より長く押さえたら 1 周の長さ**にする
- quantize は開始位置を**いちばん近い**単位に丸める（ループの末尾へ丸めたら先頭）
- パターンの切り替えで開いているノートは切り替えの tick で閉じる。ただし**切り替えの tick ちょうどに始まった
  ノート**（次のパターンの頭を少し早めに弾いた）は、新しいパターンの先頭へ移す
- replace は録音が始まった tick（arm した・Start / Continue・パターンが替わった・rec_mode を変えた）から 1 周、
  再生位置が通る tick のノートを消してから鳴らす（消したノートは鳴らない）。quantize で再生位置より**先**へ
  丸めたノートは、確定したときにそこまでを先に消し、通過のときに消し直さない
- 録音したノートの確定は `edits` を進めるので、自動保存の対象になる（録音中は 60 秒ごと）

## 画面

### 構成（Tab5 / CrowPanel の全画面。CoreS3 は後述）

```
┌──────────────────────────────────────────────────────────────────────┐
│ groove* │ Chord C3 E3 G3 │                         │ File │ ● REC     │ ← ヘッダ 1 段目
│ [1][2][3][4][5][6]…[16] │ Chain ▶ │ ◀ 9-24/32 ▶ │ Follow │ 16/8     │ ← ヘッダ 2 段目（パターン・表示位置）
├──────────────┬───────────────────────────────────────────────────────┤
│ 1 Kick   G M │ ▉   ·   ·   ·   ▉   ·   ·   ·   ▉   ·   ·   ·   ▉ ▉   │
│   ch10 16x16 │                                                       │
│ 2 Keys   G   │ ▬▬▬▬▬▬▬     ·   ·  ▬▬▬▬▬▬▬▬▬▬▬▬▬  ·   ·   ·          │ ← 和音は音程ごとの横棒
│   ch1  32x16 │ ▬▬▬▬▬▬▬            ▬▬▬▬▬▬▬▬▬▬▬▬▬     │← 再生位置        │
│ 3 Lead   R ● │  ▂  ▅▅▅     ▃▃       ▇      ▂▂▂                     │ ← 縮小ピアノロール
│   ch1  2bar  │                                                       │
│      ...     │                                                       │
│ ＋ Line      │                                                       │ ← 16 本未満のとき
└──────────────┴───────────────────────────────────────────────────────┘
  ライン見出し: 番号・名前・方式(G/R)・ミュート(M)・arm(●)、チャンネル・長さ（このパターンの）
```

- パターンのスロット `[1]`〜`[16]`: 再生中は塗り、予約中は点滅、編集対象（Edit 固定中）は枠、空は暗く
- CoreS3 はヘッダを 1 段にし、スロットは「P3 ▾」のボタン（タップでスロットのシート）、Chain / view / Follow は
  File と同じシートにまとめる
- **1 画面に入るのは 8 ステップか 16 ステップ**（`view`。既定は Tab5 / CrowPanel 16、CoreS3 8）。
  4 ステップのラインは `view` 幅いっぱいに 4 マスを広げて描く。32 / 64 ステップは表示位置を送って見る

### 表示位置（window）と 1/2 ページ送り

- 表示位置 `window` は **`view / 2` ステップ単位**（view 16 なら 8 ステップずつ、view 8 なら 4 ステップずつ）。
  ◀ ▶ は **1/2 ページ**ずつ送る。表示は「9-24/32」のように見えている範囲
- 1/2 ずつずらせるので、ページ境界をまたぐ長さのノートも、境界が真ん中に来る位置へ送ってからドラッグで付けられる
  （**ドラッグは見えている範囲の右端で止まる**）
- `window` は全ラインで共通。各ラインは `window` を自分のステップ数で割った余りから `view` ステップ分を映す。
  ラインの末尾を越える分は先頭から続けて描き、境界に区切り線を入れる（ドラッグはラインの末尾でも止まる）
- **Follow** が ON なら、選択中のラインの再生位置が見えている範囲の 3/4 を過ぎたところで 1/2 ページ送る
- **列は時間でそろえない**（ラインごとに `rate` と `steps` が違えば、同じ x が同じ時刻とは限らない）。
  realtime のラインは 1 ステップ = 1/16 として同じ `window` を当てる（view 16 なら 1 小節分）
- 再生位置はラインごとの縦線（とそのステップのマスの明るい枠）
- 見えるライン数は領域の高さで決まり（1 ラインの最小高さ Tab5 64px / CoreS3 32px）、溢れたら縦スクロール
- パターンが切り替わったら `window` は 0 に戻す（Follow のときだけ）

### grid の描き方（和音）

ラインの行の中を**縮小ピアノロール**として描く: そのラインのノートの最低音〜最高音を行の高さに割り付け、
ノートを「開始ステップから長さ分の横棒」で描く（realtime と同じ描き方で、grid はマスの区切り線が付く）。

- 行の高さに対して音域が広すぎるときは、同じステップから始まる音を 1 本の太い棒にまとめ、棒に音数を描く（例「3」）
- マスが十分広ければ、そのステップから始まる最低音の音名を描く（和音なら「C3+2」）
- ヘッダの **Chord** は最後に入力された和音。タップで試聴（選択中ラインのチャンネルで 1 回 trigger）

### タッチ操作

パターンのスロット:

| 操作 | 結果 |
|---|---|
| タップ | そのパターンへ切り替え（停止中は即座、再生中は予約）。チェーンはオフになる |
| 長押し | メニュー: Edit（編集対象に固定）/ Copy / Paste / Clear / Length（auto・1〜8 小節） |

**Chain** ボタン: タップで有効 / 無効、長押しでチェーンの編集シート（エントリの追加・削除・並べ替え・繰り返し回数）。

ライン見出し:

| 操作 | 結果 |
|---|---|
| タップ | ラインを選択（REC の対象・Follow の基準） |
| `M` / `●` の位置をタップ | ミュート / disarm（`●` は arm 中のラインにだけ出る。arm はヘッダの REC） |
| 長押し | ラインの設定シート（方式・チャンネル・steps / rate / bars・velocity・gate・quantize・rec mode・名前・このラインの Copy / Paste / Clear・Delete） |
| 「＋ Line」をタップ | ラインを末尾に追加して選択 |

grid のマス（当たり判定は**列＝ステップだけ**。音程の高さは見ない）:

| 操作 | 結果 |
|---|---|
| 何も始まっていないマスをタップ | **和音の全音**を長さ 1 で置く（同じ音程が鳴っている途中なら、その長さをここで切る） |
| 何も始まっていないマスを押してドラッグ | 押したマスから指のあるマスまでを長さにして和音を置く（ドラッグ中はプレビュー。見えている範囲の右端とラインの末尾で止まる） |
| ノートが始まっているマスをタップ | そのステップから始まるノートを全部消す |
| ノートが始まっているマスを押してドラッグ | そのステップから始まるノート全部の長さを変える（先頭は動かさない） |
| ノートが始まっているマスを長押し | そのステップから始まるノートを和音で置き換える（長さは元の最長に合わせる） |

1 音ずつの追加・削除・別々の長さは Ruby API で行える（画面では和音を 1 単位として扱う）。

realtime のライン: 縮小ピアノロールを描くだけで、マスの編集はしない（v1）。Clear は設定シートから。

録音（2026-10-25）:

- ヘッダ 1 段目の Chain の左に **REC**。タップで選択中のラインを arm（arm 中なら disarm）。表示は `REC 3`
  （arm 中のライン）、色は録音中（arm + 再生中）が赤・停止中の arm が暗い赤。ノートを押さえている間は白い枠。
  何も arm していなくて選択中のラインが grid なら灰色の文字
- arm 中のラインの見出しには `M` の左に赤い丸（録音中は明るい赤）。タップで disarm
- realtime のラインの設定シートでは、grid 用の **Steps / Rate** のボタンが **Quant**（off → 1/4 → 1/8 → 1/16 →
  1/32 → 1/8T → 1/16T）と **Overdub / Replace** になる

### 描画

- 共有ストリップ（`ui_strip::paint` / `compose`）を使う。描画は `ui_gfx()` へ（`CLAUDE.md` の UI 規約）
- 再生位置の移動は変わったマスの矩形だけ描き直す（Keys の `take_dirty` と同じ方式で、ラインごとに
  「再生位置が動いた」「データが変わった」ビットを持つ）。パターンの切り替えはライン領域を
  `ui_strip::repaint_content()` で描き直す
- **UI タスクのスタックは 3.5KB** なので、ノート配列（最大 4KB）をローカルにコピーしない。
  描画は見えている範囲のノートだけを、ロックを取ってヒープの Scratch に 1 フレーム 1 回写して使う
- 画面番号は `UI_SCREEN_SEQUENCER`（= 12、`ui_screen_index_t` の末尾）、Ruby は `:sequencer` /
  `UI.sequencer` / `UI::SCREEN_SEQUENCER`。`MENU_ORDER` に載せ、Home のタイル状態は
  `"groove P3 ▶"`（ソング名・再生中のパターン・再生中）などを `getTileStatus()` で返す

## Ruby API

```ruby
seq = UI::Sequencer.new(                 # 一時的なソング（自動保存しない。終了後はユーザーのソングに戻る）
  lines: 8,                              # 1..16（既定 8）
  clock: UI::Tempo,                      # クロックの送信元（既定）。外部機器、nil（張らない）も可
  input: MIDIDevices.usb_midi_host,      # ノートの入力元（midi in）。配列可、nil で張らない
  device: MIDIDevices.amy,               # 送信先（midi out）。MIDI.route とは別。配列可
  view: 16,                              # 1 画面のステップ数 8 / 16
  autosave: false)                       # true でユーザーのソングとして扱う
# seq = UI::Sequencer.current           # ユーザーのソングをそのまま使う（リセットしない）

seq.line(0, name: "Kick", mode: :grid, channel: 9)
seq.line(2, name: "Lead", mode: :realtime, quantize: 16)
seq.add_line(name: "Pad", channel: 3)    # => Line（16 本で false）
seq.remove_line(7)
seq.show                                 # = UI.sequencer / UI.show(:sequencer)
```

`clock:` / `input:` で張ったルートは `seq.clock = ...` / `seq.input = ...` で張り直せ、スクリプト終了時に
他のルートと一緒に消える（その後は MIDI Devices の保存済みパッチ）。

### パターンとチェーン

```ruby
a = seq.pattern(0)                       # => UI::Sequencer::Pattern（空なら作る）
a.length = :auto                         # または 1..8（小節）
a.copy_to(1); seq.pattern(1).clear

seq.play_pattern = 1                     # 停止中は即座、再生中は予約（switch で変更可）
seq.switch = :pattern                    # :pattern（既定）/ :bar
seq.playing_pattern / seq.next_pattern   # => 0 / 1（予約が無ければ nil）
seq.edit_pattern = 2                     # 編集対象を固定（nil で再生中に追従）

seq.chain = [[0, 2], [1, 1], [0, 2], [2, 1]]   # (パターン, 繰り返し)
seq.chain_on = true
```

### ラインの操作

ラインの内容は**パターンごと**。`seq.line(n)` は編集対象のパターン（`edit_pattern`、既定は再生中）のラインを指す。
特定のパターンのラインは `seq.pattern(p).line(n)`。

```ruby
l = seq.pattern(0).line(1)               # => UI::Sequencer::Line
l.channel = 2; l.mute = true             # ライン設定（全パターン共通）
l.steps = 32; l.rate = :"16t"            # 長さ（このパターンだけ）
l.program = 80; l.bank = [0, 3]          # このパターンが始まるときに送る音色（nil でなし）

# grid（位置・長さはステップ単位）
l[0] = [48, 52, 55]                      # ステップ 0 に和音（長さ 1、velocity はライン設定）
l.set(4, notes: [38], length: 2, velocity: 90)
l.add(4, note: 45, length: 4)            # 1 音だけ足す
l.remove(4, note: 45); l.clear(4); l.clear
l[4]                                     # => [{ note: 38, length: 2, velocity: 90 }, ...]（そのステップから始まるもの）
l.pattern("x...x...x...x.x.", notes: [36])  # 文字列で一括（x = 置く、. = 空、- = 前を伸ばす）

# realtime（位置・長さは tick = 96 PPQN）
l.arm; l.disarm; seq.armed               # => Line / nil
l.notes                                  # => [{ pos:, len:, note:, velocity: }, ...]（方式に応じた配列）
l.notes = [...]                          # 丸ごと差し替え
```

### 保存

```ruby
seq.save("groove")                       # /sd/patterns/groove.mseq（フルパスも可）
seq.load("groove", tempo: true)          # tempo: true で保存時の BPM を UI::Tempo.bpm に
UI::Sequencer.songs                      # => ["groove", "SONG001", ...]（_ で始まるものは除く）
seq.name / seq.dirty?

h = seq.to_h                             # スクリプトに書き込める形（Hash / Array）
seq.restore(h)                           # ソングをスクリプトの中に持つとき
```

### 状態・イベント

```ruby
seq.chord                                # => [60, 64, 67]（最後に入力された和音）
seq.chord = [60, 64, 67]                 # 直接設定。seq.chord = 60 でも可
seq.running? / seq.position              # => { pattern:, tick:, bar:, beat: }
seq.window = 8; seq.follow = true; seq.view = 8

seq.on_step { |e| ... }                  # { line:, step:, pattern: }。ブロックを渡すまでキューに載らない
seq.on_step(line: [0, 2]) { |e| ... }    # 見るラインを絞る（C で絞るのでキューに載らない）
seq.on_pattern { |e| ... }               # { pattern:, from: }（パターンが切り替わったとき）
seq.on_edit { |e| ... }                  # 画面での編集 { pattern:, line: }（line nil = パターン全体・ソング）
seq.off(:step) / seq.off(block) / seq.off   # 外す。戻り値は外した数
```

イベントの実装（2026-10-25）:

- 3 つとも UI イベント（`UI_EVENT_SEQUENCER`、Ruby の `:sequencer`）で届き、ブロックは **`UI.process`** で走る
  （`MIDI.sleep_ms` だけのループでは走らない）。ハンドラはスクリプト限り（終了時に C の `ui_seq_cleanup` が全部止める）
- **step**: エンジン（`seq_engine.c` の `step_events`）が、`step_events`（ラインのビット）に入ったラインがステップに
  来た tick に `out.event` を呼ぶ。step は grid ならそのライン自身のステップ、realtime なら 16 分音符の番号。
  ミュート中のラインと空のスロットも進むので届く。1 拍に何通も来るので、**UI イベントキュー（32）が半分以上
  埋まっている間は捨てる**（パッドやノブのイベントの場所を残すため。`UI.process` を回すのが遅いスクリプトは
  取りこぼす）
- **pattern**: `play_pattern` が変わったとき（予約・チェーン・小節頭・停止中の選択・読み込み）。`from` は前のパターン
- **edit**: 画面の操作で変わったときだけ（マスの置く / 消す / 長さ / 和音の置き換え、ミュート、ライン設定シート、
  Paste、ラインの追加・削除、パターンメニューの Paste / Clear / Length、チェーンの編集）。Ruby からの変更と録音は出さない。
  ラインの削除・チェーンは `line: nil`
- C では `ui_seq_set_i` の `step_events`（マスク）/ `pattern_events` / `edit_events`（0 / 1）。Ruby は
  ハンドラの増減のたびに `UI::Sequencer._sync_events` で書く

再生・停止は Sequencer の API ではなく `UI::Tempo.start` / `.stop`（クロックの送信元が外部機器なら、その機器）。

### トランスポートとして

`transport_id` / `send_packet` / `connected?` / `bytes_available`(0) / `read_available`(nil) /
`device_info` を持つ、Keys と同じ形のトランスポート。

- **送信元（midi out）**: 再生したノートを `device:` の送信先と、Sequencer のビットから張られた全ルートの宛先へ、
  1 回の `MIDI_route_dispatch()` で送る。ルートの `channel:` も効くので、ラインのチャンネルで宛先を分けられる:
  ```ruby
  MIDI.route(seq, MIDIDevices.sam2695, channel: 9)   # ドラム（ch10）だけ SAM2695 へ
  MIDI.route(seq, MIDIDevices.amy, channel: [0, 1])
  ```
- **宛先（midi in）**: 上の「基本の考え方」のとおり
- 登録表のビットは `MIDI_transport_register()` で取り、`MIDI_route_set_source()` で送信元として宣言する
- **登録表の空きに注意**: マスクは 8 ビットで、USB Host / UART / USB Device / Clock / AMY / Keys で 6 本使っている。
  Sequencer で 7 本目（残り 1）。足りなくなったら登録表の拡張（`uint16_t` 化）が先に要る
- MIDI Devices 画面には内蔵デバイス `seq`（"Sequencer"）として IN / OUT の両方に載せ、
  画面で張ったパッチは他の内蔵デバイスと同じく NVS に保存する

## レイアウト

```ruby
UI.layout("Groove", cols: 4, rows: 4) do
  areas <<~GRID
    s s s t
    s s s t
    s s s k
    y y y y
  GRID
  sequencer area: :s                     # 省略時は UI::Sequencer.current。sequencer: seq で渡しても可
  tempo area: :t
  knob area: :k, label: "Cutoff" do |v| ... end
  keys area: :y, octaves: 2              # 画面の鍵盤で和音を選ぶ / 録音する
end.show
MIDI.route(UI::Keys, UI::Sequencer.current)   # 鍵盤を Sequencer の入力に
```

- モデルが 1 つなので 1 レイアウトに 1 つまで（`SINGLETONS` に `:sequencer` を追加）
- `ui_widget_kind_t` に `UI_WIDGET_SEQUENCER` を足し、他の画面ビューと同じく表示中だけ画面オブジェクトを
  セルに狭めて借りる（`setArea()` / `resetArea()`）
- 最小サイズの目安は 240x96px（警告のみ）。狭いセルでは CoreS3 と同じ 1 段のヘッダにし、見えるライン数を減らす
- レイアウトの Sequencer と Sequencer 画面は同じモデルを映す

## スクリプト終了時

- Sequencer は鳴らしているノートを note off し、停止状態になる。続いて `picoruby_esp32_midi_cleanup()` が
  全ルートを消し、全トランスポートへ All Notes Off を送る
- ソングは残る。`UI::Sequencer.new` の一時的なソングなら、UI モードに戻ったところで `_poll` が
  自動保存のソングを読み直す（C 側は「一時的」の印を立てておくだけで、ファイルは触らない）
- `clock:` / `input:` / `device:` と `on_*` のハンドラはスクリプト限り

## 実装の分担

| 場所 | 追加するもの |
|---|---|
| `main/ui/seq_engine.{h,c}` | **ESP-IDF に依存しない**部分: ノート配列の操作と不変条件、tick → note on / off、パターンの切り替え・チェーン・SPP の換算、録音の確定、和音の取り込み、`.mseq` の直列化・検証。ホストテスト（`host_test/`） |
| `main/ui/ui_sequencer.{h,cpp}` | モデル、seq タスク、補間タイマ、トランスポート、`ui_seq_*` C API、保存・読み込み・自動保存の要求 |
| `main/ui/screen_sequencer.{h,cpp}` | 描画・タッチ・設定シート・スロットのメニュー・チェーンのシート・File シート |
| `main/ui/ui_common.h` | `UI_SCREEN_SEQUENCER`、`UI_WIDGET_SEQUENCER`、C API 宣言、ジオメトリ定数 |
| `main/ui/ui_manager.cpp` / `screen_layout.cpp` | `MENU_ORDER`、レイアウト部品の描画・タッチ |
| `main/ui/ui_midi_devices.cpp` | 内蔵デバイス `seq` の登録、既定パッチ Tempo → seq（Clock） |
| `main/platform/platform_freenove.c` | no-op スタブ（`transport_id` 0） |
| `components/midori-runtime/mrblib/main_task_base.rb` | `setup_sequencer`（起動時に自動保存を読む） |
| picoruby-ui `src/` / `mrblib/ui_sequencer.rb` / `ui_layout.rb` | バインディング、`UI::Sequencer` / `Pattern` / `Line`、`_poll`（ファイル I/O・自動保存・一時ソングの後始末）、`sequencer` 部品、`SCREEN_*` |
| picoruby-ui `test/sequencer_test.rb` | Ruby 側（引数の検証、`pattern` 文字列の解釈、ルートの組み方、保存の手順、自動保存の契機）のホストテスト |

## 段階

1. **エンジン + grid + Ruby API（画面なし）**: `seq_engine` とホストテスト（Start / Stop / Continue / SPP、
   補間 tick、ループ、gate、off-before-on、長さの違うライン、和音、同じ音程の切り詰め）。
   Ruby から `l.pattern` / `l[n] = [...]` で組んで鳴らせるところまで
2. **パターンとチェーン**: スロット、切り替え予約（`:pattern` / `:bar`）、チェーン、SPP のチェーン内換算、コピー・クリア
3. **保存と自動保存**: `.mseq` の直列化・検証（ホストテストで往復・壊れたファイル・知らないチャンク・上限超え）、
   Ruby の save / load、`_autosave`、起動時の復元、一時ソングの後始末
4. **画面 + レイアウト**: grid の表示・編集・1/2 ページ送り・Follow、スロット・チェーン、ラインの追加・削除、
   設定シート、File シート
5. **リアルタイム録音**: arm / overdub / replace / quantize、縮小ピアノロール
6. **仕上げ**: MIDI Devices への登録と既定パッチ、`on_step` / `on_pattern` / `on_edit`

## 決定事項（2026-10-25）

- パターン（スロット + チェーン）は実装する
- Standard MIDI File の読み書きは現時点では考えない
- 拍子は 4/4 固定
- 和音は 8 音まで、ノート配列は 512 ノートまで
- 自動保存あり
- スウィング・確率・ステップごとのベロシティ編集・ステップ入力は v1 では入れない
- Tempo を Start せずに Sequencer だけを走らせる経路は考えない
- ソング全体の上限 8192 ノート（64KB）、パターン 16、チェーン 64 エントリで実装する。足りなければ
  PSRAM 側を広げて対応する（上限は `SEQ_MAX_*` の定数にまとめ、`.mseq` には上限を書かないので、上げても既存のファイルは読める）
- 自動保存の間隔は 5 秒の静止 / 最長 60 秒
- ミュートは**ラインごと**（ソング全体で共通。パターンを切り替えても保つ）。パターンごとに変わるライン設定は持たない

## 未決事項

なし（2026-10-25 時点）。
