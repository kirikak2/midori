/*
 * Board audio power-up for the AMY synth (picoruby-amy)
 *
 * The gem drives the I2S output; what sits behind it is board-specific and
 * lives here, handed to the gem as its power callback:
 *
 *   Tab5      I2S0 -> ES8388 codec -> speaker amplifier. The codec is set
 *             up over the internal I2C bus (M5.In_I2C, address 0x10) and
 *             the amplifier is SPK_EN, bit 1 of the PI4IOE5V6408 IO
 *             expander at 0x43 (M5.begin() already made it an output, low).
 *   CrowPanel I2S -> two NS4168 I2S amplifiers. No codec. GPIO 30 switches
 *             their supply through a P-MOSFET (AO3401): low = on.
 *
 * The callback runs on the gem's audio task, once the I2S clocks (and on
 * Tab5 MCLK) are running. See docs/AMY_SYNTH.md.
 */

#include "board_audio.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "amy_gem.h"

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
#include <M5Unified.h>
#elif defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
#include "driver/gpio.h"
#endif

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)

static const char *TAG = "board_audio";

static constexpr uint8_t ES8388_ADDR = 0x10;
static constexpr uint8_t PI4IO1_ADDR = 0x43;
static constexpr uint8_t PI4IO_OUT_SET = 0x05;
static constexpr uint8_t SPK_EN_BIT = 1 << 1;
static constexpr uint32_t I2C_FREQ = 400000;

/* ES8388 as DAC-only, I2S slave, 16-bit standard I2S, MCLK = 128 x fs.
 * This is M5Unified's sequence for the Tab5 speaker
 * (_speaker_enabled_cb_tab5), unchanged: the gem is configured with
 * AMY_GEM_MCLK_MULTIPLE = 128 to match reg 24 = 0x00. */
static constexpr uint8_t es8388_enable[][2] = {
    {  0, 0x80 },   // RESET / CSM power on
    {  0, 0x00 },
    {  0, 0x00 },
    {  0, 0x0E },
    {  1, 0x00 },
    {  2, 0x0A },   // CHIP POWER: power up all
    {  3, 0xFF },   // ADC POWER: power down all
    {  4, 0x3C },   // DAC POWER: power up, LOUT1/ROUT1/LOUT2/ROUT2 enabled
    {  5, 0x00 },   // ChipLowPower1
    {  6, 0x00 },   // ChipLowPower2
    {  7, 0x7C },   // VSEL
    {  8, 0x00 },   // I2S slave mode
    { 23, 0x18 },   // I2S format, 16 bit
    { 24, 0x00 },   // DACFsRatio: MCLK / LRCK = 128 (single speed)
    { 25, 0x20 },   // DAC unmute
    { 26, 0x00 },   // LDACVOL
    { 27, 0x00 },   // RDACVOL
    { 28, 0x08 },   // digital click-free power up / down
    { 29, 0x00 },
    { 38, 0x00 },   // DAC CTRL16
    { 39, 0xB8 },   // left channel mixer
    { 42, 0xB8 },   // right channel mixer
    { 43, 0x08 },   // ADC and DAC separate
    { 45, 0x00 },   // 1.5k VREF analog output
    { 46, 0x21 },
    { 47, 0x21 },
    { 48, 0x21 },
    { 49, 0x21 },
};

static void power(bool on, void *arg)
{
    (void)arg;
    if (on) {
        for (const auto &rv : es8388_enable) {
            if (!M5.In_I2C.writeRegister8(ES8388_ADDR, rv[0], rv[1], I2C_FREQ)) {
                ESP_LOGE(TAG, "ES8388 reg %u write failed", rv[0]);
                return;
            }
        }
        M5.In_I2C.bitOn(PI4IO1_ADDR, PI4IO_OUT_SET, SPK_EN_BIT, I2C_FREQ);
    } else {
        M5.In_I2C.bitOff(PI4IO1_ADDR, PI4IO_OUT_SET, SPK_EN_BIT, I2C_FREQ);
        M5.In_I2C.writeRegister8(ES8388_ADDR, 8, 0x00, I2C_FREQ);
    }
}

void board_audio_init(void)
{
    AMY_GEM_set_power_callback(power, nullptr);
}

#elif defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)

static constexpr gpio_num_t AUDIO_CTRL = GPIO_NUM_30;   // low = amplifiers powered

static void power(bool on, void *arg)
{
    (void)arg;
    gpio_set_level(AUDIO_CTRL, on ? 0 : 1);
}

void board_audio_init(void)
{
    // Keep the amplifiers off until AMY starts clocking I2S, so there is
    // no pop from a floating data line at boot.
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << AUDIO_CTRL;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_set_level(AUDIO_CTRL, 1);
    gpio_config(&cfg);
    gpio_set_level(AUDIO_CTRL, 1);

    AMY_GEM_set_power_callback(power, nullptr);
}

#else

void board_audio_init(void)
{
}

#endif
