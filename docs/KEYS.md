# Keys 画面（画面の鍵盤）

1〜4 オクターブ（13〜49 鍵）の鍵盤を画面に出して指で弾く。最大 5 本の同時タッチ。
鍵盤そのものが **picoruby-midi のトランスポート**でもあり、`MIDI.route` の送信元にも
宛先にもなる。宛先として受けたノートは鍵を光らせるので、光る MIDI 鍵盤のように
「いま何が押されているか」を画面に映せる。

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

Tab5 のビルドは通る（Freenove のスタブはホストで構文チェックのみ、CoreS3 / CrowPanel /
Freenove のフルビルドは未実施）。**実機では未確認**（操作感・描画速度・鍵の大きさ）。

## 位置づけ

| | XYPad | Tombola | Keys |
|---|---|---|---|
| 操作 | 平面をタッチ＆ドラッグ | 多角形をドラッグ回転 | 鍵を押す（スライドで隣の鍵へ） |
| MIDI を送るのは | Ruby（内蔵ハンドラ） | C++ | **C++**（UI タスク） |
| トランスポート | — | — | **送信元にも宛先にもなる** |
| 個数 | 1（シングルトン） | 1 | 1（シングルトン） |

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

`UI::Keys.new` は既定値へリセットしてから引数を適用する（前のインスタンスの色・光・
送信先は消え、押したままの鍵は**押したときの送信先へ** note off が送られる）。

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

XYPad / Tombola と同じく、モデルが 1 つなので 1 レイアウトに 1 つまで（`SINGLETONS`）。
最小サイズの目安は 120x48px（警告のみ）。レイアウトの Keys と Keys 画面は同じ鍵盤を映す。

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

- モデル（設定・鍵ごとの色・光・押下数）は**初めて使われたときに calloc** する（約 1KB。
  .bss に置かない: docs/MEMORY_ALLOCATION.md）。同時にトランスポート登録表へ
  `MIDI_transport_register()` で登録し、`MIDI_route_set_source()` で送信元として宣言する。
  Home のタイル状態（"C3-C5"）を出すときにも作られるので、実際には起動直後に登録される
- 押下は**ノートごとの指の本数**で数える。2 本で同じ鍵を押しても note on は 1 回、
  最後の指が離れたときに note off。note off は **note on を送った時点の送信先・チャンネル**へ送る
  （押している間に `device=` や `channel=` が変わっても音が残らない）
- `ui_keys_reset()` / `ui_keys_cleanup()` は世代番号を進める。画面の `update()` がそれを見て
  押したままの鍵を離す（タッチの状態を持つのは UI タスクだけ）
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
