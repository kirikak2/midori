# lumi.rb -- LUMI Keys を光らせる filter（examples/midi_to_lumi.rb の filter 版。
# docs/MIDI_FILTER_REGISTRY.md）
#
# /sd/filters/ に lib/lumi_sysex.rb と一緒に置く（lib/ は自動では読まれない）。
# MIDI Devices 画面で「鍵盤 > LUMI Keys」のパッチに LUMI を付けると、付いたときに
# LUMI をスケール色モードに設定し、鍵盤のノートでキーが光る（ノートはパッチが C から
# 直接届けるので、この filter は何も Ruby に回さない: types []）。
#
# パネル（Home のタイル）: Wave Up / Wave Dn（高速ウェーブ）、Ripple（Tempo の
# クロックに合わせた静かなウェーブ）、Bright（輝度）。スクリプト版の
# MIDI.sleep_ms(13) のループは every(13) に置き換えた。filter はほかのパッチと
# VM を共有するので、ブロックの中で sleep してはいけない。

load "/sd/filters/lib/lumi_sysex.rb"

class LumiLights < MIDI::Filter
  label "LUMI"
  types []
  clock :internal

  WAVE_LOW  = 48   # C2
  WAVE_HIGH = 96   # C6
  RIPPLE_STEP = 3  # クロック 3 つ（24 ppq）で 1 音
  RIPPLE_DUR  = 450
  RIPPLE_VEL  = 45

  param :scale, :enum, choices: [:chromatic, :major, :minor, :pentatonic_major,
                                 :pentatonic_minor, :blues, :dorian, :whole_tone],
        default: :chromatic, label: "Scale"
  param :brightness, :int, range: 0..100, default: 80, step: 5, label: "Bright"

  def setup
    @ripple = false
    @wave = nil
    Lumi.set_color_mode(output, 1)            # スケール色モード
    Lumi.set_color(output, 0,   0, 150, 255)  # 青: スケール音
    Lumi.set_color(output, 1, 255,  80,   0)  # 橙: ルート音
    Lumi.set_scale(output, params[:scale])
    Lumi.set_brightness(output, params[:brightness])
  end

  def teardown
    ripple_off
  end

  def param_changed(name, value)
    Lumi.set_scale(output, value) if name == :scale
    Lumi.set_brightness(output, value) if name == :brightness
  end

  # 下から上 / 上から下へのウェーブ（13 ms ごとに 1 音、各 100 ms 点灯）
  def wave(from, to)
    @wave.cancel if @wave
    note = from
    step = from < to ? 1 : -1
    @wave = every(13) do
      output.trigger(note, 110, duration: 100)
      note += step
      if note == to + step
        @wave.cancel
        @wave = nil
      end
    end
  end

  def ripple=(on)
    @ripple = on
    ripple_off unless on
  end

  def ripple_off
    n = WAVE_LOW
    while n <= WAVE_HIGH
      output.note_off(n)
      n += 1
    end
  end

  # 音域を ping-pong で往復する
  def on_clock(tick)
    return unless @ripple && tick % RIPPLE_STEP == 0
    range = WAVE_HIGH - WAVE_LOW
    step = (tick / RIPPLE_STEP) % (range * 2)
    pos = step > range ? range * 2 - step : step
    output.trigger(WAVE_LOW + pos, RIPPLE_VEL, duration: RIPPLE_DUR)
  end

  def layout(ui)
    f = self
    lo = WAVE_LOW
    hi = WAVE_HIGH
    ui.layout(cols: 4, rows: 2) do
      pad(col: 0, row: 0, label: "Wave Up", color: :cyan) { f.wave(lo, hi) }
      pad(col: 1, row: 0, label: "Wave Dn", color: :orange) { f.wave(hi, lo) }
      pad(col: 2, row: 0, label: "Ripple", color: :green, type: :toggle) { |on| f.ripple = on }
      param_knob :brightness, col: 3, row: 0, color: :yellow
      param_pad :scale, col: 0..3, row: 1, color: :blue
    end
  end
end

MIDI.register_filter(:lumi, LumiLights.new)
