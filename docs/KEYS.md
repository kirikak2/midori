# Keys 画面（画面の鍵盤）

1〜4 オクターブ（13〜49 鍵）の鍵盤を画面に出して指で弾く。最大 5 本の同時タッチ。
鍵盤そのものが **picoruby-midi のトランスポート**でもあり、`MIDI.route` の送信元にも
宛先にもなる。宛先として受けたノートは鍵を光らせるので、光る MIDI 鍵盤のように
「いま何が押されているか」を画面に映せる。

**`UI::Keys.new` のたびに別の鍵盤になる**（2026-11-18。それまではモデルが 1 つで、new は
それをリセットして使い回していた）。鍵盤ごとに設定・色・光と**トランスポートのビット**を持つので、
MIDI Devices 画面にもそれぞれ別の機器として並び、入力（光らせる元）も出力（弾いた音の行き先）も
鍵盤ごとにパッチできる。ファームウェアの鍵盤は `UI::Keys.default`（MIDI Devices の内蔵機器「Keys」）。

- 対応ボード: M5Stack CoreS3 / Tab5、CrowPanel（画面が必要）。Freenove は no-op スタブ
  （[main/platform/platform_freenove.c](../main/platform/platform_freenove.c)）。
  スタブではトランスポート登録もしないので `transport_id` は 0、`MIDI.route` は拒否する
- 画面: [main/ui/screen_keys.cpp](../main/ui/screen_keys.cpp)（ジオメトリ・描画・タッチ）
- モデル / トランスポート: [main/ui/ui_keys.cpp](../main/ui/ui_keys.cpp)（`ui_keys_*`）
- Ruby API: `UI::Keys`（[mrbgems/picoruby-ui/mrblib/ui.rb](../mrbgems/picoruby-ui/mrblib/ui.rb)）
- レイアウト: `keys`（[mrblib/ui_layout.rb](../mrbgems/picoruby-ui/mrblib/ui_layout.rb)）
- サンプル: [examples/keys.rb](../examples/keys.rb) / [examples/plugins/keys.rb](../examples/plugins/keys.rb)（`/sd/plugins` 用） /
  [examples/keys_amy.rb](../examples/keys_amy.rb)（鍵盤 + レイアウト + Knobs で AMY を弾く）

## 実装状況（2026-10-18）

2026-11-18: 鍵盤ごとのインスタンス（複数の鍵盤・鍵盤ごとの MIDI Devices の機器）。Tab5 のビルド・
Web シミュレーター・ホストテスト（`ruby test/keys_test.rb`、picoruby-ui ディレクトリで）まで。**実機は未確認**。

Tab5 のビルドは通る（Freenove のスタブはホストで構文チェックのみ、CoreS3 / CrowPanel /
Freenove のフルビルドは未実施）。**実機では未確認**（操作感・描画速度・鍵の大きさ）。

## 位置づけ

| | XYPad | Tombola | Keys |
|---|---|---|---|
| 操作 | 平面をタッチ＆ドラッグ | 多角形をドラッグ回転 | 鍵を押す（スライドで隣の鍵へ） |
| MIDI を送るのは | Ruby（内蔵ハンドラ） | C++ | **C++**（UI タスク） |
| トランスポート | — | — | **送信元にも宛先にもなる** |
| 個数 | 1（シングルトン） | 1 | **最大 8**（`UI::Keys::MAX`、既定の鍵盤を含む） |

発音を C++ 側にしたのは Tombola と同じ理由: 鍵盤はノートのタイミングがそのまま
演奏になるので、`UI.process` のポーリング間隔に左右されたくない。加えて
`MIDI.route` の送信元になるには、転送が Ruby を通らず C で完結している必要がある
（入力タスクが USB Host / UART を転送するのと同じ扱い）。

## Ruby API

```ruby
keys = UI::Keys.new(
  octaves: 2,          # 1..4（13..49 鍵）
  low: 48,             # 最低音。黒鍵を指定すると下の白鍵へ寄せる。上端が 127 を超えないよう寄せる
  channel: 0,          # 弾いたノートのチャンネル
  velocity: 100,       # 1..127、または :position（鍵の奥ほど弱く、手前ほど強く 30..127）
  slide: true,         # 指を滑らせると次の鍵を弾き直す（false: 最初の鍵のまま）
  labels: true,        # C の鍵に音名
  device: dev,         # 送信先（MIDI::Device / トランスポート / その配列）。MIDI.route とは別
  colors: { 60 => :red },
  white: :white, black: :black,   # 鍵の地の色
  press: :cyan,        # 指で押している鍵
  light: :orange)      # 外から来たノートで光っている鍵

keys.show              # Keys 画面へ（UI.keys / UI.show(:keys) と同じ）
```

