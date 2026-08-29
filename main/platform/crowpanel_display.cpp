#include "crowpanel_display.hpp"

#if defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)

#include <M5Unified.h>
#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"
#include "lgfx/v1/platforms/esp32/Light_PWM.hpp"
#include "lgfx/v1/touch/Touch_GT911.hpp"

#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "CROWPANEL";

namespace crowpanel {
namespace {

// ---------------------------------------------------------------------------
// Pins. Taken from Elecrow's V1.2 board support code
// (example/V1.2/idf-code/*/peripheral/bsp_*), not from the wiki tables, which
// list the MIPI-DSI pads as if they were GPIOs.
// ---------------------------------------------------------------------------
constexpr int PIN_BACKLIGHT = 31;   // LEDC PWM, active high
constexpr int PIN_TOUCH_SCL = 46;
constexpr int PIN_TOUCH_SDA = 45;
constexpr int PIN_TOUCH_INT = 42;
constexpr int PIN_TOUCH_RST = 40;
constexpr int TOUCH_I2C_PORT = 0;

constexpr int PANEL_WIDTH  = 1024;
constexpr int PANEL_HEIGHT = 600;

// The GT911 latches its I2C address from the INT pin at the end of reset:
// low picks 0x5D, high picks 0x14. Elecrow's code leaves it low, so that is
// the address this board comes up at.
constexpr uint8_t TOUCH_I2C_ADDR = 0x5D;

// The MIPI D-PHY runs off LDO3 (Bus_DSI acquires that one itself). LDO4 feeds
// the 3.3V rail the touch panel and its I2C pull-ups hang off; without it the
// GT911 never answers.
constexpr int LDO_TOUCH_CHAN_ID    = 4;
constexpr int LDO_TOUCH_VOLTAGE_MV = 3300;

// ---------------------------------------------------------------------------
// EK79007 (1024x600 MIPI-DSI)
// ---------------------------------------------------------------------------

// Command sequence transcribed from Espressif's esp_lcd_ek79007 driver, which
// is what Elecrow's own examples run. Panel_DSI sends MADCTL and COLMOD after
// the last list, so neither appears here.
struct Panel_EK79007 : public lgfx::Panel_DSI
{
protected:
    static constexpr uint8_t CMD_SWRESET     = 0x01;
    static constexpr uint8_t CMD_PAD_CONTROL = 0xB2;
    static constexpr uint8_t DSI_2_LANE      = 0x10;
    static constexpr uint8_t DSI_4_LANE      = 0x00;

    const uint8_t* getInitParams(size_t listno) const override
    {
        static constexpr uint8_t list0[] =
        {//len(cmd+params), cmd, params
            1, CMD_SWRESET,
            0, // end
        };

        static constexpr uint8_t list1[] =
        {
            2, CMD_PAD_CONTROL, DSI_2_LANE,
            2, 0x80, 0x8B,
            2, 0x81, 0x78,
            2, 0x82, 0x84,
            2, 0x83, 0x88,
            2, 0x84, 0xA8,
            2, 0x85, 0xE3,
            2, 0x86, 0x88,
            0, // end
        };

        static constexpr uint8_t list1_lane4[] =
        {
            2, CMD_PAD_CONTROL, DSI_4_LANE,
            2, 0x80, 0x8B,
            2, 0x81, 0x78,
            2, 0x82, 0x84,
            2, 0x83, 0x88,
            2, 0x84, 0xA8,
            2, 0x85, 0xE3,
            2, 0x86, 0x88,
            0, // end
        };

        static constexpr uint8_t list2[] =
        {
            1, CMD_SLPOUT,
            0, // end
        };

        switch (listno)
        {
        case 0: return list0;
        case 1:
        {
            auto bus = getBusDSI();
            return (bus && bus->config().lane_num == 4) ? list1_lane4 : list1;
        }
        case 2: return list2;
        default: return nullptr;
        }
    }

    size_t getInitDelay(size_t listno) const override
    {
        switch (listno)
        {
        case 0: return 20;   // after SWRESET
        case 2: return 120;  // after SLPOUT
        default: return 0;
        }
    }
};

lgfx::Bus_DSI      s_bus;
Panel_EK79007      s_panel;
lgfx::Touch_GT911  s_touch;
lgfx::Light_PWM    s_light;
esp_ldo_channel_handle_t s_touch_ldo = nullptr;
bool s_panel_ready = false;

// Reset the GT911 ourselves rather than leaving it to Touch_GT911::init().
// The driver's own reset releases the line and starts probing 1ms later,
// which is well inside the ~50ms the controller needs before it answers; it
// only recovers by retrying, and each retry also flips the address it tries.
// Doing it here with the datasheet timing means the very first probe lands,
// on the address the INT level selected.
void reset_touch(void)
{
    lgfx::pinMode(PIN_TOUCH_INT, lgfx::pin_mode_t::output);
    lgfx::gpio_lo(PIN_TOUCH_INT);          // INT low during reset -> addr 0x5D
    lgfx::pinMode(PIN_TOUCH_RST, lgfx::pin_mode_t::output);
    lgfx::gpio_lo(PIN_TOUCH_RST);
    vTaskDelay(pdMS_TO_TICKS(10));
    lgfx::gpio_hi(PIN_TOUCH_RST);
    vTaskDelay(pdMS_TO_TICKS(10));          // INT must stay low across this
    lgfx::pinMode(PIN_TOUCH_INT, lgfx::pin_mode_t::input);
    vTaskDelay(pdMS_TO_TICKS(50));          // controller boots its firmware
}

}  // namespace

esp_err_t display_init(void)
{
    if (s_panel_ready) {
        return ESP_OK;
    }

    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = LDO_TOUCH_CHAN_ID;
    ldo_cfg.voltage_mv = LDO_TOUCH_VOLTAGE_MV;
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &s_touch_ldo);
    if (err != ESP_OK) {
        // Not fatal for the display itself, so carry on and let the touch
        // probe be the thing that complains.
        ESP_LOGW(TAG, "LDO%d (touch 3.3V) not available: %s",
                 LDO_TOUCH_CHAN_ID, esp_err_to_name(err));
        s_touch_ldo = nullptr;
    }

