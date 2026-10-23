# keys_amy.rb
#
# 画面の鍵盤（UI::Keys、docs/KEYS.md）で内蔵スピーカーの AMY シンセ（AMY::Synth）を弾く。
# Tab5 / CrowPanel 用（AMY がある大画面ボード）。
#
#   レイアウト「AMY Keys」（Home のタイル）
#   ┌──────┬──────┬──────┬──────┬──────┬──────┐
#   │Patch │Cutoff│ Reso │FltEnv│Reverb│Volume│   よく触るノブ
#   ├─────┬┴────┬─┴───┬──┴──┬───┴─┬────┴──────┤
#   │Oct- │Oct+ │Scale│ Vel │Edit │            │   パッド
#   ├─────┴─────┴─────┴─────┴─────┴────────────┤
#   │                                          │
#   │            Keys（3 オクターブ）            │   鍵盤 → AMY（C から直接）
#   │                                          │
#   └──────────────────────────────────────────┘
#
#   Knobs 画面（[Edit] で移動、ナビの Back で戻る）
#     Amp A/D/S/R、Filter A/D/S/R、LFO Rate、Vibrato、LFO>Filter、Chorus
#
# - 鍵盤は device: に OUT（始めは AMY）を渡すだけで鳴る。発音は C 側
#   （UI タスク）なので、このスクリプトのループの速さに左右されない
# - MIDI Devices 画面で選んだ IN（既定は USB-MIDI Host の鍵盤）の演奏も AMY で
#   鳴り、画面の鍵が光る（MIDI.route で C から転送。Ruby は通らない）
# - Patch ノブでプリセット（-1 = Init、0..127 = Juno-60）を変えると、
#   ほかのノブがプリセットの値へ動く
# - [Scale] で C メジャーペンタトニックの鍵に色を付ける（鍵ごとの色）
# - [Vel] がオンのときは鍵の奥ほど弱く、手前ほど強く弾ける

require 'midi'
require 'ui'

raise "AMY is not available on this board" unless MIDIDevices.amy

syn = AMY::Synth.new(channel: 0, voices: 8)

# 鍵盤と CC のノブは MIDI Devices 画面で選ぶ OUT へ送る。始めは AMY。
# 画面で OUT を別の音源にすると演奏はそちらへ行く（音色のノブは AMY のまま）
md = UI::MIDIDevices
md.midi_out = syn.transport

CC_RESO   = 71
CC_CUTOFF = 74

CUTOFF_MIN = 100
CUTOFF_MAX = 8000
RESO_MIN   = 0.7
RESO_MAX   = 8.0
FENV_MAX   = 6.0           # octaves
ATTACK     = [1, 2000]     # ms (log)
DECAY      = [10, 4000]    # ms (log)
LFO_RATE   = [0.1, 20.0]   # Hz (log)
VIBRATO_MAX    = 0.1       # octaves
LFO_FILTER_MAX = 3.0       # octaves

# Cutoff / Reso は AMY 自身に CC を適用させる（外部鍵盤のノブでも同じ CC で効く）。
# プリセットを読み込むとマッピングが消えるので、そのたびに張り直す。
map_ccs = Proc.new do
  syn.map_cc(CC_CUTOFF, :cutoff,    min: CUTOFF_MIN, max: CUTOFF_MAX)
  syn.map_cc(CC_RESO,   :resonance, min: RESO_MIN, max: RESO_MAX)
end
map_ccs.call

# 音源の値 → ノブ位置（0..127）。値をまだ読み戻せていない（nil）ときは nil
def unscale(value, min, max, log: false)
  value.nil? ? nil : AMY.unscale(value, min, max, log: log)
end

def log_unscale(value, range)
  unscale(value, range[0], range[1], log: true)
end

# ノブを動かすだけ（notify: false でブロックは呼ばない = 何も送らない）。nil なら触らない
def move_knob(handle, v)
  handle.set_value(v, notify: false) unless v.nil?
end

def move_screen_knob(index, v)
  UI.knob_set(index, v, notify: false) unless v.nil?
