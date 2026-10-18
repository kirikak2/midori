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
 *
 * Master volume (set from the Settings screen, kept in NVS):
 *
 *   Tab5      ES8388 analog output volume. OUT1 (LOUT1/ROUT1) feeds the
 *             speaker amplifier, and the headphone jack hangs off the codec
 *             output too: powering OUT1 down silences the earphones, so
 *             the speaker is muted only by SPK_EN. Which of OUT1 / OUT2 the
 *             jack is on is not documented, so both are driven at the
 *             volume of whichever output is live -- the headphone volume
 *             while HP_DET (bit 7 of the 0x43 expander, polled from the UI
 *             task) says they are in, the speaker volume otherwise.
 *   CrowPanel Speaker only: the NS4168s have no volume, so the gem scales
 *             the samples (AMY_GEM_set_output_gain()).
 */

#include "board_audio.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "amy_gem.h"

#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
#include <M5Unified.h>
#elif defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
#include "driver/gpio.h"
#endif

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5) || defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)

static const char *TAG = "board_audio";

extern "C" void initialize_nvs(void);   // midori-runtime (picoruby-esp32.c)

static constexpr const char *NVS_NAMESPACE = "audio";
static constexpr const char *NVS_KEYS[BOARD_AUDIO_OUTPUT_COUNT] = { "spk_vol", "hp_vol" };

// 100 % on the speaker is what the board played at before there was a
// volume setting. Headphones start lower.
static uint8_t s_volume[BOARD_AUDIO_OUTPUT_COUNT] = { 100, 70 };
static uint8_t s_saved[BOARD_AUDIO_OUTPUT_COUNT] = { 100, 70 };

// Guards the volumes and the codec / amplifier state: the UI task (Settings,
// headphone polling) and the gem's audio task (power callback) both touch them.
static SemaphoreHandle_t s_lock = nullptr;
static bool s_powered = false;

struct Lock {
    Lock() { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { if (s_lock) xSemaphoreGive(s_lock); }
};

static void volume_load(void)
{
    initialize_nvs();
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;  // nothing saved yet
    for (int i = 0; i < BOARD_AUDIO_OUTPUT_COUNT; i++) {
        uint8_t v;
        if (nvs_get_u8(h, NVS_KEYS[i], &v) == ESP_OK && v <= 100) {
            s_volume[i] = s_saved[i] = v;
        }
    }
    nvs_close(h);
}

static void apply_locked(void);

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)

static constexpr uint8_t ES8388_ADDR = 0x10;
static constexpr uint8_t PI4IO1_ADDR = 0x43;
static constexpr uint8_t PI4IO_OUT_SET = 0x05;
static constexpr uint8_t PI4IO_IN_STA = 0x0F;
static constexpr uint8_t SPK_EN_BIT = 1 << 1;
static constexpr uint8_t HP_DET_BIT = 1 << 7;   // input (M5GFX sets the direction); high = plugged in
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
    { 46, 0x21 },   // LOUT1VOL .. ROUT2VOL: rewritten by apply_locked()
    { 47, 0x21 },
    { 48, 0x21 },
    { 49, 0x21 },
};

// ES8388 output volume registers (0x00 = -45 dB .. 0x1E = 0 dB .. 0x21 = +4.5 dB)
static constexpr uint8_t REG_DACPOWER = 4;
static constexpr uint8_t DACPOWER_OUTS = 0x3C;    // LOUT1 | ROUT1 | LOUT2 | ROUT2
static constexpr uint8_t REG_OUTVOL_FIRST = 46;   // 46..49: LOUT1, ROUT1, LOUT2, ROUT2
static constexpr uint8_t OUTVOL_MAX = 0x21;

static bool s_hp_inserted = false;
static bool s_hp_pending = false;       // last raw read, for debouncing
static TickType_t s_hp_last_poll = 0;

static bool read_hp_det(bool *inserted)
{
    uint8_t v;
    if (!M5.In_I2C.readRegister(PI4IO1_ADDR, PI4IO_IN_STA, &v, 1, I2C_FREQ)) return false;
    *inserted = (v & HP_DET_BIT) != 0;
    return true;
}

// 1-100 % -> 1.5 dB steps from -43.5 dB to +4.5 dB
static uint8_t outvol(uint8_t percent)
{
    if (percent == 0) return 0;
    return 1 + (percent - 1) * (OUTVOL_MAX - 1) / 99;
}

static void apply_locked(void)
{
    if (!s_powered) return;
    // The speaker and the jack share the codec outputs: set them to the
    // volume of the output that is live, and gate the speaker with its amp.
    uint8_t vol = s_volume[s_hp_inserted ? BOARD_AUDIO_HEADPHONE : BOARD_AUDIO_SPEAKER];
    bool spk_on = !s_hp_inserted && vol > 0;

    if (!spk_on) M5.In_I2C.bitOff(PI4IO1_ADDR, PI4IO_OUT_SET, SPK_EN_BIT, I2C_FREQ);
    for (uint8_t reg = REG_OUTVOL_FIRST; reg < REG_OUTVOL_FIRST + 4; reg++) {
        M5.In_I2C.writeRegister8(ES8388_ADDR, reg, outvol(vol), I2C_FREQ);
    }
    M5.In_I2C.writeRegister8(ES8388_ADDR, REG_DACPOWER, vol > 0 ? DACPOWER_OUTS : 0, I2C_FREQ);
    if (spk_on) M5.In_I2C.bitOn(PI4IO1_ADDR, PI4IO_OUT_SET, SPK_EN_BIT, I2C_FREQ);
}

