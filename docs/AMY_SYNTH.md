# AMY シンセサイザー（設計ドキュメント）

M5Stack Tab5 / Elecrow CrowPanel Advanced 7inch の内蔵スピーカーから音を出す
ソフトウェアシンセ機能。音源エンジンには [AMY](https://github.com/shorepine/amy)
（MIT, Brian Whitman / Dan Ellis）を使う。

- 対象ボード: M5Stack Tab5 / CrowPanel（どちらも ESP32-P4NRW32）
- 対象外: M5Stack CoreS3 / Freenove（ESP32-S3）。CoreS3 も I2S アンプ（AW88298）を
  持つが、S3 の CPU / 内部 RAM で FM が何ボイス出るか分からないため当面サポートしない
- 状態: **Phase 0〜3 を実装済み**。**Tab5 では内蔵スピーカーから正しく鳴ることを確認済み
  （2026-10-04）**。CrowPanel はビルドのみで、実機では未確認（下の「実装状況」）。
  方式と未確定事項は 2026-10-03 に決定済み（末尾の「決定事項」）
- AMY: `shorepine/amy@00141f2`（2026-10-01）を `mrbgems/picoruby-amy/lib/amy` に
  サブモジュールで固定
- 使い方: [mrbgems/picoruby-amy/README.md](../mrbgems/picoruby-amy/README.md) /
  サンプル [examples/amy_fm.rb](../examples/amy_fm.rb)

## 実装状況（2026-10-04）

| Phase | 内容 | 状態 |
|-------|------|------|
| 0 | picoruby-midi のトランスポート登録表 | 実装済み（ホストで単体テスト済み） |
| 1 | パーティション拡張・ボードのオーディオ電源・gem の I2S とオーディオタスク | 実装済み。Tab5 で動作確認済み / CrowPanel は未確認 |
| 2 | `AMY::Synth`（トランスポート）・`MIDIDevices.amy`・`BoardConfig::HAS_AMY` | 実装済み。Tab5 で動作確認済み |
| 3 | `AMY.command` / `AMY::FM` / CC マッピング / 直接セット・サンプル | 実装済み。Tab5 で発音を確認済み。現行の `examples/amy_fm.rb`（momentary パッド・XYPad・3 バンクのノブ）は未確認 |
| 4 | `MIDI.route`（C 側の MIDI Thru） | 未着手 |
| 5 | 負荷・遅延の詰め | 未着手（`AMY.render_load` の実測から） |

ファイル：

| 場所 | 中身 |
|------|------|
| `mrbgems/picoruby-amy/` | gem 本体。独立リポジトリ（[kirikak2/picoruby-amy](https://github.com/kirikak2/picoruby-amy)）を Midori のサブモジュールとして取り込み、その中で `lib/amy`（shorepine/amy）をサブモジュールにしている |
| `mrbgems/picoruby-amy/mrblib/dx7_patches.rb` | DX7 プリセット名の表（`AMY.patch_name`）。AMY の `patches.h` のコメントから生成 |
| `mrbgems/picoruby-amy/ports/esp32/amy_port.c` | I2S・オーディオタスク・`amy_start`・トランスポート登録 |
| `mrbgems/picoruby-amy/mrblib/amy.rb` | `AMY` / `AMY::Synth` / `AMY::FM` / `AMY::FM::Operator` |
| `components/amy/CMakeLists.txt` | AMY エンジンの IDF コンポーネント（P4 のときだけ中身がある） |
| `main/platform/board_audio.cpp` | ボード依存の電源 ON（Tab5: ES8388 + SPK_EN / CrowPanel: GPIO 30） |
| `mrbgems/picoruby-midi/src/midi_transport_registry.c` | Phase 0 の登録表（OS 非依存） |
| `components/picoruby-esp32/` | ピン等の注入（CMakeLists）・`HAS_AMY`・`MIDIDevices.amy`・停止時の `AMY_GEM_reset()` |
| `partitions.csv` | factory を 0x310000 から 0x3F0000（PicoRuby のフラッシュ FAT 0x210000〜0x310000 の後ろ） |

設計からの変更点：

- **ピン等は `BoardConfig` ではなくビルド定義だけで渡す**。Ruby 側は `HAS_AMY` しか
  使わないため（`AMY_GEM_I2S_*` 等。一覧は `include/amy_gem_config.h`）
- **C API の I2S 設定構造体はやめた**。設定は全部ビルド定義で、実行時に渡すのは
  電源コールバックだけ
- **`src/amy_gem.c`（OS 非依存部）は作らなかった**。MIDI パケット → AMY の変換は
  数行なので ESP32 port に置いた
- **`AMY.send` は `AMY.command` にした**。`send` は Ruby の `Object#send` と衝突するため
- **オペレータ指定の CC マッピングは `fm.map_cc(21, :level, op: 2)`** の形にした
- **`AMY.scale` の対数変換は C 側**（`AMY._log_scale`）。この mruby/c ビルドには
  `Math` も `Float#**` も無い（`MRBC_USE_MATH = 0`）
- **浮動小数の書式**：mruby/c の `Float#to_s` は `%g` なので、1e-4 未満 / 1e6 以上は
  指数表記になり、ワイヤメッセージ中の `e` が次のパラメータ文字と誤読される恐れがある。
  `AMY.format_value` で 1e-4 未満は 0、1e5 以上は整数に丸めて出す
- **スクリプト停止時のリセット**：`picoruby_esp32_midi_cleanup()`（全停止経路で呼ばれる）
  の最後で `AMY_GEM_reset()` を呼ぶ。All Sound Off も登録表経由で AMY に届く

ビルド結果（CrowPanel / midi_device、2026-10-03）：

- アプリ 2.12 MB（AMY 込み）。拡張前の factory（1.94 MB）には入らない
- AMY が内部 RAM に静的に置く量：.data 11.0 KB（クリッピング表など `DRAM_ATTR`）、
  .bss 4.8 KB、IRAM .text 10.2 KB。[MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) の
  「内部 DRAM の静的配置が変わると LCD が出なくなることがある」に当たり得るとして
  警戒したが、**Tab5 では起動・画面表示とも問題なかった**

実機で確認したこと（Tab5 / midi_device、2026-10-04）：

- 起動して画面が出る（上記の内部 RAM の件は問題なし）
- `AMY.start` の後も USB-C のコンソールと USB-MIDI デバイスが生きている
  （§2「Tab5：I2S のピンが USB PHY のパッドと重なる」の修正後）
- ES8388 経由で内蔵スピーカーから正しく鳴る（`AMY.bleep`、`AMY::FM` + `MIDI::Device` で発音）

実機確認の途中で起きた問題（どちらも修正済み。詳細は §2）：

1. **書き込み後に画面が真っ暗**：拡張したアプリ領域が PicoRuby の固定アドレスの
   フラッシュ FAT（0x210000〜）と重なり、起動時のフォーマットでアプリが壊れた。
   factory を 0x310000 へ移した（「メモリとフラッシュ」）
2. **`AMY.start` でコンソールが止まり、パッドも無音**：I2S のピン（GPIO 26/27）が USB PHY1 の
   パッドで、ESP-IDF が既定の PHY mux を前提に USB-C 側の TinyUSB のパッドを切っていた。
   gem の port でパッド有効フラグを mux の実状に合わせて直す（`p4_usb_pads_fixup()`）

まだ確認していないこと：

1. CrowPanel 全般：音が出るか、パンを振った音が左右に分かれて出るか
2. 現行の `examples/amy_fm.rb`：momentary パッド、XYPad のグライドの音程、ノブ（パッチ名の表示、
   パッチ切り替え後の CC マッピングの掛け直し、オペレータの ADSR）
3. `AMY.render_load` の値（`examples/amy_fm.rb` がログに 5 秒ごとに出す）
4. スクリプトを止めて別のスクリプトを起動したとき、前の音色や CC マッピングが残らない

このドキュメントは次の 4 点を検討する。

1. 実装方式 — Midori の機能にするか、PicoRuby の mrbgem にするか
2. 音の出力 — 両ボードのスピーカーまでの経路（I2S でよいか）
3. デバイス透過性 — `MIDI::Device` として他の MIDI デバイスと同列に扱えるか
4. 音源 — まず FM。パラメータ操作を Knob / Pad / XYPad とどう繋ぐか

結論（2026-10-03 決定）：

| 論点 | 結論 |
|------|------|
| 1. 実装方式 | **mrbgem（`mrbgems/picoruby-amy`、`require 'amy'`）として切り出す**。ボード依存部分（コーデック初期化・アンプ電源）は gem に入れず Midori 側に置き、コールバックで注入する。gem に残るボード依存は「I2S のピン番号と形式」だけで、これはビルド定義／`BoardConfig` で外から渡せる |
| 2. 音の出力 | **I2S で正しい**。ただし Tab5 は I2S → **ES8388 コーデック** → アンプ、CrowPanel は I2S → **I2S 入力アンプ直結**と構成が違う。I2S チャネルは AMY 内蔵のドライバではなく gem 側で張る（理由は後述） |
| 3. デバイス透過性 | gem が **MIDI トランスポート**（`send_packet` 等）を実装すれば `MIDI::Device.new(amy)` がそのまま動く。ただし `trigger` / Tombola / ノートスケジューラは picoruby-midi の ESP32 port が送信先を**決め打ち**しているため、**picoruby-midi にトランスポート登録 API を足す（案 2）**。これを Phase 0 として AMY より先に行う |
| 4. FM 音源 | AMY の DX7 互換 FM（`ALGO` 波形、128–255 の DX7 プリセット）を使う。Ruby API は「ワイヤメッセージ直送」「FM パッチオブジェクト」「CC マッピング」の 3 層にし、UI からは **CC を送るだけ**で操作できるようにする（外部シンセと同じコードで動く） |

---

## 1. 実装方式：Midori 機能か mrbgem か

### 「デバイス依存のコード」の中身を分解する

「デバイス依存が多い」という懸念を、実際に何がボードに依存するかで分けてみる。

| 部分 | 内容 | ボード依存? | 置き場所（提案） |
|------|------|-------------|-----------------|
| AMY エンジン本体 | オシレータ・FM・フィルタ・エフェクト・ボイス割り当て・MIDI 解釈 | なし（ポータブル C） | gem（`lib/amy` にサブモジュール） |
| レンダリングタスク | AMY の ESP32 マルチコア描画（`i2s.c` の FreeRTOS タスク） | ESP32 依存だが**ボード非依存** | gem（AMY 側のコードをそのまま使う） |
| I2S 出力 | `i2s_new_channel` / `i2s_channel_write` | **ピン番号・I2S 形式・MCLK 有無だけ** | gem（ピン等はビルド定義 or `BoardConfig` で受け取る） |
| コーデック初期化 | Tab5 の ES8388 を I2C で設定 | **完全にボード依存** | Midori（`main/platform/`） |
| アンプ電源 | Tab5: IO エキスパンダ PI4IOE5V6408 のビット / CrowPanel: GPIO30 | **完全にボード依存** | Midori（`main/platform/`） |
| I2C バスの所有 | Tab5 の内部 I2C は M5Unified（`M5.In_I2C`）が握っている | **Midori の構成に依存** | Midori |
| Ruby API / トランスポート | `AMY::Synth`、`send_packet` | なし | gem |

つまり**本当にボード依存なのは「コーデック」と「アンプ電源」の 2 点だけ**で、
これは既存の gem でも同じ形で解決済みのパターンになっている：

- `picoruby-uart_midi` / `picoruby-sam2695` … ピン番号は呼び出し側（`BoardConfig::SAM2695_TX_PIN`）から渡す
- `picoruby-usb_midi_device` … 製品名・HS/FS ポート・CDC 有無は CMakeLists から `target_compile_definitions` で注入し、CDC 受信は `USB_MIDI_DEVICE_set_cdc_rx_callback()` でアプリが登録する

AMY gem も同じ形にできる。

### 方式の比較

| | A. Midori の機能（`main/` 以下） | B. mrbgem（`mrbgems/picoruby-amy`） |
|---|---|---|
| ボード依存コード | 1 か所にまとまる | コーデック / アンプは Midori 側、それ以外は gem。**境界が 1 本のコールバック**で済む |
| `MIDI::Device` との接続 | picoruby-midi から見ると「Midori 固有の何か」になる。トランスポートの Ruby クラスも結局どこかの gem に要る | 他のトランスポート gem（uart_midi, usb_midi_*）と**同じ形**。標準化計画の依存図にそのまま収まる |
| Ruby API の置き場所 | `main_task_base.rb` か picoruby-ui に押し込むことになる | gem の `mrblib/` と `sig/` に自然に置ける |
| 他ボード展開 | ボードを足すたびに Midori 本体を改修 | I2S DAC 直結のボードならピン設定だけで動く余地がある（CoreS3 は性能面で対象外と決定済み） |
| upstream PicoRuby | 不可 | 方針として upstream に入れない gem 群（CLAUDE.md の midori-local mrbgem）と同列で管理できる。将来出す選択肢も残る |
| 実装コスト | やや低い | gem の雛形 + ビルド配線の分だけ高い（既に 8 個の前例があるので小さい） |

### 決定：B（mrbgem）＋ ボード依存部はアプリから注入

```
mrbgems/picoruby-amy/
├── mrbgem.rake                      require_name = 'amy'
├── lib/amy/                         shorepine/amy（git サブモジュールで固定）
├── include/amy_gem.h                アプリ向け C API（電源コールバック等）
├── include/amy_gem_config.h         ビルド定義（ピン・タスク・DMA 等）の既定値
├── src/amy.c, src/mrubyc/amy.c      mruby/c バインディング
├── ports/esp32/amy_port.c           I2S チャネル + オーディオタスク + amy_start()
├── mrblib/amy.rb                    AMY, AMY::Synth（トランスポート）, AMY::FM
├── sig/amy.rbs
├── example/fm_basic.rb
└── README.md

components/amy/CMakeLists.txt        AMY エンジンの IDF コンポーネント
main/platform/board_audio.cpp        ES8388 + SPK_EN（Tab5）/ GPIO 30（CrowPanel）
```

境界の C API（`include/amy_gem.h`）：

```c
/* コーデック / アンプの電源。オーディオタスク上で、I2S のクロックが
 * 動き出した後・最初のブロックを書く前に true で呼ばれる */
typedef void (*amy_gem_power_cb_t)(bool on, void *arg);
void AMY_GEM_set_power_callback(amy_gem_power_cb_t cb, void *arg);
```

ピン・I2S 形式・MCLK 比・タスクのコア / 優先度は
`components/picoruby-esp32/CMakeLists.txt` のボード分岐で決め、
`target_compile_definitions` で `AMY_GEM_*` として port に注入する
（`picoruby-usb_midi_device` と同じ流れ）。Ruby 側には `BoardConfig::HAS_AMY` だけを出す。

AMY 本体は ESP-IDF コンポーネントとしての `CMakeLists.txt` を持っていないので、
`components/amy/` に薄い IDF コンポーネントを作り、ソースは
`mrbgems/picoruby-amy/lib/amy/src` を参照する（`-O2`、警告をエラーにしない）。
取り込むのは AMY の Makefile の `SOURCES` から `libminiaudio-audio.c` を除き
`i2s.c` を足したもの。`usb.c`（TinyUSB ディスクリプタ。Midori は自前の USB スタック）、
デスクトップ用 MIDI、Python / サンプルのフロントエンドは入れない。

---

## 2. 音の出力

### 結論：I2S でよい。ただし 2 ボードで下流が違う

両ボードとも SoC からはデジタル I2S で出るが、その先の構成が違う。

| | M5Stack Tab5 | CrowPanel Advanced 7" |
|---|---|---|
| I2S ペリフェラル | I2S0（M5Unified の設定） | I2S1（Elecrow サンプルの設定） |
| BCLK | GPIO 27 | GPIO 22 |
| WS (LRCLK) | GPIO 29 | GPIO 21 |
| DOUT | GPIO 26 | GPIO 23 |
| MCLK | **GPIO 30（必須）** | なし |
| DIN（参考） | GPIO 28（ES7210 マイク ADC） | —（マイクは PDM: CLK 24 / DIN 26） |
| I2S の受け手 | **ES8388 コーデック**（I2C 0x10、内部 I2C SDA31/SCL32） | **NS4168 × 2**（I2S 入力のモノラル D 級アンプを L / R に 1 個ずつ。コーデック無し） |
| アンプ電源 | PI4IOE5V6408（I2C 0x43）レジスタ 0x05 の bit1 を ON | **GPIO 30 を LOW** で ON（P-ch MOSFET AO3401 のゲート。active-low） |
| I2S 形式 | ES8388 側で選ぶ（M5Unified は reg23=0x18 で I2S 16bit に設定） | **Philips**（標準 I2S。サンプルも `bit_shift = true`） |
| 根拠 | `managed_components/m5stack__m5unified/src/M5Unified.cpp` の `_speaker_enabled_cb_tab5` とボード設定 | Elecrow リポジトリ `example/V1.2/idf-code/Lesson11-*/peripheral/bsp_audio/` |

Midori で既に使っているピンとの衝突は無い（Tab5: SAM2695 は 53/54、SD は SPI。
CrowPanel: バックライト 31、タッチ 45/46、SD 39/43/44、SAM2695 47/48）。
CrowPanel の GPIO 30 は Tab5 では MCLK だが、別ボードなので問題ない。

> **CrowPanel の音声出力（回路図 `ESP32-P4 Display 7.0 inch V1.0.sch` より）**
>
> - **NS4168 が 2 個**（U3 / U13）。モノラルの I2S 入力 D 級アンプを左右 1 個ずつ
>   使う構成で、Makerguides の記事の "Dual-channel speaker outputs" はこれのこと
>   （記事は "Audio Codec: NS4168" と書くが、NS4168 はコーデックではない）。
>   NS4168 は CTRL ピンの電圧で L / R どちらのスロットを鳴らすか選ぶので、
>   2 個で L / R を分担していると読める。したがって **AMY のステレオ出力（パン）は
>   そのまま左右に出る**見込みで、出力前に L+R を混ぜる処理は不要
> - 2 個の NS4168 の I2S ネット名は `I2SOUT_SCLK1/LRCK1/SD1` と
>   `I2SOUT_SCLK2/LRCK2/SD2` で分かれているが、Elecrow のサンプルは 1 本の I2S
>   （GPIO 21/22/23）だけでステレオ再生している。両方が同じ 3 本の GPIO に
>   繋がっていると判断する（0Ω 抵抗経由と思われる）
> - **アンプ電源は P-ch MOSFET（AO3401）のハイサイドスイッチ**。`AUDIO_CTRL`
>   （= GPIO 30）がゲートに入るので Low で導通し、`AUDIO_OUT_5V` / `AUDIO_VDD_3V3`
>   がアンプに供給される。GPIO 30 が active-low なのはこのため。CTRL ピンに
>   直結ではない
> - I2S 形式：**Philips**。NS4168 は MCLK 不要で、Elecrow サンプル
>   （Philips・MCLK 無し・ステレオ）で鳴っている
> - 回路図には **ES8311（DAC）/ ES7210（マイク ADC）と NS4268/NS4263B
>   （ヘッドホンアンプ）のフットプリントがあるが、すべて NC（未実装）**。
>   I2S 出力は NS4168 にしか行っていない。将来の基板リビジョンでコーデックが
>   実装されたら、Tab5 と同じく I2C での初期化と MCLK が必要になる
> - マイクは PDM のデジタルマイク（LMD3526B381）。今回は使わない

### I2S は AMY 内蔵ドライバではなく gem で張る

AMY には ESP32 用の I2S 出力（`AMY_AUDIO_IS_I2S`, `src/i2s.c`）があるが、
そのままは使わない：

1. **I2S 形式が MSB（left-justified）決め打ち**（`I2S_STD_MSB_SLOT_DEFAULT_CONFIG`）。
   CrowPanel のアンプは Philips 形式で、1 ビットずれる
2. **ポートが `I2S_NUM_AUTO`**。Tab5 で M5Unified と衝突させないためにポートを明示したい
3. クロック源（P4 の APLL 等）やコーデック起動順を制御できない

代わりに AMY を `audio = AMY_AUDIO_IS_NONE` で起動し、**gem のオーディオタスクが
`amy_update()` を回し、`write_samples_fn` で自前の I2S チャネルへ書く**。
AMY 側はこの使い方を正式にサポートしている（`api.c` の `amy_update()`）。

```
[gem audio task (Core 0, prio 2〜3)]
   loop:
     amy_update()  ── 描画（同じタスク内で全 osc を描画。AMY 内部タスクは使わない）
       └─write_samples_fn──▶ i2s_channel_write() ──▶ ES8388 / NS4168 ──▶ スピーカー
                              （DMA に空きができるまでブロック ＝ 5.8 ms ごとのペース）
```

AMY 内部のタスク分割は使わない（`platform.multithread = 0` / `platform.multicore = 0`）。
描画と I2S 書き込みを gem のオーディオタスク 1 本で行い、優先度とコアはこの 1 本だけで
管理する（次節「タスクとコア配置」）。

**注意（AMY の仕様）**：I2S を AMY に任せない場合、`amy_platform_init()` が
**`amy_start()` を呼んだタスク**を `amy_update` の通知先として登録する。
multithread を切っていれば通知は飛ばないが、前提を崩さないよう
`amy_start()` はオーディオタスク自身の中で呼ぶこと。

### オーディオ設定（初期値の案）

| 項目 | 値 | メモ |
|------|----|------|
| サンプルレート | 44100 Hz | AMY の既定。P4 ではビット落とし（`>>1`）が無効化済み（AMY issue #1169） |
| ブロック | 256 サンプル（`BLOCK_SIZE_BITS=8`） | 5.8 ms/ブロック。遅延が気になれば 7（128）へ |
| DMA | `AMY_I2S_DMA_BLOCKS=4` 程度 | 256×4 ≈ 23 ms。既定 6 だと ≈ 35 ms |
| ビット幅 | 16 bit ステレオ | `amy_update()` が返すのは int16 インタリーブ |
| MCLK（Tab5） | **128 × fs**（`I2S_MCLK_MULTIPLE_128`） | ES8388 reg24 = 0x00 とセット。下記「ES8388 の MCLK 比」 |

### ES8388 の MCLK 比（Tab5）

Tab5 の ES8388 はスレーブ（reg8 = 0x00）で、MCLK / BCLK / LRCK はすべて
ESP32-P4 が出す。ES8388 は MCLK を分周して内部クロックを作るが、
「MCLK が LRCK の何倍か」は自動判定せず **reg24（DACCONTROL2）の DACFsRatio**
で教える必要がある。基準はただ一つ：

> **ESP32 側の `mclk_multiple` と ES8388 の reg24 を同じ比にそろえる。**

ずれるとピッチ / テンポずれ、無音、ノイズになる。比の値そのものに正解は無く、
実績のある組み合わせは 2 つ：

| 実装 | ESP32 側 MCLK | ES8388 reg24 |
|------|---------------|--------------|
| M5Unified（Tab5 実機で動作実績） | 128 × fs（`Speaker_Class.cpp`：MCLK 使用時 `div_m = 8` × 16 bit） | 0x00（128） |
| Espressif `esp_codec_dev` の es8388 ドライバ | 256 × fs（IDF 既定） | 0x02（256） |

**Midori は M5Unified と同じ 128 × fs / reg24 = 0x00 を採用する。**
`_speaker_enabled_cb_tab5` のレジスタ列（reg24 = 0x00 を含む）を無改造で流用できるため。
256 × fs にする場合は reg24 を 0x02 に変えて**必ずセットで**切り替える
（M5Unified のレジスタ列のまま IDF 既定の 256 × fs を使うと 2 倍ずれる）。

比を選ぶときの制約：

1. ES8388 が対応する比であること（シングルスピードで 128 / 256 / 384 / 512 … 。
   一覧はデータシートで要確認）。fs ≤ 48 kHz なので DACFsMode（bit5）は 0 = シングルスピード
2. BCLK で割り切れること。16 bit ステレオは BCLK = 32 × fs なので比は 32 の倍数。
   24 bit データにするなら 384（IDF の注記）
3. 44.1 kHz 系の MCLK は 160 MHz / 40 MHz から整数分周できず小数分周になり、
   ジッタが少し乗る。気になれば P4 の APLL（`I2S_CLK_SRC_APLL`）で正確に作れる。
   音質を詰める段階で検討する
4. 将来マイク（ES7210、DIN 28）を使うときは、同じ MCLK を受ける ES7210 にも
   同じ比を設定する（今回は DAC のみなので対象外）

### タスクとコア配置（2026-10-03 決定：Core 0 / 優先度 2〜3）

#### 守るべき時間制約

1 ブロック = 256 サンプル ÷ 44.1 kHz = **5.8 ms**。条件は 2 つある：

- **平均**：1 ブロックの描画が 5.8 ms を超え続けると必ず途切れる
- **単発の遅れ**：DMA に溜まっている分は吸収できる。`AMY_I2S_DMA_BLOCKS=4` なら
  満杯の状態から**約 17〜23 ms の停止**まで耐える。つまり「毎回 5.8 ms 以内」ではなく
  「**最悪の停止 < DMA 残量**」が条件

オーディオのループは周期タイマではなく `i2s_channel_write()` のブロックで
ペースが決まる。重要なのは「DMA に空きができた瞬間に CPU をもらえるか」で、
これをタスクの優先度とコア配置で保証する。

#### 既存タスクの配置（2026-10-03 時点）

| コア | タスク | 優先度 | 性質 |
|------|--------|--------|------|
| Core 1 | tud_task（TinyUSB） | 5 | 短い処理の繰り返し |
| Core 1 | supervisor / console / USB-MIDI Device TX | 4 | ほぼ待機 |
| Core 1 | **PicoRuby VM** | **3** | **スクリプト実行中は CPU を手放さない**（Ruby タスクが全部 sleep したときだけ `vTaskDelay(1)` で譲る） |
| Core 1 | midi_input | 1 | ほぼ待機 |
| Core 0 | `app_main`（UI ループ：`platform_update` → `ui_update`） | 1 | 描画。重いが待たされても壊れない |
| Core 0 | uart_midi 入力 / USB Host（`usb_midi_drv`, `usb_lib_evt`） | 1 | ほぼ待機 |

`FREERTOS_HZ=100`（tick 10 ms）なので、同じ優先度のタスク同士の交代は 10 ms 粒度で、
5.8 ms より粗い。

#### 決定

**オーディオタスクは Core 0、優先度 2〜3**（初期値 3。実測で詰める）。

- Core 1 にしない理由：PicoRuby VM（優先度 3）が計算ループ・`load` のコンパイル・
  irb の評価・GC で数十〜数百 ms Core 1 を占有し得る。VM より下（例：優先度 1）だと
  その間オーディオが一切動けず、DMA 残量（約 20 ms）を使い切って途切れる。
  スクリプトの書き方で音が途切れる作りは避ける
- Core 0 なら UI ループ（優先度 1）より上なので、UI 描画の重さに関係なく
  DMA に空きができた時点で走れる。代償は**描画負荷の分だけ UI のフレームレートが
  落ちる**こと（UI は遅れても壊れない）
- Core 0 の USB Host タスク（優先度 1）も同様に描画中は待たされる。USB-MIDI の
  受信はリングバッファで受けるので数 ms の遅れは許容できる
- AMY 内部タスク（`amy_r_task` / `amy_fb_task`、既定 `ESP_TASK_PRIO_MAX - 1`）は
  `multithread = 0` / `multicore = 0` で**作らせない**。最上位優先度で両コアに
  張り付く既定構成は tud_task まで巻き込むため
- タスクは gem の `ports/esp32/amy_port.c` で作る。コア・優先度はビルド定義
  （`AMY_GEM_TASK_CORE` / `AMY_GEM_TASK_PRIORITY`、既定 0 / 3）で上書き可能にする

#### Tab5：I2S のピンが USB PHY のパッドと重なる（2026-10-04）

ESP32-P4 の GPIO 24/25 と 26/27 は、内蔵フルスピード USB PHY 0 / 1 のパッドを兼ねる。
ESP-IDF の `gpio_ll_func_sel()`（`hal/esp32p4/include/hal/gpio_ll.h`）は、これらの
ピンを別の機能に切り替えるとき USB パッドを切るが、**既定の mux（PHY0 → USB-Serial/JTAG、
PHY1 → OTG1.1）を決め打ち**している：26/27 なら OTG1.1 の、24/25 なら USJ のパッドを切る。

Tab5 の midi_device モードでは、`picoruby-usb_midi_device` が USB-C（PHY0 側の配線）で
TinyUSB を動かすために **mux を入れ替えている**（OTG1.1 → PHY0、USJ → PHY1）。
この状態で I2S が GPIO 26（DOUT）/ 27（BCLK）を取ると：

- 切られるのは OTG1.1 のパッド ＝ **USB-C の TinyUSB**。CDC コンソールと USB-MIDI
  デバイスが落ちる。irb では `AMY.start` の直後にコンソールが止まって「フリーズ」に
  見えた（画面・ログ画面は動いており、`AMY_GEM_start` も 10 ms で完了していた）
- 本当に PHY1 を持っている USJ のパッドは有効のまま残り、I2S の信号の下で USB モード
  のまま。I2S が ES8388 に届かない（最初の「パッドで鳴らない」もこれと考えられる）

対策（gem の `ports/esp32/amy_port.c`、ESP32-P4 のみ）：I2S を設定する前に両コントローラの
パッド有効フラグを保存し、設定後に復元してから、`LP_SYS.usb_ctrl` の mux を見て
**I2S が取ったピンの PHY に実際に繋がっているコントローラだけ**を切る
（`p4_usb_pads_fixup()`）。
CrowPanel（I2S は 21〜23）は対象外で、何もしない。

#### コンソールに書かない

上の件の調査中に入れた予防策。midi_device モードのコンソールは USB CDC で、TinyUSB の
タスク（Core 1）以外のコアから書き込むと壊れる（[console_input.c](../components/picoruby-esp32/console_input.c)
が Core 1 に固定されているのと同じ理由）。

- オーディオタスク（Core 0）の冒頭で stdout / stderr を `/dev/null` に向ける（newlib では
  タスクごと）。AMY 内部の `fprintf(stderr, ...)` とボードの電源コールバックのログも
  止まる（UI のログ画面には出る）。起動の経過は `AMY_GEM_start()` を呼んだタスクでログする
- 過負荷フックは回数を数えるだけ（`AMY.overloads`）
- `trigger` の自動ノートオフは esp_timer タスク（Core 0）から AMY に届く。AMY はノートの
  対応が取れないと呼び出し元のタスクで `stderr` に警告を出すので、`AMY::FM` は
  パッチ読み込みのたびに synth フラグ `SYNTH_FLAGS_NO_NOTE_WARNINGS`（8）を立てる。
  `AMY.command` で synth を直接作る場合は `synth_flags: 8` を自分で付けること

#### 描画負荷の上限

優先度を上げた分、描画が重すぎると Core 0 の下位タスク（UI）が止まる。上限は 2 段で抑える：

1. **ボイス数**（`max_voices`, `max_oscs`）を控えめから始める。FM は 1 ボイス 7 osc
   （ALGO 1 + オペレータ 6）を食う
2. **過負荷フェイルセーフ**（`overload_threshold`）を既定 0.98 から **0.7 前後**へ下げる。
   描画が 1 ブロックの 70% を超え続けたら AMY が自分をリセットするので、UI に最低 30% の
   CPU が残る。`amy_external_overload_hook` で UI ログに出す

P4 での実際の描画時間は未計測。Phase 1 で `amy_get_render_load()` を使い、
FM 6 ボイス（+ reverb）の負荷を測って、優先度（2 か 3）・ボイス上限・
`overload_threshold` を決める。描画が 1 コアに収まらないと分かった場合に限り、
`multicore` の再検討（Core 1 側の補助タスクの優先度を VM より上にする必要がある）を行う。

### メモリとフラッシュ

- **アプリパーティションを拡張する（決定）**。現状 `partitions.csv` の factory は
  0x1F0000（約 1.94 MB）で、Tab5 ビルドの `midori.bin` は約 1.67 MB。
  AMY は DX7/Juno パッチ表（`patches.h`）、ピアノ（`interp_partials.h`）、
  PCM サンプルを持つので、このままでは入らない。
  PCM は最小セット（`pcm_samples_tiny.h`）にし、GAMMA9001 ドラムは入れない

  `partitions.csv` は全ボード共通。フラッシュ容量は Tab5 / CrowPanel / CoreS3 が
  16 MB、**Freenove が 8 MB で最小**。

  **注意：PicoRuby のフラッシュ FAT はパーティション表を見ない**。
  `picoruby-filesystem-fat` の ESP32 port（`ports/esp32/flash_disk.c`）が
  **固定アドレス 0x210000 から 1 MB** を直接読み書きしている。旧レイアウトでは
  factory（〜0x200000）と coredump（0x200000〜0x210000）の直後にあたり、
  たまたま空いていた。

  > **2026-10-04 の事故**：最初は factory を 0x10000 から 0x3F0000 に広げただけだった。
  > アプリ（2.12 MB、〜0x214ED0）が FAT 領域に食い込み、起動時に PicoRuby が
  > FAT をフォーマットしてアプリの末尾を消したため、CrowPanel に書き込むと
  > **画面が真っ暗のまま**になった。

  そこで factory を FAT の後ろへ移し、FAT 領域は予約パーティションとして明記した
  （中身を読むのは引き続き `flash_disk.c` の固定アドレス）：

  ```
  # Name,       Type, SubType,  Offset,   Size
  nvs,          data, nvs,      0x9000,   0x6000,
  phy_init,     data, phy,      0xf000,   0x1000,
  coredump,     data, coredump, 0x200000, 0x10000,
  picoruby_fat, data, fat,      0x210000, 0x100000,
  factory,      app,  factory,  0x310000, 0x3F0000,
  ```

  factory の末尾は 0x700000 で 8 MB に収まる。0x10000〜0x200000 は空きのまま。
  パーティションテーブルが変わるので、書き込みは `idf.py flash`（テーブルごと書く）で
  行うこと。AMY を含まない S3 ビルドも同じ表を使う（領域が余るだけ）。
  `flash_disk.c` の `FLASH_OFFSET` / `FLASH_SIZE` を変える場合は、この表も合わせて直すこと
- RAM は `amy_config_t.ram_caps_*` で大物（events / synth / delay / sample）を
  PSRAM に置く。**内部 DRAM の .bss に大きな static を置かない**
  （[MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) の落とし穴）。
  描画ホットパスは AMY 側が `IRAM_ATTR` で内部 RAM に置く
- reverb / echo / chorus は既定で ON だが 1 バスで数百 KB 使うので、
  最初は reverb のみ ON・`max_buses` を絞る

---

## 3. デバイス透過性（MIDI::Device として使う）

### トランスポートとしての AMY

`MIDI::Device` がトランスポートに要求するのは
`send_packet(cable, cin, b1, b2, b3)` / `bytes_available` / `read_available` /
`connected?`（+ `transport_id`）だけ。AMY gem はこれを実装する：

```ruby
amy = MIDIDevices.amy            # => AMY::Synth（未対応ボードでは nil）
dev = MIDI::Device.new(amy)
dev.note_on(60, 100)             # → AMY が鳴る
dev.control_change(74, 90)       # → AMY の CC マッピングへ
dev.program_change(3)            # → DX7 バンク内の 4 番へ（後述）
```

C 側の `send_packet` は USB-MIDI パケットを MIDI バイト列に戻して
`amy_event_midi_message_received(bytes, len, 0)` に渡すだけ。
AMY がボイス割り当て・ノートオフ・サステイン・ピッチベンド・プログラムチェンジを
内部で処理する。入力（`read_available`）は当面空を返す（送信専用）。

**チャンネルの対応**：AMY の `synth` 1–16 が MIDI チャンネル 1–16 に対応する。
`MIDI::Device` のチャンネルは 0 始まりなので **`channel: 0` ＝ AMY の synth 1**。
AMY は synth が定義されたチャンネルにしか反応しない（`_midi_channel_active`）。

### 前提作業：picoruby-midi にトランスポート登録 API を足す

`note_on` 等は Ruby の `send_packet` を通るので問題ないが、**次の経路は
picoruby-midi の ESP32 port（[ports/esp32/midi.c](../mrbgems/picoruby-midi/ports/esp32/midi.c)）
が送信先を 3 種類に決め打ちしている**：

- `MIDI::Device#trigger` / `trigger_batch` → `MIDI_Note_trigger(transport_mask, ...)`
- ノートスケジューラの自動ノートオフ（`scheduler_send_packet`）
- Tombola の C++ 側発音（`MIDI_Note_trigger()` を直接呼ぶ）
- `UI::Tombola.transport_mask_for` の Ruby 側マスク表

`transport_mask` は `MIDI_TRANSPORT_USB 0x01` / `SAM2695 0x02` / `USB_DEVICE 0x04`
しか無いので、このままでは **`trigger` で AMY が鳴らない**。
マルチタッチの Pads はほぼ `trigger` を使うので、これは必須。

2 案：

| | 案 1: ビットを 1 つ足す | **案 2: 登録 API（採用）** |
|---|---|---|
| 変更 | `MIDI_TRANSPORT_AMY 0x08` を足し、`midi.c` に `#include "amy_gem.h"` | `MIDI_transport_register(uint8_t bit, const midi_transport_t *)` を作り、各トランスポート gem が起動時に自分を登録 |
| 依存方向 | picoruby-midi → picoruby-amy（逆向き。標準化計画の「プロトコル層が個別トランスポートを知らない」に反する） | 個別 gem → picoruby-midi（正しい向き） |
| 今後 | BLE-MIDI 等でも同じ改修が毎回要る | 新トランスポートは picoruby-midi 無改修 |

**案 2 を採用し、AMY 実装の前段（Phase 0）として行う**（2026-10-03 決定）。
[PICORUBY_MIDI_STANDARDIZATION.md](PICORUBY_MIDI_STANDARDIZATION.md) の方針や
`midi.c` 冒頭コメントの「将来は各トランスポート gem に所有を移す」とも一致する。
案 1（`MIDI_TRANSPORT_AMY 0x08` の直書き）は暫定策としても使わない。

#### 登録 API の形（案）

```c
/* include/midi_transport.h に追加 */
#define MIDI_TRANSPORT_MAX 8   /* transport_mask は uint8_t のまま。1 ビット = 1 トランスポート */

/* 空いているビットを 1 つ割り当てて返す（0x01, 0x02, ...）。満杯なら 0 */
uint8_t MIDI_transport_register(const midi_transport_t *t);
void    MIDI_transport_unregister(uint8_t bit);
const midi_transport_t *MIDI_transport_get(uint8_t bit);
```

- ノートスケジューラ・`MIDI_Note_trigger()`・クリーンアップ（All Sound Off 等）は
  マスクのビットを走査して登録表から送信先を引く。決め打ちの
  `g_usb_transport` / `g_sam_transport` / `g_usbdev_transport` 分岐は無くなる
- 各トランスポート gem は初期化時に自分を登録し、返ってきたビットを Ruby の
  `transport_id` として返す。`MIDI::Device#_get_transport_mask` と
  `UI::Tombola.transport_mask_for` は `transport_id` を見るだけになり、
  クラス名の `case` 分岐は消せる
- **既存 3 トランスポートのビット値は互換のため固定で予約する**
  （USB Host 0x01 / UART 0x02 / USB Device 0x04）。スクリプトや Tombola に
  数値が直接書かれていても壊れない。AMY 以降は 0x08 から動的に割り当てる
- `MIDI_TRANSPORT_ALL`（0x03）の意味は変えない（物理出力のみ。AMY は含めない）
- 入力側（USB Host / UART を読む入力タスク）の決め打ちも同じ表に寄せるかは
  Phase 0 では必須にしない。後述の `MIDI.route` を作るときに合わせて整理する

### 他デバイスからの入力を AMY へ繋ぐ（MIDI Thru）

「USB-MIDI キーボード → AMY で発音」のような接続は、Ruby で書くと
`MIDI::Input` のコールバックで `dev.note_on` を呼ぶ形になるが、
これは Ruby VM のポーリング（`MIDI.sleep_ms` は 50 ms 刻み）に遅延が律速される。
演奏用途には遅い。

そこで picoruby-midi の C 側入力タスク（USB Host / UART を既に C で読んでいる）に
**ルーティング表**を持たせ、Ruby では宣言だけする：

```ruby
MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.amy)               # 全チャンネル
MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.amy, channel: 0)   # ch1 のみ
MIDI.unroute(MIDIDevices.usb_midi_host, MIDIDevices.amy)
```

- ルーティングは登録 API（前節案 2）の上に乗る。宛先は AMY に限らず SAM2695 や
  USB-MIDI Device でもよい（汎用の MIDI Thru）
- Ruby 側の `MIDI::Input` も引き続き同じイベントを受け取れる（ルートとは独立）
- スクリプト停止時（Supervisor のクリーンアップ）にルート表を空にする

これは picoruby-midi への機能追加なので、AMY gem 自体の範囲外として別タスクに切る。

### 停止・切り替え時の扱い

Supervisor がスクリプトを止めるときの MIDI クリーンアップ（All Sound Off / All
Notes Off）は、登録 API 経由で AMY にも届くようにする。加えて AMY gem に
`AMY.reset`（`amy_add_message("S16384Z")` 相当、全 osc リセット）を用意し、
スクリプト切り替え時に前のスクリプトのパッチ・エフェクト設定を残さない。
**AMY エンジン自体（オーディオタスク・I2S）は起動したまま**にし、
スクリプトごとに立ち上げ直さない（I2S/コーデックの再初期化はポップノイズの元）。

### SAM2695 との併用

排他にしない（決定）。どちらも `MIDIDevices` に並ぶ別のトランスポートで、
登録表の別ビットを持つだけなので、同じスクリプトから両方を鳴らせる
（例：リズムは SAM2695 の GM ドラム、リードは AMY の FM）。

### MIDI クロックとシーケンサ

**AMY 内蔵のシーケンサは使わない**（決定）。テンポの基準は Midori の
`MIDI::Clock` / `MIDI.start!` / `MIDI.bpm_loop` のみとし、AMY には
ノート・CC 等の演奏メッセージだけを送る。

- AMY の MIDI クロック追従（`external_midi_sync_mode`）は既定の「追従しない」のまま。
  `MIDI.route` で外部機器の Start / Stop / Clock が AMY に流れても無視される
- `amy_config_t.amy_external_sequencer_hook` は使わない
- Ruby API にも AMY のシーケンサ系（`sequence` / ティック指定）は出さない

---

## 4. 音源：まずは FM

### AMY の FM の構造

- DX7 互換の 6 オペレータ FM。`wave=ALGO` の osc が 1 つ（キャリア側の取りまとめ）、
  オペレータは `SINE` の osc 6 つ。アルゴリズムは DX7 の 1–32
- プリセット: **patch 128–255 が DX7**（0–127 は Juno-6、256 はピアノ）
- プログラムチェンジはバンク内のみ（DX7 バンクに設定された synth なら PC n → patch 128+n）
- オペレータの周波数は `ratio`（ALGO osc の周波数に対する比）か固定 `freq`
- オペレータの音量は自前の `amp` 係数とエンベロープ（`bp0`、DX7 型 EG あり）
- `.syx`（DX7 SysEx）は AMY の Python 側 `fm.py` で AMY パッチに変換できる。
  変換済みワイヤメッセージを SD に置いて読む運用が現実的（ESP32 上で `.syx` を
  直接解析するのは後回し）

### Ruby API（3 層）

**L1: ワイヤメッセージ / キーワード送信**（AMY の全機能への逃げ道）

```ruby
AMY.command(synth: 1, patch: 130, num_voices: 6)   # → "i1iv6K130Z"
AMY.command(synth: 1, osc: 0, filter_freq: 2000)
AMY.wire("i1K130iv6Z")                           # 生のワイヤメッセージ
```

キーワード → ワイヤ文字の変換表（`AMY::WIRE`）は AMY の `docs/api.md` の表から
FM / エンベロープ / フィルタ / エフェクト系に絞って手で起こした。synth（`i`）は
常に先頭に出す（それ以降のパラメータがその synth 宛てになるため）。

**L2: FM シンセオブジェクト**（よく触るパラメータの名前付きアクセサ）

```ruby
fm = AMY::FM.new(channel: 0, voices: 6, patch: 128)  # channel 0 = AMY synth 1
fm.patch = 133                  # DX7 プリセット切り替え
fm.algorithm = 5                # 1..32
fm.feedback = 0.4               # 0.0..1.0
fm.op(2).ratio = 3.5            # オペレータ 2 の周波数比
fm.op(2).level = 0.8            # オペレータ 2 の出力レベル
fm.op(2).envelope(attack: 10, decay: 300, sustain: 0.5, release: 400)
fm.volume = 0.7
fm.reverb = 0.3

dev = MIDI::Device.new(fm.synth)  # 演奏は MIDI::Device 経由
```

synth 宛てのコマンドは AMY 側で全ボイスに適用される（osc 番号はボイス内相対）
ので、Ruby 側でボイス数ぶんループする必要は無い。

**L3: CC マッピング**（UI と外部コントローラを同じ経路にする）

AMY は synth ごとに「CC 番号 → パラメータ」の対応表を持てる
（ワイヤの `ic`、`midi_cc`。対数カーブ・範囲指定つき）。これを Ruby から張る：

```ruby
fm.map_cc(74, :filter_freq, min: 100, max: 8000, log: true)
fm.map_cc(71, :resonance,   min: 0.7, max: 8)
fm.map_cc(20, :feedback)                       # 既定 0.0..1.0
fm.map_cc(21, :level, op: 2)                   # オペレータ指定
```

こうすると **UI は CC を送るだけ**になり、宛先が SAM2695 でも外部シンセでも
AMY でも同じスクリプトが書ける。USB-MIDI キーボードのノブも、§3 のルートを張れば
同じマッピングで効く。

### CC にマップできないパラメータ（2026-10-03 決定）

AMY の `midi_cc` の直接パラメータ指定は、バスレベルのパラメータと osc 指定の
パラメータには対応する。しかし**ノート形のパラメータ、osc 参照、ブレークポイント
（エンベロープ）はマップできない**（AMY docs/api.md の `ic` 行）。
エンベロープ時間・アルゴリズム・パッチ番号などがこれに当たる。

これらは **AMY のインスタンス（`AMY::FM` 等）に直接パラメータをセットする API**
（L2）で扱い、割り当ては Ruby 側で行う：

- 画面のノブ → `UI.knob` のブロックから直接セットする
- 外部コントローラ → `MIDI::Input#on(:control_change)` のハンドラから直接セットする

L3（CC マッピング）は「CC で動かせるものを、AMY 内部で低遅延に動かす」経路として
そのまま残す。どちらで扱うかはパラメータで決まり、スクリプトから見ると
「CC を送る」か「インスタンスにセットする」かの違いになる。

| | L3: CC マッピング | L2: 直接セット |
|---|---|---|
| 対象 | フィルタ、レゾナンス、フィードバック、オペレータレベル、音量、エフェクト量など | エンベロープ（ブレークポイント）、アルゴリズム、パッチ、比率の切り替え方など、CC 不可のもの全部（CC 可のものも直接セットはできる） |
| 経路 | CC → AMY 内部の対応表 → パラメータ | Ruby → `amy_add_message()`（ワイヤメッセージ） |
| 外部コントローラからの遅延 | `MIDI.route` を張れば C 側で完結（低遅延） | `MIDI::Input` を Ruby で処理する周期（`MIDI.sleep_ms` は 50 ms 刻み）に律速 |
| 他の音源との共通化 | 宛先を SAM2695 等に替えても同じスクリプト | AMY 専用 |

パラメータの調整は発音ほどタイミングに厳しくないので、L2 の遅延は許容範囲とする。

#### 直接セット API

名前付きアクセサ（前出の `fm.algorithm=` 等）に加えて、汎用のセッタを持つ：

```ruby
fm.set(:algorithm, 5)
fm.set(:attack, 30, op: 2)          # op: でオペレータ指定（省略時は ALGO osc / synth 全体）
fm.set(:release, 800, op: 1)
fm.get(:attack, op: 2)              # Ruby 側に持っている現在値（後述）
```

値は AMY の単位（ms、Hz、0.0〜1.0 など）。UI や MIDI の 0〜127 からの変換は
共通ヘルパで行う：

```ruby
AMY.scale(v, 1, 2000, log: true)    # 0..127 → 1..2000（対数）
AMY.scale(v, 0.0, 1.0)              # 0..127 → 0.0..1.0（線形）
```

#### エンベロープは Ruby 側に現在値を持つ

AMY のエンベロープはブレークポイント列（`bp0` = `"時間,値,時間,値,..."`）を
**まるごと送る**形式なので、「アタックだけ変える」ことができない。
そのため `AMY::FM` はオペレータごとに ADSR の現在値を Ruby 側に持ち、
1 項目が変わるたびにブレークポイント列を組み立て直して送る。

```ruby
fm.op(2).attack = 30     # → 保持している decay / sustain / release と合わせて bp0 を再送
```

- **プリセット読み込み後は ADSR の現在値が分からない**。DX7 パッチのエンベロープは
  多段（DX7 型 EG）で、ADSR の 4 値で表せないため。パッチ読み込み時に Ruby 側の
  値を既定の ADSR に戻し、最初に ADSR を 1 項目でもセットした時点で、そのオペレータの
  エンベロープは**ADSR 形に置き換わる**（元の DX7 エンベロープは失われる）。
  これは仕様として README に明記する
- synth 宛てなので全ボイスに一度に効く（ボイスごとのループは不要）
- 送信は `amy_add_message()` で Ruby VM タスク（Core 1）から行う。AMY は別スレッドからの
  投入を前提に作られている（キューロック + 描画ロック。どちらも FreeRTOS のミューテックス）。
  パッチ読み込みはわざと描画ロックの外で行い、ESP32-S3 で 50〜70 ms かかる読み込みの間も
  描画を止めない（`amy.c` のコメント）。送り手の Ruby タスクがその間待つだけ
  ノブは値の変化をノブごとに最新 1 通に潰すので、ワイヤメッセージが溢れることはない

#### 割り当て例

```ruby
fm  = AMY::FM.new(channel: 0, voices: 6, patch: 128)

# 画面のノブ → 直接セット
UI.knob(5, label: "Op2 Atk", value: 10) { |v| fm.op(2).attack  = AMY.scale(v, 1, 2000, log: true) }
UI.knob(6, label: "Op2 Rel", value: 60) { |v| fm.op(2).release = AMY.scale(v, 10, 4000, log: true) }

# 外部コントローラ（USB-MIDI Host）の CC → 直接セット
input = MIDI::Input.new(MIDI::Device.new(MIDIDevices.usb_midi_host))
input.on(:control_change) do |e|
  case e[:cc]
  when 73 then fm.op(1).attack  = AMY.scale(e[:value], 1, 2000, log: true)
  when 72 then fm.op(1).release = AMY.scale(e[:value], 10, 4000, log: true)
  when 80 then fm.algorithm     = 1 + e[:value] * 31 / 127
  end
end
```

CC 74 のように L3 でマップ済みの CC は、`MIDI.route` で AMY に流せば
このハンドラを通さずに AMY 内部で処理される。

### UI 部品との接続例

```ruby
require 'midi'
require 'amy'

fm  = AMY::FM.new(channel: 0, voices: 6, patch: 128)
dev = MIDI::Device.new(fm.synth)

fm.map_cc(74, :filter_freq, min: 100, max: 8000, log: true)
fm.map_cc(20, :feedback)

# Knobs: CC を送るだけ（他の音源と同じ書き方）
UI.knob(1, label: "Cutoff",   color: :cyan,  value: 100) { |v| dev.control_change(74, v.to_i) }
UI.knob(2, label: "Feedback", color: :green, value: 40)  { |v| dev.control_change(20, v.to_i) }
# CC にマップできないもの（アルゴリズム・エンベロープ等）はインスタンスに直接セット
UI.knob(3, label: "Algo", min: 1, max: 32, value: 5)      { |v| fm.algorithm = v.to_i }
UI.knob(4, label: "Atk",  value: 10) { |v| fm.op(1).attack = AMY.scale(v, 1, 2000, log: true) }

# Pads: trigger（Phase 0 の登録 API が前提）
UI.pad(1, label: "C3", color: :red) { dev.trigger(48, 110, duration: 300) }

# Pads でプリセット切り替え
UI.pad(6, label: "Next") { fm.patch = fm.patch + 1 }

# XYPad: device を渡すだけ
pad = UI::XYPad.new(scale: [48, 50, 52, 55, 57, 60], y_cc: 74, device: dev)
pad.show

# Tombola: C++ 側発音も登録 API 経由で AMY に届く
t = UI::Tombola.new(sides: 6, device: dev)
```

確認が要る点：

- **XYPad のグライド**：解決済み。XYPad の内蔵ハンドラはピッチベンドで実現しており、
  AMY のベンド幅（±2 半音固定）に合わせて `glide_range: 2` にする
- **Knob の更新頻度**。値の変化はノブごとに最新値 1 通に潰れるので、AMY への
  コマンド洪水にはならない想定だが、`UI.process` の周期で段差が出るかは実機で聴く
- **FM プリセット名**：解決済み。`patches.h` にはコメントとしてしか無いので、そこから
  生成した名前表を gem に持たせた（`AMY.patch_name(n)`）

実際のサンプル（[examples/amy_fm.rb](../examples/amy_fm.rb)）は上の例から次の点を変えた：
パッドは `:momentary` で `note_on` / `note_off`、プリセット切り替えはパッドではなくノブ
（ラベルにプリセット名を表示）、メインループは `loop do` ではなく `MIDI.bpm_loop`
（素の `loop` では VM がタスクを切り替えず、UI や MIDI のイベントが処理されない）。

---

## 実装フェーズ（案）

| Phase | 内容 | 完了条件 |
|-------|------|----------|
| 0 | picoruby-midi にトランスポート登録 API（案 2）。既存 3 トランスポートを固定ビットで自己登録に移行 | 既存スクリプト（pads / tombola / knobs / xypad）の挙動が変わらない |
| 1 | 音出し。`partitions.csv` 拡張 + `board_audio_*`（ES8388 / アンプ）+ gem の I2S + オーディオタスク + `amy_start` | 起動時に AMY の startup bleep が両ボードのスピーカーで鳴る |
| 2 | トランスポート。`AMY::Synth`、`MIDIDevices.amy`、`BoardConfig::HAS_AMY` | `MIDI::Device.new(MIDIDevices.amy).trigger(60)` で DX7 パッチが鳴る |
| 3 | FM API（L1/L2/L3）と examples/amy_fm.rb | Knob / Pad / XYPad / Tombola から操作できる |
| 4 | `MIDI.route`（picoruby-midi の C 側 Thru） | USB-MIDI キーボードで低遅延に演奏できる |
| 5 | 負荷・遅延の詰め（ボイス数、ブロック長、優先度 2/3、`overload_threshold`）、Supervisor 停止時の挙動 | 6 ボイス FM + reverb で過負荷フェイルセーフが作動せず、UI も操作できる |

## 決定事項（2026-10-03）

| 項目 | 決定 |
|------|------|
| 実装方式 | mrbgem `picoruby-amy`（`require 'amy'`）として切り出す。コーデック / アンプはアプリ（Midori）から注入 |
| トランスポート登録 | picoruby-midi に登録 API を足す（案 2）。Phase 0 として AMY より先に行う。案 1 は使わない |
| パーティション | 共通の `partitions.csv` で factory を 0x310000 から 0x3F0000 に。PicoRuby の固定アドレスのフラッシュ FAT（0x210000〜0x310000）を避けて後ろに置く（8 MB の Freenove にも収まる） |
| 対象ボード | Tab5 / CrowPanel。**CoreS3（と Freenove）はサポート範囲外** |
| SAM2695 との併用 | 排他にしない。両方を同時に使える |
| シーケンサ | AMY のシーケンサは使わない。テンポは `MIDI::Clock` が正 |
| CC 不可のパラメータ | AMY インスタンスへの直接セット（`fm.set` / 名前付きアクセサ）で扱い、`UI.knob` や `MIDI::Input#on` のハンドラから割り当てる。エンベロープは Ruby 側に ADSR の現在値を持つ |
| オーディオタスク | gem のタスク 1 本で描画 + I2S 書き込み。**Core 0 / 優先度 2〜3**。AMY 内部タスク（multithread / multicore）は使わない |
| ES8388 の MCLK 比 | 128 × fs / reg24 = 0x00（M5Unified と同じ） |
| CrowPanel の音声出力 | NS4168 × 2（L / R）、Philips 形式、GPIO 30 = AO3401 経由の電源スイッチ（active-low） |

## 未確定事項（実機確認が必要）

- ES8388 の MCLK 比：128 × fs / reg24 = 0x00（§2「ES8388 の MCLK 比」）。
  **Tab5 で M5Unified のレジスタ列のまま正しく鳴ることを確認済み（2026-10-04）**
- CrowPanel：回路図で確定済み（NS4168 × 2 で L / R、GPIO 30 は AO3401 経由の
  電源スイッチ、Philips 形式。§2）。実機ではパンを振った音が左右に分かれて
  出るか（L / R の割り当てが逆でないか）だけ確認する
- タスク配置は Core 0 / 優先度 2〜3 に決定済み（§2「タスクとコア配置」）。P4 での
  1 ブロックあたりの描画時間を測り、優先度・ボイス上限・`overload_threshold` を確定する
- XYPad のグライド：内蔵ハンドラはピッチベンドの全幅を `glide_range` 半音に対応させ、
  AMY のベンド幅は ±2 半音固定なので `glide_range: 2` で一致する（`examples/amy_fm.rb`）。
  AMY のピッチベンドは synth 全体に掛かるため、複数の指で同時にグライドすると互いに影響する
- DX7 プリセット名：AMY の `patches.h` にはコメントにしか無いので、そこから生成した
  `mrblib/dx7_patches.rb`（`AMY.patch_name(n)`）を gem に入れた。AMY を更新したら再生成する
- ALGO osc（osc 0）に掛けたフィルタ（`fm.filter_freq=`）が FM の出力全体に効くか（実機で聴く）
- 内部 RAM の静的使用量が約 26 KB 増えたことで、起動時に画面が出なくならないか：
  **Tab5 では問題なし（2026-10-04）**。CrowPanel は未確認
- Ruby VM と描画の分離：AMY のソースで確認済み（キューロック + 描画ロック。
  §4「直接セット API」）