end

def log_scale(v, range)
  AMY.scale(v, range[0], range[1], log: true)
end

# ---- 鍵盤 --------------------------------------------------------------------
# パッドのブロックから参照するので先に作り、レイアウトには keys: で渡す
kb = UI::Keys.new(octaves: 3, low: 48,     # C3〜C6（37 鍵）
                  velocity: 100,
                  device: md.midi_out,
                  press: :cyan,            # 指で押している鍵
                  light: :orange)          # 外部鍵盤で押されている鍵

PENTATONIC = [0, 2, 4, 7, 9]
SCALE_COLOR = UI.rgb(30, 70, 170)
ROOT_COLOR  = UI.rgb(70, 40, 160)

show_scale = Proc.new do |on|
  if on
    (0..127).each do |n|
      pc = n % 12
      kb.set_color(n, pc == 0 ? ROOT_COLOR : SCALE_COLOR) if PENTATONIC.include?(pc)
    end
  else
    kb.clear_colors
  end
end

# ---- MIDI IN（外部の USB-MIDI 鍵盤など）→ AMY と画面の鍵盤 ----------------------
# IN は MIDI Devices 画面で選ぶ（未選択なら USB-MIDI Host）。選び直したら張り直す
md.midi_in ||= MIDIDevices.usb_midi_host
routed_from = nil
follow_in = Proc.new do
  if routed_from
    MIDI.unroute(routed_from, syn.transport)
    MIDI.unroute(routed_from, kb)
  end
  routed_from = md.midi_in
  if routed_from
    MIDI.route(routed_from, syn.transport, channel: 0)   # ch 1 を AMY へ
    MIDI.route(routed_from, kb)                          # 全チャンネルで鍵を光らせる
  end
end
follow_in.call

md.on_change do |e|
  case e[:kind]
  when :select_in  then follow_in.call
  when :select_out then kb.device = md.midi_out
  end
end

# ---- レイアウト ----------------------------------------------------------------
# Patch ノブのブロックから使うので先に宣言（中身は後で入る）
play = nil
sync_knobs = nil

play = UI.layout("AMY Keys", cols: 6, rows: [2, 1, 3]) do
  knob col: 0, id: :patch, label: "Init", color: :yellow, min: -1, max: 127, value: -1 do |v|
    n = v.to_i
    current = syn.patch.nil? ? -1 : syn.patch
    if n != current
      if n < 0
        syn.init_voice
        syn.refresh
      else
        syn.patch = n          # プリセットの値を AMY から読み戻す
      end
      map_ccs.call
      play[:patch].label = n < 0 ? "Init" : AMY.patch_name(n)
      sync_knobs.call
    end
  end
  knob col: 1, id: :cutoff, label: "Cutoff", color: :cyan, value: 100 do |v|
    md.midi_out&.control_change(CC_CUTOFF, v.to_i)
  end
  knob col: 2, id: :reso, label: "Reso", color: :cyan, value: 0 do |v|
    md.midi_out&.control_change(CC_RESO, v.to_i)
  end
  knob col: 3, id: :fenv, label: "Flt Env", color: :cyan, value: 0 do |v|
    syn.filter_env = v / 127.0 * FENV_MAX
  end
  knob col: 4, id: :reverb, label: "Reverb", color: :blue, value: 0 do |v|
    syn.reverb = v / 127.0
  end
  knob col: 5, id: :volume, label: "Volume", color: :green, value: 100 do |v|
    syn.volume = v / 127.0
  end

  grid col: 0..5, row: 1, cols: 6 do
    pad col: 0, label: "Oct-", color: :blue do kb.octave_down end
    pad col: 1, label: "Oct+", color: :blue do kb.octave_up end
    pad col: 2, label: "Scale", color: :purple, type: :toggle do |on| show_scale.call(on) end
    pad col: 3, label: "Vel", color: :red, type: :toggle do |on|
      kb.velocity = on ? :position : 100
    end
    pad col: 4, label: "Edit", color: :gray do UI.knobs(bank: 1) end
  end

  keys col: 0..5, row: 2, keys: kb
