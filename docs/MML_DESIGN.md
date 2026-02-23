# MML (Music Macro Language) Interpreter 設計ドキュメント

## 概要

`bpm_loop`内で使用可能なMMLインタープリタを実装し、MML文字列からMIDI note_on/note_off シーケンスを生成する。

## 要件

1. MML文字列を解析し、ノートイベントのシーケンスに変換
2. `bpm_loop`のタイミングモデルと統合
3. `sync: true`時のBPM変更に追従（シーケンスの発生タイミングを動的に調整）
4. 複数チャンネル/複数シーケンスのサポート

## MML構文仕様

### 基本ノート
```
c d e f g a b   # ド レ ミ ファ ソ ラ シ
r               # 休符
```

### 変化記号
```
c+  c#          # シャープ（半音上げ）
c-  cb          # フラット（半音下げ）
```

### オクターブ
```
o4              # オクターブ4に設定（デフォルト）
>               # オクターブ上げ
<               # オクターブ下げ
```

### 音長（デフォルト音長 l4 = 四分音符）
```
c4              # 四分音符
c8              # 八分音符
c16             # 十六分音符
c2              # 二分音符
c1              # 全音符
c4.             # 付点四分音符（1.5倍）
c4..            # 複付点（1.75倍）
```

### デフォルト音長
```
l8              # デフォルト音長を八分音符に設定
```

### ベロシティ
```
v100            # ベロシティ100に設定（0-127）
```

### タイ・スラー
```
c4&c4           # タイ（同じ音を繋げる）
```

### ループ
```
[cdef]4         # cdefを4回繰り返し
[[cd]2 ef]2     # ネスト可能
```

## クラス設計

### MIDI::MML::Sequence

MML文字列をパースし、時間ベースのイベントリストを生成。

```ruby
module MIDI
  module MML
    class Sequence
      # @param mml [String] MML文字列
      # @param channel [Integer] MIDIチャンネル (0-15)
      # @param velocity [Integer] デフォルトベロシティ (0-127)
      def initialize(mml, channel: 0, velocity: 100)
        @mml = mml
        @channel = channel
        @default_velocity = velocity
        @events = []      # パース済みイベント
        @position = 0     # 現在位置（クロック単位）
        @total_length = 0 # 総長さ（クロック単位）
        parse
      end

      # シーケンスの総長さ（クロック単位、24 PPQ基準）
      attr_reader :total_length

      # 指定クロック位置のイベントを取得
      # @param clock [Integer] クロック位置（0から開始）
      # @return [Array<Hash>] イベントの配列
      def events_at(clock)
        @events.select { |e| e[:clock] == clock }
      end

      # シーケンスをリセット
      def reset
        @position = 0
      end

      # ループ再生用：位置を先頭に戻す
      def rewind
        @position = 0
      end
    end
  end
end
```

### MIDI::MML::Player

`bpm_loop`内でシーケンスを再生するプレイヤー。

```ruby
module MIDI
  module MML
    class Player
      # @param device [MIDI::Device] MIDI出力デバイス
      # @param sequence [Sequence] 再生するシーケンス
      # @param loop [Boolean] ループ再生するか
      def initialize(device, sequence, loop: true)
        @device = device
        @sequence = sequence
        @loop = loop
        @clock = 0
        @playing = true
      end

      # 現在のクロックでイベントを処理
      # bpm_loopのsubdivisionsと連携
      # @param clock_count [Integer] bpm_loopからのクロックカウント
      def tick(clock_count)
        return unless @playing

        # シーケンス内の相対クロック位置
        seq_clock = clock_count % @sequence.total_length

        # ループ時にリセット処理
        if @loop && clock_count > 0 && seq_clock == 0
          # 前回のノートオフを確実に送信
        end

        events = @sequence.events_at(seq_clock)
        events.each do |event|
          case event[:type]
          when :note_on
            @device.note_on(@sequence.channel, event[:note], event[:velocity])
          when :note_off
            @device.note_off(@sequence.channel, event[:note])
          end
        end
      end

      # 再生停止
      def stop
        @playing = false
        # 全ノートオフ送信
      end
    end
  end
end
```

## イベントデータ構造

```ruby
# パース後のイベント形式
{
  type: :note_on,     # :note_on, :note_off
  clock: 0,           # クロック位置（24 PPQ基準）
  note: 60,           # MIDIノート番号
  velocity: 100,      # ベロシティ（note_onのみ）
  channel: 0          # MIDIチャンネル
}
```

