# dfrobot_encoder_dual_cc.rb
# DFRobot Visual Rotary Encoder (SEN0502) を「1 ノブ = 2 パラメータ」の
# MIDI CC コントローラにする。dfrobot_encoder_cc.rb の発展版。
#
#   - エンコーダは最大 4 台 (DIP スイッチで 0x54..0x57)。起動時に自動検出する
#   - 1 台につき最大 2 つの CC パラメータを持ち、ノブのクリックで切り替える
#   - 切り替えると Tab5 の UI パッドの色が 青 (1つ目) ⇔ 緑 (2つ目) に変わる
#   - 切り替え時、そのパラメータの保存値をエンコーダのカウンタに書き戻すので、
#     LED リングの位置とパラメータの値が常に一致する
#   - UI パッド 5-12 はノート (momentary)。青/緑はパラメータ表示用に予約している
#     ので、ノートパッドの色には使わない
#
# ※ プライマリ I2C を占有するため SAM2695 (UART, 同じピン) とは同時使用不可。
#
# ---- LED について ----------------------------------------------------
# SEN0502 の LED リングは色を制御できない (レジスタは PID/VID/VERSION/ADDR/
# COUNT/KEY_STATUS/GAIN のみで、LED はモジュールがカウンタ値から自動点灯させる)。
# そのため「どちらのパラメータを操作中か」は Tab5 の UI パッド色で示す。
# エンコーダ本体の LED リングは、今まで通り現在値のバーグラフとして機能する。
#
# ---- Ableton Live 側の設定 -------------------------------------------
# 絶対値 (Absolute) で送る。dfrobot_encoder_cc.rb と同じく:
#   - Preferences > Link/Tempo/MIDI > Takeover Mode を "None" に
#   - MIDI マッピングブラウザの Mode 列は "Absolute"
# 1 ディテントあたりのカウンタ増分は GAIN で決まる (SEN0502 は 20 PPR)。
# GAIN=51 なら 1 回転でフルレンジ、1 ディテント = CC 約 6.3。

require 'midi'
require 'ui'
require 'gpio'
require 'i2c'
require 'machine'
require 'dfrobot_rotary_encoder'

# ---- 設定 ------------------------------------------------------------
CH   = 0                  # MIDI チャンネル (0-15)
GAIN = 51                 # 1 ディテント = 51 カウント -> 1 回転でフルレンジ
NOTE_VELOCITY = 100
ADDRS = DFRobotRotaryEncoder::ADDRESSES   # [0x54, 0x55, 0x56, 0x57]

# パラメータスロットごとの UI パッド色。ここを変えれば表示色を変更できる。
# 0 番目 = 1 つ目のパラメータ、1 番目 = 2 つ目のパラメータ。
PARAM_COLORS = [:blue, :green]

# エンコーダごとのパラメータ定義: [[CC番号, 表示名], [CC番号, 表示名]]
# 1 台につき最大 2 つ。1 つだけ書けばそのエンコーダは切り替えなしになる。
# 検出された順 (0x54, 0x55, ...) に上から割り当てられる。
ENCODER_PARAMS = [
  [[16, "Filter"], [20, "Reso"]],     # 1 台目
  [[17, "SendA"],  [21, "SendB"]],    # 2 台目
  [[18, "Vol"],    [22, "Pan"]],      # 3 台目
  [[19, "Delay"],  [23, "Reverb"]],   # 4 台目
]

INITIAL_VALUE = 64        # 各パラメータの初期 CC 値 (中央)
ENC_PAD_BASE  = 1         # エンコーダ i の状態表示に使う UI パッド番号 (1..4)

# ボタンの多重検出よけ (ms)。詳細は EncoderChannel#poll のコメント参照。
BUTTON_LOCKOUT_MS = 300

# ディテントで到達できる最大カウンタ値。カウンタは GAIN 刻みでしか動かないので、
# 上限は 1023 ではなく GAIN の倍数に切り下げた値になる (GAIN=51 なら 1020)。
# ここで正規化しないと CC 127 に届かず、丸めで「回しても CC が変わらない
# ディテント」が生じる。
V_MAX = (1023 / GAIN) * GAIN

# ---- 値の変換 --------------------------------------------------------
# エンコーダ値 (0-V_MAX) -> CC (0-127)
def to_cc(v)
  cc = v * 127 / V_MAX
  cc > 127 ? 127 : cc