end

# ---- Knobs 画面（音作り） -----------------------------------------------------
UI.knob(1, label: "Amp A", color: :green, value: 0) do |v|
  syn.amp_attack = log_scale(v, ATTACK)
end
UI.knob(2, label: "Amp D", color: :green, value: 0) do |v|
  syn.amp_decay = log_scale(v, DECAY)
end
UI.knob(3, label: "Amp S", color: :green, value: 127) do |v|
  syn.amp_sustain = v / 127.0
end
UI.knob(4, label: "Amp R", color: :green, value: 0) do |v|
  syn.amp_release = log_scale(v, DECAY)
end
UI.knob(5, label: "Flt A", color: :cyan, value: 0) do |v|
  syn.filter_attack = log_scale(v, ATTACK)
end
UI.knob(6, label: "Flt D", color: :cyan, value: 0) do |v|
  syn.filter_decay = log_scale(v, DECAY)
end
UI.knob(7, label: "Flt S", color: :cyan, value: 127) do |v|
  syn.filter_sustain = v / 127.0
end
UI.knob(8, label: "Flt R", color: :cyan, value: 0) do |v|
  syn.filter_release = log_scale(v, DECAY)
end
UI.knob(9, label: "LFO Rate", color: :orange, value: 0) do |v|
  syn.lfo.rate = log_scale(v, LFO_RATE)
end
UI.knob(10, label: "Vibrato", color: :orange, value: 0) do |v|
  syn.lfo.vibrato = v / 127.0 * VIBRATO_MAX
end
UI.knob(11, label: "LFO>Flt", color: :orange, value: 0) do |v|
  syn.lfo.filter = v / 127.0 * LFO_FILTER_MAX
end
UI.knob(12, label: "Chorus", color: :blue, value: 0) do |v|
  syn.chorus = v / 127.0
end

# ---- ノブを音源の現在値へ（起動時とプリセット変更後） --------------------------
sync_knobs = Proc.new do
  move_knob(play[:cutoff], unscale(syn.cutoff, CUTOFF_MIN, CUTOFF_MAX, log: true))
  move_knob(play[:reso],   unscale(syn.resonance, RESO_MIN, RESO_MAX))
  move_knob(play[:fenv],   unscale(syn.filter_env, 0, FENV_MAX))
  move_knob(play[:reverb], unscale(syn.reverb, 0, 1))
  move_knob(play[:volume], unscale(syn.volume, 0, 1))

  move_screen_knob(1,  log_unscale(syn.amp_attack, ATTACK))
  move_screen_knob(2,  log_unscale(syn.amp_decay, DECAY))
  move_screen_knob(3,  unscale(syn.amp_sustain, 0, 1))
  move_screen_knob(4,  log_unscale(syn.amp_release, DECAY))
  move_screen_knob(5,  log_unscale(syn.filter_attack, ATTACK))
  move_screen_knob(6,  log_unscale(syn.filter_decay, DECAY))
  move_screen_knob(7,  unscale(syn.filter_sustain, 0, 1))
  move_screen_knob(8,  log_unscale(syn.filter_release, DECAY))
  move_screen_knob(9,  log_unscale(syn.lfo.rate, LFO_RATE))
  move_screen_knob(10, unscale(syn.lfo.vibrato, 0, VIBRATO_MAX))
  move_screen_knob(11, unscale(syn.lfo.filter, 0, LFO_FILTER_MAX))
  move_screen_knob(12, unscale(syn.chorus, 0, 1))
end
# Init ボイスは reverb / chorus などを持たないので、AMY から読み戻してから合わせる
# （examples/amy_synth.rb と同じ）
syn.refresh
sync_knobs.call

UI.menu(only: [:layout, :knobs, :keys, :tempo])
play.show

sm = ScriptManager.new
until sm.stop_requested?
  UI.process
  MIDI.sleep_ms(10)
end