### 鍵盤ごとのインスタンス（2026-11-18）

```ruby
kb0  = UI::Keys.default                         # ファームウェアの鍵盤（id 0、MIDI Devices の「Keys」）
lead = UI::Keys.new(low: 60, name: "Lead")      # 新しい鍵盤（id 1..、MIDI Devices の「Lead」）
bass = UI::Keys.new(low: 36, channel: 1)        # name: なしは「Keys 3」など（id + 1）
MIDI.route(lead, MIDIDevices.amy)               # 鍵盤ごとに別のルート
MIDI.route(bass, MIDIDevices.sam2695)
lead.show                                       # Keys 画面にこの鍵盤を出す
bass.close                                      # 閉じる（MIDI Devices からも消える）
UI::Keys.all                                    # => 開いている鍵盤（既定が先頭）
```

| | `UI::Keys.default` | `UI::Keys.new` |
|---|---|---|
| id | 0 | 1..7（空いている一番小さいもの） |
| MIDI Devices | 内蔵機器「Keys」（`main_task.rb` が起動のたびに `register_builtin(:keys)`）。画面のパッチは保存される | スクリプトの機器（`name:`、既定は「Keys N」）。パッチはスクリプト限り |
| スクリプトの終わり | 既定値に戻る（閉じない） | 閉じる（ビットを返し、MIDI Devices から消える） |
| `close` | リセットだけ | 閉じる。以後の操作は例外（スロットはもう別の鍵盤かもしれない） |
| Keys 画面 | 既定で映る | `show` で映す。閉じると既定の鍵盤に戻る |

- `UI::Keys.default(**kw)` はキーワードがあれば `configure` で適用する（`UI::Keys.new` と同じキーワード）。
  同じ VM の中では毎回同じインスタンス
- 鍵盤ごとにトランスポート登録表のビットを 1 つ使う。表（16 ビット）が埋まっていると、その鍵盤は
  `transport_id` が 0 になり、`device:` へは送れるが `MIDI.route` と MIDI Devices には出ない
- 開ける数は `UI::Keys::MAX`（8、既定を含む）。超えると `UI::Keys.new` が例外
- `on_note` は鍵盤ごと（イベントに鍵盤の id が載る）

### トランスポートとして

```ruby
MIDI.route(keys, MIDIDevices.amy)               # 弾いた音を AMY へ（C で転送）
MIDI.route(MIDIDevices.usb_midi_host, keys)     # 外部鍵盤で弾いた音で鍵を光らせる
MIDI.route(MIDIDevices.usb_midi_device, keys)   # PC（DAW）から送られてきた音で鍵を光らせる
MIDI::Device.new(keys).note_on(60)              # 60 を光らせる（trigger なら自動で消える）
```

`transport_id` / `send_packet` / `connected?` / `bytes_available`(0) / `read_available`(nil) /
`device_info` を持つ、他の gem と同じ形のトランスポート。

- **送信元**: 指で押した鍵の note on / off を、`device:` の送信先と、鍵盤のビットから
  張られた全ルートの宛先へ、1 回の `MIDI_transport_send()` で送る（重複は 1 回にまとめる）。
  ルートのチャンネル指定（`channel:`）も効く
- **宛先**: note on で光り、note off（velocity 0 の note on を含む）で消える。チャンネルごとに
  別々に持つので、別チャンネルの同じノートが消えても光は残る。All Sound Off (CC120) /
  All Notes Off (CC123) でそのチャンネルの光が、System Reset (0xFF) で全部が消える
- 宛先として受けたノートは**転送しない**（鍵盤は受けたものを他へ流す中継ではない）。
  外部鍵盤の音も鳴らしたいなら `MIDI.route(host, synth)` と `MIDI.route(host, keys)` を両方張る
- スクリプト終了時は `picoruby_esp32_midi_cleanup()` がルートを消し、全トランスポートへ
  All Notes Off を送る（鍵盤の光も消える）。続く `picoruby_ui_cleanup()` で鍵盤は既定値に戻る

### 色