static void power(bool on, void *arg)
{
    (void)arg;
    Lock lock;
    if (on) {
        for (const auto &rv : es8388_enable) {
            if (!M5.In_I2C.writeRegister8(ES8388_ADDR, rv[0], rv[1], I2C_FREQ)) {
                ESP_LOGE(TAG, "ES8388 reg %u write failed", rv[0]);
                return;
            }
        }
        s_powered = true;
        apply_locked();     // volumes, output enables, SPK_EN
    } else {
        s_powered = false;
        M5.In_I2C.bitOff(PI4IO1_ADDR, PI4IO_OUT_SET, SPK_EN_BIT, I2C_FREQ);
        M5.In_I2C.writeRegister8(ES8388_ADDR, 8, 0x00, I2C_FREQ);
    }
}

void board_audio_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    volume_load();
    bool inserted;
    if (read_hp_det(&inserted)) {
        s_hp_inserted = s_hp_pending = inserted;
    }
    ESP_LOGI(TAG, "volume: speaker %u%%, headphones %u%% (headphones %s)",
             s_volume[BOARD_AUDIO_SPEAKER], s_volume[BOARD_AUDIO_HEADPHONE],
             s_hp_inserted ? "in" : "out");
    AMY_GEM_set_power_callback(power, nullptr);
}

void board_audio_update(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_hp_last_poll < pdMS_TO_TICKS(100)) return;
    s_hp_last_poll = now;

    bool inserted;
    if (!read_hp_det(&inserted)) return;
    // Two equal reads in a row, so a bouncing jack contact does not toggle
    // the amplifier.
    bool stable = inserted == s_hp_pending;
    s_hp_pending = inserted;
    if (!stable || inserted == s_hp_inserted) return;

    Lock lock;
    s_hp_inserted = inserted;
    apply_locked();
    ESP_LOGI(TAG, "headphones %s: speaker %s", inserted ? "in" : "out",
             inserted ? "muted" : "on");
}

bool board_audio_has_output(board_audio_output_t out)
{
    return out == BOARD_AUDIO_SPEAKER || out == BOARD_AUDIO_HEADPHONE;
}

bool board_audio_headphone_inserted(void)
{
    return s_hp_inserted;
}

#elif defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)

static constexpr gpio_num_t AUDIO_CTRL = GPIO_NUM_30;   // low = amplifiers powered

// Same curve as the Tab5's codec: 1.5 dB per 3 %, -43.5 dB at 1 %, 0 dB at 100 %
static float gain(uint8_t percent)
{
    if (percent == 0) return 0.0f;
    return powf(10.0f, -48.0f * (100 - percent) / 99.0f / 20.0f);
}

static void apply_locked(void)
{
    uint8_t spk = s_volume[BOARD_AUDIO_SPEAKER];
    AMY_GEM_set_output_gain(gain(spk));
    if (s_powered) gpio_set_level(AUDIO_CTRL, spk > 0 ? 0 : 1);
}

static void power(bool on, void *arg)
{
    (void)arg;
    Lock lock;
    s_powered = on;
    if (on) {
        apply_locked();
    } else {
        gpio_set_level(AUDIO_CTRL, 1);
    }
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

    s_lock = xSemaphoreCreateMutex();
    volume_load();
    apply_locked();
    ESP_LOGI(TAG, "volume: speaker %u%%", s_volume[BOARD_AUDIO_SPEAKER]);
    AMY_GEM_set_power_callback(power, nullptr);
}

void board_audio_update(void)
{
}

bool board_audio_has_output(board_audio_output_t out)
{
    return out == BOARD_AUDIO_SPEAKER;
}

bool board_audio_headphone_inserted(void)
{
    return false;
}

#endif

void board_audio_set_volume(board_audio_output_t out, uint8_t percent)
{
    if (!board_audio_has_output(out)) return;
    if (percent > 100) percent = 100;
    Lock lock;
    if (s_volume[out] == percent) return;
    s_volume[out] = percent;
    apply_locked();
}

uint8_t board_audio_get_volume(board_audio_output_t out)
{
    return board_audio_has_output(out) ? s_volume[out] : 0;
}

void board_audio_save(void)
{
    nvs_handle_t h;
    bool changed = false;
    for (int i = 0; i < BOARD_AUDIO_OUTPUT_COUNT; i++) {
        if (s_volume[i] != s_saved[i]) changed = true;
    }
    if (!changed) return;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed; volume not saved");
        return;
    }
    for (int i = 0; i < BOARD_AUDIO_OUTPUT_COUNT; i++) {
        uint8_t v = s_volume[i];
        if (nvs_set_u8(h, NVS_KEYS[i], v) == ESP_OK) s_saved[i] = v;
    }
    nvs_commit(h);
    nvs_close(h);
}

#else

void board_audio_init(void) {}
void board_audio_update(void) {}
bool board_audio_has_output(board_audio_output_t out) { (void)out; return false; }
void board_audio_set_volume(board_audio_output_t out, uint8_t percent) { (void)out; (void)percent; }
uint8_t board_audio_get_volume(board_audio_output_t out) { (void)out; return 0; }
void board_audio_save(void) {}
bool board_audio_headphone_inserted(void) { return false; }

#endif