## クロック計算

- 24 PPQ（Pulses Per Quarter Note）基準
- 四分音符 = 24クロック
- 八分音符 = 12クロック
- 十六分音符 = 6クロック
- 全音符 = 96クロック

```ruby
def note_length_to_clocks(length, dotted = 0)
  base = 96 / length  # 96 = 全音符のクロック数
  total = base
  dot_value = base / 2
  dotted.times do
    total += dot_value
    dot_value /= 2
  end
  total
end
```

## bpm_loop統合

`bpm_loop`の`yield`にクロックカウントを渡すよう拡張する。

### bpm_loop実装変更

```ruby
def bpm_loop(bpm = 120, output: nil, ...)
  # ...
  if block_given? && (clock_count % clocks_per_subdivision) == 0
    yield clock_count  # クロックカウントを渡す
  end
  # ...
end
```

### 使用例

```ruby
seq = MIDI::MML::Sequence.new("cdefgab>c", channel: 0)
player = MIDI::MML::Player.new(device, seq, loop: true)

MIDI.bpm_loop(120, output: device, sync: true, input: input) do |clock|
  player.tick(clock)
end
```

### 後方互換性

既存コードでブロック引数を受け取らない場合も動作する（引数は無視される）。

```ruby
# 既存コード - 引き続き動作
MIDI.bpm_loop(120, output: device) do
  device.note_on(0, 60, 100)
end
```

## BPM変更時の追従

`sync: true`時にBPMが変更された場合、シーケンスのタイミングは自動的に追従する。

理由：
- クロックカウントはBPMに関係なく一定間隔でインクリメント
- 実時間でのクロック間隔がBPMに応じて変化
- シーケンスはクロック位置ベースなので自動的にBPMに追従

```
BPM 120: 1クロック ≈ 20.83ms
BPM 140: 1クロック ≈ 17.86ms

シーケンス上の「24クロック目のノート」は、
どちらのBPMでも「1拍目」に発音される
```

## ファイル構成

```
mrbgems/picoruby-midi/
├── mrblib/
│   ├── midi_input.rb      # 既存
│   ├── midi_device.rb     # 既存
│   ├── midi_mml.rb        # NEW: MMLパーサー
│   └── midi_mml_player.rb # NEW: MMLプレイヤー
```

## 使用例

### 基本的な使用

```ruby
device = MIDI::Device.new(USB_MIDI)

# シーケンス作成
melody = MIDI::MML::Sequence.new("l8 cdef gabc", channel: 0)
bass = MIDI::MML::Sequence.new("l4 c2 g2", channel: 1)

# プレイヤー作成
melody_player = MIDI::MML::Player.new(device, melody, loop: true)
bass_player = MIDI::MML::Player.new(device, bass, loop: true)

# 再生
MIDI.bpm_loop(120, output: device) do |clock|
  melody_player.tick(clock)
  bass_player.tick(clock)
end
```

### 外部クロック同期

```ruby
input = MIDI::Input.new(device)

MIDI.bpm_loop(120, output: device, sync: true, input: input) do |clock|
  melody_player.tick(clock)
  # BPMが変わってもクロックベースなので自動追従
end
```

## 実装優先順位

1. **Phase 1**: 基本MMLパーサー（ノート、休符、音長、オクターブ）
2. **Phase 2**: bpm_loopの拡張（クロックカウントをyield）
3. **Phase 3**: MMLプレイヤー実装
4. **Phase 4**: 拡張機能（ループ記法、タイ、ベロシティ）

## 検討事項

### Q1: 複数シーケンスの同期

複数のMMLシーケンスを同時再生する場合、長さが異なるとずれる可能性がある。

**対策案**:
- 最長のシーケンスに合わせて他をループ
- 明示的にシーケンス長を揃える責任はユーザーに

### Q2: ノートオフの保証

シーケンスのループ境界やBPM変更時にノートが鳴りっぱなしになる可能性。

**対策案**:
- アクティブノートを追跡し、ループ時に全ノートオフ
- Playerに`all_notes_off`メソッドを用意

### Q3: メモリ使用量

長いMMLをパースすると多くのイベントが生成される。

**対策案**:
- イベント数の上限を設ける
- 遅延パース（必要な部分だけパース）

## 次のステップ

1. この設計ドキュメントのレビュー
2. フィードバックを反映
3. Phase 1から実装開始
