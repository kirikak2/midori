# Elecrow CrowPanel Advanced 7inch (ESP32-P4) 対応

[製品 Wiki](https://www.elecrow.com/wiki/CrowPanel_Advanced_7inch_ESP32-P4_HMI_AI_Display_1024x600_IPS_Touch_Screen_with_WiFi6_Compatible_with_ArduinoLVGL.html) /
[Elecrow のサンプル・回路図リポジトリ](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen)

ESP32-P4 + 1024x600 MIPI-DSI (EK79007) + GT911 タッチの HMI ボード。
Tab5 と同じ ESP32-P4NRW32（32MB PSRAM / 16MB Flash / チップリビジョン v1.3）なので、
SoC まわりの設定は Tab5 とほぼ同じ。違うのは**パネル・タッチ・USB・SD** の 4 点。

```bash
./switch_board.sh crowpanel            # 既定は host（USB-MIDI ホスト）
./switch_board.sh crowpanel midi_device
source ~/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build flash monitor -p /dev/ttyUSB0
```

## USB（重要）

USB-C が 2 つある：

| コネクタ | 中身 | 用途 |
|---------|------|------|
| `UART` | CH343 USB-UART ブリッジ → UART0 (GPIO 37/38) | **常に**書き込み。`host` モードではコンソールもここ |
| `USB 2.0` | ESP32-P4 の **高速 (HS / OTG2.0)** ポート | USB-MIDI ホスト / デバイス。`midi_device` モードではコンソール(CDC)もここ |

ESP32-P4 の USB-Serial/JTAG はどのコネクタにも出ていない。したがって
`serial` モードは**使えない**（`switch_board.sh` が拒否する）。コンソールが
何処にも繋がっていないビルドになり、「起動しない」ようにしか見えないため。

### "USB 2.0" は HS ポート。FS ポートではない（2026-08-29）

ESP32-P4 には USB デバイスになれるポートが 2 つある：

| ポート | esp_tinyusb | PHY | P4 での位置づけ |
|-------|-------------|-----|----------------|
| OTG 1.1 | `TINYUSB_PORT_FULL_SPEED_0` | 内蔵 FSLS PHY（USB-Serial/JTAG と mux 共有） | Tab5 の USB-C はこちら |
| OTG 2.0 | `TINYUSB_PORT_HIGH_SPEED_0` | 専用 UTMI PHY | **CrowPanel の "USB 2.0" はこちら** |

`picoruby-usb_midi_device` は `TINYUSB_PORT_FULL_SPEED_0` 決め打ちだったので、
CrowPanel では**繋がっていないパッドを叩いていた**。`tinyusb_driver_install()` は
成功しログも出るのに、ホストからはデバイスが一切見えない。

根拠：Elecrow の USB HID サンプル（Lesson06）は esp_tinyusb のポート既定値を
使っており、[tinyusb_default_config.h](../managed_components/espressif__esp_tinyusb/include/tinyusb_default_config.h)
に *"The default port is `TINYUSB_PORT_HIGH_SPEED_0` on ESP32-P4"* と書かれている。

gem 側にポートの選択肢を追加した（`USB_MIDI_DEVICE_HIGH_SPEED`、既定 0）。
CrowPanel だけ `components/picoruby-esp32/CMakeLists.txt` から 1 を注入する。
これに伴い gem 側で必要になったもの：

- **HS 用コンフィグレーションディスクリプタ**。バルクエンドポイントのサイズは
  USB 規格で速度ごとに決まっている（FS 64 byte / HS 512 byte）ので、FS 用の
  ディスクリプタをそのまま HS で使うことはできない。両方を持ち、ホストからの
  OTHER_SPEED_CONFIGURATION 要求に答える。
- **Device Qualifier ディスクリプタ**。「反対の速度でも動作できる」ことを
  宣言するもので、HS 対応デバイスには必須。
- **P4 の FSLS PHY mux 切り替えを止める**。`USB_MIDI_DEVICE_P4_PHY_SWAP` は
  Tab5 のように FS パッドへ配線されたボード用で、HS ポートは専用 PHY を持つため
  無関係。やっても USB-Serial/JTAG を無駄に切り離すだけなので、HS 選択時は
  既定で 0 になる。

> **2026-08-28 の実例**: Tab5 用ビルドを CrowPanel に焼くと、ログが
> `I (1998) TinyUSB: TinyUSB Driver installed on port 0` で止まり、以降無音になる。
> ハングではなく、その直後の `tinyusb_console_init()` で stdout が
> 誰も見ていない USB-C に移っただけ。加えて M5GFX のボード自動判別が
> M5Stack のハードを探して失敗するため、画面も出ない。ポートも間違っていたので、
> その USB-C に繋いでも CDC は現れなかった。

### コンソールの行き先

Kconfig の派生シンボル `CONFIG_USB_MIDI_CONSOLE_ON_CDC`（`midi_device` モードで y）
が決める。`console_input.c` と `picoruby-usb_midi_device` への
`USB_MIDI_DEVICE_WITH_CDC` 注入がこれを見る。

- `host`（既定）… コンソールは UART（CH343）。`idf.py monitor -p /dev/ttyUSB0`
- `midi_device` … コンソールは USB 2.0 側の CDC。`idf.py monitor -p /dev/ttyACM0`
  など。**書き込みは変わらず CH343 経由**なので、他のボードと違って
  ダウンロードモードに入る操作は不要。

UART に固定したいときは `host` モードでビルドする。

## ディスプレイ

M5GFX はこのボードを自動判別できない（M5Stack のハードを I2C で探しに行くだけ）。
そこで [main/platform/crowpanel_display.cpp](../main/platform/crowpanel_display.cpp) が
LovyanGFX のパーツを自分で組み立て、`M5GFX::init(Panel_Device*)` で `M5.Lcd` に差し込む。
このメソッドは自動判別を丸ごと飛ばすので、**main/ui 以下は一切変更なしで動く**
（CoreS3 / Tab5 と同じく `M5.Lcd` / `M5.Touch` を叩く）。

`M5.begin()` は**呼ばない**。このボードに無い PMIC / IMU / IO エキスパンダを
探しに行くだけだから。M5Unified から使うのはタッチだけなので
`M5.Touch.begin(&M5.Display)` を手で呼び、更新も `M5.update()` ではなく
`M5.Touch.update()` を直接叩く。

| 要素 | 値 | 出典 |
|------|-----|------|
| パネル | EK79007, MIPI-DSI 2 lane, 900 Mbps/lane | `bsp_illuminate.c` |
| 解像度 | 1024x600, RGB565, 回転なし（ネイティブ横） | 同上 |
| DPI クロック | 51 MHz | 同上 |
| HSYNC | BP 160 / PW 70 / FP 160 | 同上 |
| VSYNC | BP 23 / PW 10 / FP 12 | 同上 |
| MIPI D-PHY 電源 | LDO3 = 2.5V（`Bus_DSI` が自分で取得） | 同上 |
| タッチ 3.3V 電源 | LDO4 = 3.3V | Elecrow の Arduino サンプル |
| バックライト | GPIO 31, LEDC PWM 30kHz, active high | `bsp_illuminate.c` |
| パネル RESET | 未使用（`reset_gpio_num = -1`）。基板の POR に任せる | 同上 |

パネル初期化コマンド列（`Panel_EK79007::getInitParams`）は Espressif の
`esp_lcd_ek79007` から写した：SWRESET → `0xB2 0x10`（2 lane）→ `0x80`〜`0x86` →
SLPOUT + 120ms。DISPON (0x29) はベンダドライバも送っていないので送らない。

### 描画後の `display()` は Tab5 と同じく必須

`Panel_DSI` はフレームバッファパネルなので、
[CLAUDE.md の「Tab5 の描画はキャッシュに残る」](../CLAUDE.md) がそのまま当てはまる。
`UIManager::update()` の末尾で `M5.Lcd.display()` を呼んでいるので通常は意識不要。

### PPA（ピクセルアクセラレータ）

`ui_ppa` は Tombola のスプライト転送に P4 の PPA を使う。パネルが `Panel_DSI` か
どうかを型で問い合わせる手段が LovyanGFX に無いので、ボードごとに答える形にした
（CrowPanel は `crowpanel::dsi_panel()` を返す）。

## タッチ (GT911)

| 信号 | GPIO |
|------|------|
| I2C SCL | 46 |
| I2C SDA | 45 |
| INT | 42 |
| RST | 40 |

I2C アドレスはリセット解除時の INT レベルで決まる（Low → 0x5D / High → 0x14）。
Elecrow のコードは Low なので **0x5D** で決め打ちしている。

リセットは `Touch_GT911::init()` 任せにせず自分でやる。LovyanGFX の実装は
RST を離した 1ms 後から探り始めて、失敗するたびにアドレスを 0x14 ↔ 0x5D と
入れ替えながら 6 回リトライする作りで、GT911 がファームを起動し終える
~50ms を待たない。データシート通りのタイミングで自分でリセットしてから
`pin_rst = -1` で渡すと、1 回目のプローブで当たる。

## SD カード

TF スロットは SDMMC 配線（CS は GND 直結なので SPI の CS は無い）。1-bit で使う。

| 信号 | GPIO |
|------|------|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |

### スロット 0 でなければ認識しない（2026-08-28）

**この 3 本は ESP32-P4 の SDMMC スロット 0 専用 IOMUX ピンそのもの**
（[soc/sdmmc_pins.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32p4/include/soc/sdmmc_pins.h)）:

```
#define SDMMC_SLOT0_IOMUX_PIN_NUM_CLK  43
#define SDMMC_SLOT0_IOMUX_PIN_NUM_CMD  44
#define SDMMC_SLOT0_IOMUX_PIN_NUM_D0   39
// SLOT1 doesn't go through IOMUX
```

そして P4 のスロット 0 は GPIO マトリクスを通せない
（`SDMMC_LL_SLOT_SUPPORT_GPIO_MATRIX(0) == 0`）。つまり**このスロットに繋がった
カードはスロット 0 からしか触れない**。ピン番号だけ合わせてもスロットが違えば
カードは見つからない。Elecrow の BSP も `host.slot = SDMMC_HOST_SLOT_0` にして
バスクロックを 10MHz に抑えている。

`picoruby-filesystem-fat` の ESP32 ポートは `SDMMC_HOST_SLOT_1` 決め打ちだったので、
スロットとバスクロックをボード設定から渡せるようにした：

```
BoardConfig::SD_SLOT / SD_FREQ_KHZ   (CMakeLists.txt で設定)
  -> SDMMC.new(slot:, freq_khz:)     (picoruby-sdmmc)
  -> FAT.init_sdmmc(clk, cmd, d0, slot, freq_khz)
  -> FAT_set_sdmmc_pins()            (sd_disk.c)
```

CrowPanel は `SD_SLOT 0` / `SD_FREQ_KHZ 10000`。他のボードは `-1`（従来どおり）で、
その場合ポート側が「ピンがスロット 0 の IOMUX ピンと一致するならスロット 0、
それ以外はスロット 1」と判断する（`sdmmc_slot_from_pins()`）。ESP32-S3 には
`soc/sdmmc_pins.h` 自体が無いので Freenove は従来どおりスロット 1。

## SAM2695 (UART MIDI)

UART1 のヘッダ（3.3V）に出す想定で **TX = GPIO 47 / RX = GPIO 48**。
GPIO 45/46 はタッチ I2C、43/44 は SD なので使えない。

## 未確認・未対応

- **USB-MIDI ホスト**（`host` モード）: 未検証。ESP-IDF の USB Host ライブラリは
  P4 では HS ポートを使うので、デバイスモードと同じ "USB 2.0" コネクタになる
  はず。つまりこのボードでは S3 と同様にホストとデバイスが排他になる
  （board_config の `HAS_USB_MIDI_HOST` は既にそう導出している）。
  VBUS を供給できるかは基板次第で、外部給電が要るかもしれない。
- **オンボード ESP32-C6 (Wi-Fi 6)**: 未対応。SDIO のピン配置は基板リビジョンで
  変わっている（V1.1 で D0-D3 が IO14,15,16,17 → IO17,16,15,14）。
- **オーディオ (I2S スピーカ / PDM マイク)、カメラ、バッテリ残量**: 未対応。
- `main/sd_card/sd_card.c` は現在どのボードでもビルド対象外（SD は PicoRuby 側の
  VFS が担当）。そのため CrowPanel 用のピン定義も足していない。
