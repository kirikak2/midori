# MIDI to MML Converter Tools

このディレクトリには、MIDIファイルをMML (Music Macro Language) 形式に変換するツールが含まれています。

## ツール

### 1. Python版: `midi_to_mml_v2.py`

**必要なライブラリ:**
```bash
pip install mido
```

**使い方:**
```bash
python3 tools/midi_to_mml_v2.py
```

固定のパス（`bach_suite3-2_air.mid`）を変換します。

### 2. Ruby版: `midi_to_mml.rb`

**必要なgem:**
```bash
gem install midilib
```

**使い方:**
```bash
# 基本的な使い方
ruby tools/midi_to_mml.rb input.mid output.mml

# 出力ファイル名を省略（input.mmlが生成される）
ruby tools/midi_to_mml.rb input.mid

# 例
ruby tools/midi_to_mml.rb bach_suite3-2_air.mid bach_air.mml
```

**特徴:**
- コマンドライン引数で入力・出力ファイルを指定可能
- より柔軟な使い方が可能
- Rubyの標準的なスタイルに準拠

## MML形式について

生成されるMMLは、`docs/MML_DESIGN.md` の仕様に準拠しています：

### 基本構文

```mml
o4         # オクターブ4に設定
c4 d4 e4   # ド、レ、ミ（四分音符）
f+8        # ファ#（八分音符）
g2.        # ソ（付点二分音符）
r4         # 休符（四分音符）
```

### 音長

- `1` - 全音符 (4 beats)
- `2` - 二分音符 (2 beats)
- `4` - 四分音符 (1 beat)
- `8` - 八分音符 (0.5 beat)
- `16` - 十六分音符 (0.25 beat)
- `32` - 三十二分音符 (0.125 beat)

### オクターブ

- `o0` - オクターブ0（最低音）
- `o4` - オクターブ4（中央のC = C4）
- `o8` - オクターブ8（最高音）

### 変化記号

- `c+` または `c#` - シャープ（半音上げ）
- `c-` または `cb` - フラット（半音下げ）

## 変換の仕組み

1. **MIDIファイルの読み込み**
   - トラック、テンポ、ノートイベントを解析

2. **ノートイベントの収集**
   - Note On / Note Off イベントをペアにして音長を計算
   - ティック数を拍数に変換

3. **MML文字列の生成**
   - オクターブ変更を最適化
   - ノート名と音長を出力

4. **ファイル出力**
   - コメント付きMMLファイルを生成

## トラブルシューティング

### Ruby版

**エラー: `cannot load such file -- midilib (LoadError)`**
```bash
gem install midilib
```

**エラー: `Permission denied`**
```bash
chmod +x tools/midi_to_mml.rb
```

## 開発

### テスト

```bash
ruby tools/midi_to_mml.rb bach_suite3-2_air.mid test_output.mml
```

### 出力の確認

生成されたMMLファイルをテキストエディタで開いて確認するか、PicoRubyのMMLプレイヤーで再生します。

```ruby
# PicoRubyでの再生例
seq = MIDI::MML::Sequence.new("o5 c4 d4 e4 f4 g4", channel: 0)
player = MIDI::MML::Player.new(device, seq, loop: true)

MIDI.bpm_loop(120, output: device) do |clock|
  player.tick(clock)
end
```

## ライセンス

このツールは、Midoriプロジェクトの一部として提供されています。