表示の優先順位は **押している（press） > 光っている（light / チャンネル色） > 鍵ごとの色 > 地の色**。

```ruby
keys.set_color(60, :red)              # 1 鍵
keys.set_color([60, 64, 67], :blue)   # 配列 / Range
keys[62] = [255, 128, 0]              # [r, g, b]（0..255）
keys[62] = nil                        # 地の色に戻す
keys.color(60)                        # => RGB565 / nil
keys.clear_colors
keys.channel_color(9, :red)           # ch 10 で光るときの色（nil で light に戻す）
keys.white_color = UI.rgb(240, 240, 220)
keys.press_color = :green
keys.light_color = :magenta
```

色は Symbol（`:red` など `UI.pad` と同じ + `:black`）、RGB565 の Integer、`[r, g, b]`。
`UI.rgb(r, g, b)` は RGB565 を返す。

鍵ごとの色は「スケールや和音の構成音を常時見せる」用途、光は「いま鳴っている音」。

### その他

```ruby
keys.octaves = 3; keys.low = 36       # 音域は動的に変えられる
keys.octave_up / keys.octave_down     # ±12（範囲に収まるよう寄せる）
keys.range                            # => 48..72
keys.channel = 1; keys.velocity = :position; keys.slide = false
keys.device = [synth, MIDIDevices.usb_midi_device]
keys.lit?(60) / keys.pressed?(60) / keys.pressed_notes
keys.clear_lights

keys.on_note do |e|                   # { note:, velocity:, channel:, state: }
  UI.log("#{e[:note]} #{e[:state]}")  # 発音には不要。ブロックを渡すまでキューに載らない
end
UI.on(:key) { |e| ... }               # 同じイベント（type: :key）
```

## レイアウト

```ruby
UI.layout("Play", cols: 4, rows: [1, 2]) do
  knob col: 0, label: "Cutoff" do |v| dev.control_change(74, v.to_i) end
  keys col: 0..3, row: 1, octaves: 3, device: dev   # UI::Keys.new と同じキーワード
  # keys col: ..., keys: kb                           # 既存の UI::Keys を渡す
end
```

**1 つのレイアウトにいくつでも置ける**（それぞれ別の鍵盤。2026-11-18 まで 1 つだった）。
`keys:` で渡した鍵盤はそのまま使い、キーワードだけなら `UI::Keys.new(**キーワード)` を作る。
レイアウトが作った鍵盤は、そのレイアウトを同じ title で定義し直したとき・`delete` したときに閉じる
（`keys:` で渡したものは閉じない）。同じ鍵盤を 1 つのレイアウトに 2 回置くと `ArgumentError`
（鍵盤の再描画の情報は 1 つの描画先にしか渡らないため）。
最小サイズの目安は 120x48px（警告のみ）。

```ruby
UI.layout("Split", rows: 2) do
  keys row: 0, keys: lead
  keys row: 1, keys: bass
end.show
```

## 構成

```
 [PicoRuby task]                  [UI task]                          [MIDI input task 等]
  UI::Keys.new / set_color ...     ScreenKeys::onTouch/Move           MIDI.route(host, keys)
    └ UI._keys_* → ui_keys_*         └ hitTest → note, velocity         └ MIDI_transport_send
                                     └ ui_keys::touch_*                    └ keys の send_packet
                                         └ key_on / key_off                    = ui_keys_receive
                                             mask = out_mask                      └ lit[note] |= ch
                                                  | MIDI_route_targets(bit)
                                             MIDI_transport_send(mask)
                                             ui_event_push(UI_EVENT_KEY)  ← notify のときだけ
                                     ScreenKeys::update
                                       └ take_dirty → 変わった鍵だけ ui_strip::paint
```

- 鍵盤は**スロット表**（`UI_KEYS_MAX` = 8）。スロット 0 が既定の鍵盤（`ui_keys_default()`）、
  1.. がスクリプトの鍵盤（`ui_keys_create()` / `ui_keys_destroy()`）。C API はすべて先頭に鍵盤の id を取る
- モデル（設定・鍵ごとの色・光・押下数、約 1.5KB）は**スロットを初めて使うときに calloc** し、閉じても解放しない
  （.bss に置かない: docs/MEMORY_ALLOCATION.md。UI タスクが閉じた鍵盤をまだ描いている・指を離している
  ことがあるため、閉じたモデルも読める状態で残し、次に開くときに初期化する）