end

# CC (0-127) -> エンコーダ値。その CC になる最小の値 (切り上げ) を返すので
# to_cc(to_value(cc)) == cc が 0-127 の全域で成り立つ。
def to_value(cc)
  (cc * V_MAX + 126) / 127
end

# ---- 1 つの CC パラメータ --------------------------------------------
class CCParam
  attr_reader :cc, :label
  attr_accessor :value

  def initialize(cc, label, value)
    @cc = cc
    @label = label
    @value = value      # 最後に送った CC 値 (0-127)。切り替えても保持される
  end
end

# ---- エンコーダ 1 台 = 複数の CCParam を切り替えて操作する ------------
class EncoderChannel
  attr_reader :index

  # @param enc [DFRobotRotaryEncoder]
  # @param params [Array<CCParam>] 1..2 個
  # @param pad_index [Integer] 状態表示に使う UI パッド番号
  def initialize(enc:, device:, index:, params:, pad_index:, channel:)
    @enc = enc
    @device = device
    @index = index
    @params = params
    @pad_index = pad_index
    @channel = channel
    @slot = 0
    @armed = true       # 次の押下を受け付けてよいか
    @lock_until = 0     # この時刻 (ms) までは押下を無視する
  end

  # 現在操作中のパラメータ
  def param
    @params[@slot]
  end

  # 現在のスロットに対応する色
  def color
    PARAM_COLORS[@slot % PARAM_COLORS.size]
  end

  # 起動時: 全パラメータの初期値をホストへ送ってから 1 つ目を有効化する。
  # (使わない方のパラメータも DAW 側と値を合わせておく)
  def start
    j = 0
    while j < @params.size
      @device.control_change(@params[j].cc, @params[j].value, channel: @channel)
      j += 1
    end
    activate
  end

  # 現在のパラメータを「操作対象」にする。
  # 保存してある CC 値をエンコーダのカウンタへ書き戻すので、ノブの物理位置と
  # LED リングがそのパラメータの値を指すようになる。
  def activate
    @enc.value = to_value(param.value)
    @device.control_change(param.cc, param.value, channel: @channel)
    UI.pad_label(@pad_index, param.label)
    UI.pad_color(@pad_index, color)
  end

  # 次のパラメータへ切り替える (パラメータが 1 つだけなら何もしない)
  def switch
    return if @params.size < 2
    @slot = (@slot + 1) % @params.size
    activate
    UI.log("enc#{@index}: #{param.label} (CC#{param.cc})")
  end

  # 毎ループ呼ぶ。クリック -> パラメータ切り替え、回転 -> CC 送信。
  def poll
    # SEN0502 のキーステータスは「押下フラグを立てて、読んだらクリア」方式。
    # 1 クリックでもフラグが複数回立つ (押している間ずっと / 離すときにもう一度 /
    # チャタリング) ため、素直に読むと 20ms ごとのポーリングで switch が何度も
    # 走ってしまう。そこで 2 段で 1 クリック = 1 回に落とす:
    #   1. フラグが降りた読みを 1 回見るまで次の押下を受け付けない (エッジ検出)
    #   2. 切り替え直後は BUTTON_LOCKOUT_MS の間ロックする (離す時の再発火よけ)
    # フラグはどちらの場合も読んでクリアしておく必要があるので、ロック中でも
    # button_down? は必ず呼ぶ。
    now = Machine.uptime_us / 1000
    if @enc.button_down?
      if @armed && now >= @lock_until
        @armed = false
        @lock_until = now + BUTTON_LOCKOUT_MS
        switch
        return
      end
    elsif now >= @lock_until
      @armed = true
    end

    v = @enc.value    # 0-V_MAX or nil (I2C 失敗)
    return unless v
    cc = to_cc(v)
    if cc != param.value
      param.value = cc
      @device.control_change(param.cc, cc, channel: @channel)
    end
  end
end

# ---- 出力先 (USB-MIDI Device / USB-C) --------------------------------
usb = MIDIDevices.usb_midi_device
unless usb
  UI.log("USB-MIDI Device not available on this board")
  exit
end
device = MIDI::Device.new(usb)

unless BoardConfig::HAS_PRIMARY_I2C
  UI.log("primary I2C not available on this board")
  exit
end

# ---- エンコーダ検出 (プライマリ I2C) ---------------------------------------------
SDA = BoardConfig::PRIMARY_I2C_SDA_PIN
SCL = BoardConfig::PRIMARY_I2C_SCL_PIN

