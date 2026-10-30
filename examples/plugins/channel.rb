# channel.rb -- チャンネル変換（docs/MIDI_FILTER_REGISTRY.md）
#
# /sd/plugins/ に置くと起動時に読み込まれる（プラグイン）。MIDI Devices 画面でパッチを長押し →
# Filters タブで「Ch Map」を選ぶと、受けたチャンネルメッセージのチャンネルを
# 書き換えて送る（例: ch5 → ch1）。
#
# 規則は 3 本（In → Out）。In が "-" の規則は使わない。Out が "-" の規則は、
# In のチャンネルを捨てる。同じ In に規則を 2 本書くと両方のチャンネルへ送る
# （レイヤー）。どの規則にも当たらないチャンネルは Other で決める
# （thru = そのまま / drop = 捨てる。drop にすると規則に書いたチャンネルだけを通す）。
#
# 設定は選んだ Ch Map をもう一度長押しして出るパラメータ画面のほか、Home に出る
# パネル（規則ごとに In / Out の - / + と、Other の切り替え、Clear）でも変えられる。
#
# NoteMap で note off を note on と同じチャンネルへ送るので、押さえている間に
# 規則を変えても、離したときは押したときのチャンネルの音が止まる。

class ChannelMap < MIDI::Filter
  label "Ch Map"
  # types は宣言しない（全チャンネルメッセージ。クロックなどは C から直接）

  RULES = 3
  FROM = [:from1, :from2, :from3]
  TO   = [:to1, :to2, :to3]

  RULES.times do |i|
    param FROM[i], :channel, default: nil, label: "In#{i + 1}"
    param TO[i],   :channel, default: nil, label: "Out#{i + 1}"
  end
  param :other, :enum, choices: [:thru, :drop], default: :thru, label: "Other"

  def setup
    @map = MIDI::NoteMap.new
  end

  def call(m)
    if m.type == :note_on || m.type == :note_off
      @map.apply(m) { |x| remap(x) }
    else
      remap(m)
    end
  end

  # m を規則どおりのチャンネルへ。Message / Array / nil（捨てる）
  def remap(m)
    outs = []
    hit = false
    RULES.times do |i|
      from = params[FROM[i]]
      next if from.nil? || from != m.channel
      hit = true
      to = params[TO[i]]
      outs << to unless to.nil? || outs.include?(to)
    end
    return(params[:other] == :thru ? m : nil) unless hit
    return nil if outs.empty?
    return m.with(channel: outs[0]) if outs.size == 1
    outs.map { |ch| m.with(channel: ch) }
  end

  # name（:channel のパラメータ）を d だけ動かす。"-"（-1）〜 16（15）で止める
  def step(name, d)
    spec = self.class.param_spec(name)
    code = self.class._code(spec, params[name]) + d
    code = -1 if code < -1
    code = 15 if code > 15
    set_param(name, code)
  end

  def clear
    RULES.times do |i|
      set_param(FROM[i], nil)
      set_param(TO[i], nil)
    end
  end

  # パネル: 規則 1 行に [-][In][+][-][Out][+]、最後の行に Other と Clear。
  # 値のパッドの id を param_<name> にしておくと、パラメータ画面で変えたときも
  # ラベルが追従する（UI::MIDIFilters._param_shown）
  def layout(ui)
    f = self
    @panel = ui.layout(cols: 6, rows: RULES + 1) do
      RULES.times do |i|
        [[FROM[i], 0], [TO[i], 3]].each do |name, col|
          pad(col: col, row: i, label: "-") { f.step(name, -1) }
          pad(col: col + 1, row: i, id: "param_#{name}".to_sym,
              label: f.value_label(name), color: f.value_color(name))
          pad(col: col + 2, row: i, label: "+") { f.step(name, 1) }
        end
      end
      param_pad :other, col: 0..2, row: RULES, color: :orange
      pad(col: 3..5, row: RULES, label: "Clear") { f.clear }
    end
  end

  # UI::MIDIFilters._param_label と同じ書式（"In1 5" / "Out1 -"）
  def value_label(name)
    spec = self.class.param_spec(name)
    v = params[name]
    "#{spec[:label]} #{v.nil? ? '-' : v + 1}"
  end

  # 使っている規則の In は水色、Out は緑（"-" = 捨てるなら赤）。使っていない規則は灰色
  def value_color(name)
    i = FROM.index(name) || TO.index(name)
    return :gray if params[FROM[i]].nil?
    return :cyan if name == FROM[i]
    params[name].nil? ? :red : :green
  end

  # 規則の In が変わると Out の色も変わるので、その行を塗り直す
  def param_changed(name, value)
    return unless @panel
    i = FROM.index(name) || TO.index(name)
    return unless i
    [FROM[i], TO[i]].each do |n|
      pad = @panel["param_#{n}".to_sym]
      pad.color = value_color(n) if pad
    end
  end
end

MIDI.register_filter(:chmap, ChannelMap.new)