- 開くとトランスポート登録表へ `MIDI_transport_register()` で登録し（ctx が id）、`MIDI_route_set_source()`
  で送信元として宣言する。閉じると取り消して `MIDI_transport_unregister()`
- MIDI Devices への登録は Ruby（既定の鍵盤は `main_task.rb` の `setup_midi_devices`、他は `UI::Keys.new`）。
  以前は C が「keys」を自分で登録していた
- 描画とタッチは `KeysView`（[screen_keys.h](../main/ui/screen_keys.h)）。Keys 画面が 1 つ、レイアウトは
  keys の部品ごとに 1 つ持つ（部品は `UI_WIDGET_KEYS` で鍵盤の id を持つ。`ui_layout_add_keys()`）。
  再描画の情報（dirty）は鍵盤ごとにあり、それを取った 1 つのビューが描く
- Keys 画面が映すのは `ui_keys_shown()`（`show` で選んだ鍵盤、閉じていれば既定の鍵盤）
- 押下は**ノートごとの指の本数**で数える。2 本で同じ鍵を押しても note on は 1 回、
  最後の指が離れたときに note off。note off は **note on を送った時点の送信先・チャンネル**へ送る
  （押している間に `device=` や `channel=` が変わっても音が残らない）
- `ui_keys_reset()` / `ui_keys_destroy()` / `ui_keys_cleanup()` は鍵盤の世代番号を進める。ビューの
  `update()` がそれを見て押したままの鍵を離す（タッチの状態を持つのは UI タスクだけ）。閉じた鍵盤の
  note off も、note on を送ったときのビットと送信先へ送る
- スクリプト終了時の `ui_keys_cleanup()` は、スクリプトの鍵盤を閉じ、既定の鍵盤を既定値に戻す
- 画面を離れる（`leave()`）と押している鍵はすべて離す
- 描画は変わった鍵の矩形だけ。白鍵の矩形を塗り直すと、上に重なる黒鍵も一緒に描き直す
  （矩形でクリップして鍵盤全体を描く）。隣り合う鍵の矩形はまとめて 1 回で塗る
- 黒鍵は左右の白鍵の境界に中央をそろえる（実物のような左右へのずらしはしない）。
  幅は白鍵の 58%、長さは鍵盤の 62%

### picoruby-midi 側の追加（2026-10-18）

`MIDI.route` の送信元は従来、入力タスクが読む USB Host（0x01）と UART（0x02）だけだった。
自分で MIDI を生み出すトランスポートも送信元になれるよう、
[midi_route.h](../mrbgems/picoruby-midi/include/midi_route.h) に追加した:

| 関数 | 内容 |
|---|---|
| `MIDI_route_set_source(bit, on)` | `bit` を送信元として宣言 / 取り消し |
| `MIDI_route_sources()` | 送信元になれるビット（0x07 + 宣言済み）。Ruby の `MIDI._route_sources` |
| `MIDI_route_targets(src, cin, b1)` | そのパケットの転送先マスク（`MIDI_route_packet` の中身を切り出したもの） |

`MIDI.route` は送信元を `MIDI._route_sources` で検査し、入力タスク由来でない送信元では
`_route_start`（入力タスクの起動）を呼ばない。

**USB-MIDI Device（0x04）も入力タスクが読む送信元にした**（2026-10-18）。PC から来たノートで
鍵盤を光らせるため（`MIDI.route(MIDIDevices.usb_midi_device, keys)`）。入力タスクがデバイスの
受信リングバッファを読むのは**デバイスから張られたルートがある間だけ**（`MIDI_route_from()`）で、
その間は PC からのパケットを全部取るので `USB_MIDI_DEVICE#read_available` と併用しないこと。
ルートが無いときは従来どおり Ruby の `read_available` 用にバッファへ残す。列挙（`tud_ready()`）
は見ないので、PC がポートを開く前に張ったルートもそのまま効く。

## タッチ → ノート

- 黒鍵の長さより上なら黒鍵を先に判定し、外れたら白鍵（`whiteX()` で丸めた境界で判定）
- `velocity: :position` は鍵の上端 30 → 下端 127 の線形（黒鍵は黒鍵の長さで）
- 鍵盤の外へ指が出ると note off（`slide: true` なら戻ってきた鍵で弾き直し）。
  `slide: false` は最初の鍵を指が離れるまで持つ