# 直前まで SAM2695(UART) がこのピンを使っていた場合のルーティング残りを剥がす
GPIO.new(SDA, GPIO::IN)
GPIO.new(SCL, GPIO::IN)

i2c = I2C.new(unit: BoardConfig::PRIMARY_I2C_UNIT, sda_pin: SDA, scl_pin: SCL, frequency: 100_000)

chans = []
a = 0
while a < ADDRS.size
  e = DFRobotRotaryEncoder.new(i2c: i2c, address: ADDRS[a])
  if e.connected?
    e.gain = GAIN

    # このエンコーダのパラメータ定義 -> CCParam の配列
    defs = ENCODER_PARAMS[chans.size]
    params = []
    k = 0
    while defs && k < defs.size && k < PARAM_COLORS.size
      params << CCParam.new(defs[k][0], defs[k][1], INITIAL_VALUE)
      k += 1
    end

    if params.size == 0
      UI.log(sprintf("0x%02x: no params defined, skipped", ADDRS[a]))
    else
      idx = chans.size
      chans << EncoderChannel.new(
        enc: e, device: device, index: idx,
        params: params, pad_index: ENC_PAD_BASE + idx, channel: CH
      )
      UI.log(sprintf("0x%02x -> enc%d %d param(s)", ADDRS[a], idx, params.size))
    end
  end
  a += 1
end

N = chans.size
if N == 0
  UI.log("no encoder found on 0x54-0x57")
  UI.log("-> check DIP switch / wiring (i2c_scan.rb)")
else
  UI.log("#{N} encoder(s)  ch#{CH + 1}  -> USB-MIDI")
end

# ---- エンコーダ状態表示パッド (1-4) ----------------------------------
# 表示専用なのでコールバックは付けない。切り替えはノブのクリックで行う。
i = 0
while i < N
  UI.pad(ENC_PAD_BASE + i, label: "enc#{i}", color: PARAM_COLORS[0], type: :trigger)
  i += 1
end

# ---- 各エンコーダを初期化 (初期値送信 + 1 つ目を有効化) ---------------
i = 0
while i < N
  chans[i].start
  i += 1
end

# ---- ノートパッド (momentary): 押下で note_on / 離しで note_off -------
# パッド 5-12 に C4 から E5 まで。PARAM_COLORS の青/緑はパラメータ表示用に
# 予約しているので、ここでは使わない。
UI.pad(5, label: "C4", color: :red, type: :momentary) do |on|
  on ? device.note_on(60, NOTE_VELOCITY, channel: CH) : device.note_off(60, channel: CH)
end
UI.pad(6, label: "D4", color: :orange, type: :momentary) do |on|
  on ? device.note_on(62, NOTE_VELOCITY, channel: CH) : device.note_off(62, channel: CH)
end
UI.pad(7, label: "E4", color: :yellow, type: :momentary) do |on|
  on ? device.note_on(64, NOTE_VELOCITY, channel: CH) : device.note_off(64, channel: CH)
end
UI.pad(8, label: "G4", color: :cyan, type: :momentary) do |on|
  on ? device.note_on(67, NOTE_VELOCITY, channel: CH) : device.note_off(67, channel: CH)
end
UI.pad(9, label: "A4", color: :magenta, type: :momentary) do |on|
  on ? device.note_on(69, NOTE_VELOCITY, channel: CH) : device.note_off(69, channel: CH)
end
UI.pad(10, label: "C5", color: :purple, type: :momentary) do |on|
  on ? device.note_on(72, NOTE_VELOCITY, channel: CH) : device.note_off(72, channel: CH)
end
UI.pad(11, label: "D5", color: :white, type: :momentary) do |on|
  on ? device.note_on(74, NOTE_VELOCITY, channel: CH) : device.note_off(74, channel: CH)
end
UI.pad(12, label: "E5", color: :gray, type: :momentary) do |on|
  on ? device.note_on(76, NOTE_VELOCITY, channel: CH) : device.note_off(76, channel: CH)
end

# ---- メインループ ----------------------------------------------------
on_loop = Proc.new { UI.process }
MIDI.bpm_loop(120, subdivisions: 24, send_start: false, on_loop: on_loop) do |c|
  i = 0
  while i < N
    chans[i].poll
    i += 1
  end
end