    {
        auto cfg = s_bus.config();
        cfg.bus_id = 0;
        cfg.lane_num = 2;
        cfg.lane_mbps = 900;
        cfg.ldo_chan_id = 3;          // MIPI D-PHY supply
        cfg.ldo_voltage_mv = 2500;
        s_bus.config(cfg);
    }
    if (!s_bus.init()) {
        ESP_LOGE(TAG, "MIPI-DSI bus init failed");
        return ESP_FAIL;
    }

    {
        auto cfg = s_panel.config();
        cfg.memory_width  = PANEL_WIDTH;
        cfg.memory_height = PANEL_HEIGHT;
        cfg.panel_width   = PANEL_WIDTH;
        cfg.panel_height  = PANEL_HEIGHT;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.readable = true;
        cfg.rgb_order = true;
        cfg.bus_shared = false;
        // The panel's reset line is handled by the board's power-on circuit;
        // Elecrow's driver config passes -1 for it too.
        cfg.pin_cs  = -1;
        cfg.pin_rst = -1;
        s_panel.config(cfg);
    }

    {
        // Video timing for the 1024x600 panel, from Elecrow's bsp_illuminate.c.
        auto det = s_panel.config_detail();
        det.dpi_freq_mhz = 51;
        det.hsync_back_porch  = 160;
        det.hsync_pulse_width = 70;
        det.hsync_front_porch = 160;
        det.vsync_back_porch  = 23;
        det.vsync_pulse_width = 10;
        det.vsync_front_porch = 12;
        s_panel.config_detail(det);
    }

    {
        auto cfg = s_light.config();
        cfg.pin_bl = PIN_BACKLIGHT;
        cfg.freq = 30000;
        cfg.pwm_channel = 7;
        cfg.invert = false;
        s_light.config(cfg);
    }

    reset_touch();
    {
        auto cfg = s_touch.config();
        cfg.i2c_port = TOUCH_I2C_PORT;
        cfg.i2c_addr = TOUCH_I2C_ADDR;
        cfg.pin_sda = PIN_TOUCH_SDA;
        cfg.pin_scl = PIN_TOUCH_SCL;
        cfg.pin_int = PIN_TOUCH_INT;
        cfg.pin_rst = -1;             // already done, with the right timing
        cfg.freq = 400000;
        cfg.x_min = 0;
        cfg.x_max = PANEL_WIDTH - 1;
        cfg.y_min = 0;
        cfg.y_max = PANEL_HEIGHT - 1;
        cfg.bus_shared = false;
        cfg.offset_rotation = 0;
        s_touch.config(cfg);
    }

    // Probe now rather than leaving it to Panel_Device::initTouch(), which
    // throws the result away. Touch_GT911::init() is idempotent, so the later
    // call from LGFX_Device::init_impl just returns true.
    if (!s_touch.init()) {
        ESP_LOGW(TAG, "GT911 not responding on I2C%d (SDA %d / SCL %d) - "
                      "the UI will draw but not accept touch",
                 TOUCH_I2C_PORT, PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    }

    s_panel.setBus(&s_bus);
    s_panel.setLight(&s_light);
    s_panel.setTouch(&s_touch);

    // Installs the panel and runs LGFX_Device::init_impl on it, skipping the
    // autodetection that would otherwise find no M5Stack hardware here.
    if (!M5.Lcd.init(&s_panel)) {
        ESP_LOGE(TAG, "Panel init failed");
        return ESP_FAIL;
    }

    // Native landscape; unlike the Tab5 there is no portrait framebuffer to
    // rotate out of.
    M5.Lcd.setRotation(0);

    // M5.begin() is never called on this board, so nothing else has told
    // M5Unified where to read touch points from.
    M5.Touch.begin(&M5.Display);

    s_panel_ready = true;
    ESP_LOGI(TAG, "CrowPanel display ready: EK79007 %dx%d, GT911 touch",
             PANEL_WIDTH, PANEL_HEIGHT);
    return ESP_OK;
}

lgfx::Panel_DSI* dsi_panel(void)
{
    return s_panel_ready ? &s_panel : nullptr;
}

void touch_update(void)
{
    if (s_panel_ready) {
        M5.Touch.update(m5gfx::millis());
    }
}

}  // namespace crowpanel

#endif // CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL
