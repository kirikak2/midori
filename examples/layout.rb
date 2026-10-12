# layout.rb
#
# レイアウト DSL（docs/UI_LAYOUT_DSL.md）で、XYPad・ノブ・パッドを 1 画面に並べる
# 演奏面を 2 枚作り、Home のタイル / MIDI 入力 / パッドで切り替える例。
#
#   Perform                         Mix
#   ┌──────────────────┬──────┐     ┌──────┬──────┬──────┬──────┐
#   │                  │Cutoff│     │ Vol  │ Pan  │Reverb│Chorus│
#   │      XYPad       ├──────┤     ├──────┴──────┴──────┴──────┤
#   │                  │ Reso │     │  [Perform]   [Hold XY]     │
#   ├────┬────┬────┬───┴──────┤     └───────────────────────────┘
#   │Kick│Snr │ HH │  [Mix]   │
#   └────┴────┴────┴──────────┘
#
# 列・行は 0 始まり。Range で何マス占めるかを書く。名前付き領域（areas）と
# 入れ子の grid も使っている。
#
# 画面切り替え（docs/UI_NAVIGATION.md）:
#   - Home のタイル（各レイアウトが 1 枚ずつ）、ナビバーの Home / Back
#   - パッドから layout.show
#   - USB-MIDI Host に繋いだコントローラのプログラムチェンジ 0 / 1

require 'midi'
require 'ui'

transport = MIDIDevices.usb_midi_device || MIDIDevices.sam2695
unless transport
  puts "No MIDI output available"
  return
end
dev = MIDI::Device.new(transport)
CH = 0

# 小さい画面ではパッドの行を太らせる（UI.display で分岐するのは素の Ruby）
large = UI.display[:large]

mix = nil   # Perform のパッドから参照するので先に宣言

perform = UI.layout("Perform", cols: [3, 1], rows: large ? [2, 2, 1] : [1, 1, 1]) do
  areas <<~GRID
    xy k1
    xy k2
    p  p
  GRID

  xypad area: :xy, scale: [48, 50, 52, 55, 57, 60, 62, 64],
        glide_range: 2, y_cc: 74, device: dev

  knob area: :k1, id: :cutoff, label: "Cutoff", color: :cyan, value: 64 do |v|
    dev.control_change(74, v.to_i, channel: CH)
  end
  knob area: :k2, label: "Reso", color: :magenta, value: 20 do |v|
    dev.control_change(71, v.to_i, channel: CH)
  end

  # 領域 :p をさらに 4 列に割る（中の col: は子グリッドの座標）
  grid area: :p, cols: 4 do
    [["Kick", 36, :red], ["Snare", 38, :orange], ["HH", 42, :yellow]].each_with_index do |(name, note, color), i|
      pad col: i, label: name, color: color do
        dev.trigger(note, 120, duration: 100, channel: 9)
      end
    end
    pad col: 3, label: "Mix", color: :blue do
      mix.show
    end
  end
end

xy = nil
mix = UI.layout("Mix", cols: 4, rows: 2) do |l|
  # ブロック引数あり: self はスクリプトのまま
  [["Vol", 7, :white, :min], ["Pan", 10, :orange, :center],
   ["Reverb", 91, :green, :min], ["Chorus", 93, :purple, :min]].each_with_index do |(name, cc, color, origin), i|
    l.knob col: i, label: name, color: color, origin: origin,
           value: origin == :center ? 64 : 100 do |v|
      dev.control_change(cc, v.to_i, channel: CH)
    end
  end
  l.pad col: 0..1, row: 1, label: "Perform", color: :blue do
    perform.show
  end
  l.pad col: 2..3, row: 1, label: "Hold XY", type: :toggle do |on|
    xy.hold = on if xy
  end
end

# Perform に置いた XYPad は UI::XYPad のインスタンスとして取り出せる
perform.widgets.each { |w| xy = w if w.is_a?(UI::XYPad) }

# 定義しただけでは何も送らないので、ノブの初期値を送っておく
perform.send_all
mix.send_all

# Home には Perform / Mix のタイルが出る。使わない画面は隠す
UI.menu(hide: [:tombola, :pads, :knobs])

# ---- MIDI 入力から画面を切り替える ----------------------------------------
if MIDIDevices.usb_midi_host
  ctl = MIDI::Input.new(MIDI::Device.new(MIDIDevices.usb_midi_host))
  pages = [perform, mix]
  ctl.on(:program_change) do |e|
    page = pages[e[:program]]
    UI.show(page) if page
  end
  # CC 102 / 103 で前後の画面、104 で Back
  ctl.on(:control_change) do |e|
    next if e[:value] == 0
    case e[:cc]
    when 102 then UI.prev_screen
    when 103 then UI.next_screen
    when 104 then UI.back
    end
  end
end

UI.on(:screen_change) do |e|
  name = e[:layout] ? e[:layout].title : e[:screen].to_s
  UI.log("screen: #{name} (#{e[:source]})")
end

perform.show

sm = ScriptManager.new
loop do
  break if sm.stop_requested?
  UI.process
  MIDI.sleep_ms 5   # Kernel#sleep_ms では MIDI::Input のハンドラが呼ばれない
end
