# chord.rb -- コードハーモナイザ（docs/MIDI_FILTER_REGISTRY.md）
#
# /sd/filters/ に置くと起動時に読み込まれる。MIDI Devices 画面でパッチを長押し →
# Filters タブで「Chord」を選ぶと、弾いた音に和音が付く。パラメータ（コードの種類・
# オクターブ）は選んだ Chord をもう一度長押しで変えられるほか、Home に出る
# パネルでも変えられる（コードごとのパッドで選び、選んでいるコードだけ色が付く）。
#
# NoteMap で note off を和音全体に広げるので、押さえている間にコードを変えても、
# 離したときは押したときの和音が止まる。

class Chord < MIDI::Filter
  label "Chord"
  types :note_on, :note_off

  CHORDS = {
    major:   [0, 4, 7],
    minor:   [0, 3, 7],
    sus4:    [0, 5, 7],
    seventh: [0, 4, 7, 10],
    min7:    [0, 3, 7, 10],
    power:   [0, 7, 12],
  }

  param :chord,  :enum, choices: CHORDS.keys, default: :major, label: "Chord"
  param :octave, :int,  range: -2..2, default: 0, label: "Oct"

  def setup
    @map = MIDI::NoteMap.new
  end

  def call(m)
    @map.apply(m) do |x|
      base = x.note + 12 * params[:octave]
      CHORDS[params[:chord]].map { |i| x.with(note: base + i) }
    end
  end

  # パネル: コードごとのパッド（選んでいるコードだけ色が付く）とオクターブのノブ
  SELECTED = :cyan
  UNSELECTED = :gray

  def layout(ui)
    f = self
    names = CHORDS.keys
    current = params[:chord]
    cols = (names.size + 1) / 2   # 2 段に並べ、右端の列をノブに
    @panel = ui.layout(cols: cols + 1, rows: 2) do
      names.each_with_index do |name, i|
        pad(col: i % cols, row: i / cols, id: name, label: name.to_s,
            color: name == current ? SELECTED : UNSELECTED) do
          f.set_param(:chord, name)
        end
      end
      param_knob :octave, col: cols, row: 0..1, color: :magenta
    end
  end

  # 画面（MIDI Devices のパラメータ）やパッドでコードが変わったら色を塗り直す
  def param_changed(name, value)
    return unless name == :chord && @panel
    CHORDS.each_key do |k|
      pad = @panel[k]
      pad.color = (k == value ? SELECTED : UNSELECTED) if pad
    end
  end
end

MIDI.register_filter(:chord, Chord.new)
# 既定値違いのプリセット。同じクラスを 2 つ登録するときは label: で画面の名前を分ける
MIDI.register_filter(:minor7, Chord.new(chord: :min7, octave: -1), label: "Min7")
