# arp.rb -- アルペジエータ（docs/MIDI_FILTER_REGISTRY.md）
#
# /sd/filters/ に置くと起動時に読み込まれる。押さえている音を MIDI::Clock
# （Tempo 画面のテンポ）に合わせて 1 音ずつ鳴らす。Chord の後ろにつなぐと、
# 1 音押すだけで和音の分散和音になる（MIDI Devices の Filters タブで Chord → Arp の順に選ぶ）。
#
# 入力のノートは鳴らさない（call が nil を返す）。鳴らすのは on_clock の emit で、
# emit はこの filter より後ろのチェーンを通って宛先に届く。鳴らした音はチェーンが
# 覚えているので、filter を外すと止まる。

class Arp < MIDI::Filter
  label "Arp"
  types :note_on, :note_off
  clock :internal

  RATES = [:"1/4", :"1/8", :"1/16", :"1/32"]
  TICKS = [24, 12, 6, 3]   # 24 ppq

  param :rate,    :enum, choices: RATES, default: :"1/16", label: "Rate"
  param :mode,    :enum, choices: [:up, :down, :updown, :random], default: :up, label: "Mode"
  param :octaves, :int,  range: 1..4, default: 1, label: "Oct"
  param :hold,    :bool, default: false, label: "Hold"

  def setup
    @held = []        # 押さえているノート（押した順）
    @step = 0
    @playing = nil    # 今鳴らしている note on
    @velocity = 100
    @channel = 0
  end

  def call(m)
    if m.type == :note_on
      # Hold 中に全部離してから弾き直したら、新しいフレーズにする
      @held = [] if params[:hold] && @released
      @released = false
      @held.delete(m.note)
      @held << m.note
      @velocity = m.velocity
      @channel = m.channel
    else
      @released = true if params[:hold]
      @held.delete(m.note) unless params[:hold]
    end
    nil
  end

  def param_changed(name, value)
    @held = [] if name == :hold && !value
  end

  def on_clock(tick)
    return unless tick % TICKS[RATES.index(params[:rate]) || 2] == 0
    stop_note
    seq = sequence
    return if seq.empty?
    @playing = MIDI::Message.new(:note_on, @channel, seq[@step % seq.size], @velocity)
    @step += 1
    emit(@playing)
  end

  def on_stop
    stop_note
    @step = 0
  end

  def stop_note
    emit(@playing.with(type: :note_off, velocity: 0)) if @playing
    @playing = nil
  end

  def sequence
    base = @held.sort
    notes = []
    params[:octaves].times { |o| base.each { |n| notes << n + 12 * o } }
    case params[:mode]
    when :down   then notes.reverse
    when :updown then notes.size > 2 ? notes + notes[1..-2].reverse : notes
    when :random then notes.shuffle
    else notes
    end
  end

  def layout(ui)
    ui.layout(cols: 4, rows: 1) do
      param_pad :rate, col: 0, color: :cyan
      param_pad :mode, col: 1, color: :green
      param_knob :octaves, col: 2, color: :magenta
      param_pad :hold, col: 3, color: :orange
    end
  end
end

MIDI.register_filter(:arp, Arp.new)
