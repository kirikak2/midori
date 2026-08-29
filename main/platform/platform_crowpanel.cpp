// Platform bring-up for the Elecrow CrowPanel Advanced 7.0" (ESP32-P4).
//
// Same shape as platform_m5stack.cpp -- LCD console first, then the UI, then
// the log routing -- but the hardware underneath is not an M5Stack, so the
// display is built by hand in crowpanel_display.cpp instead of by M5.begin().
#include "platform.h"
#include "crowpanel_display.hpp"
#include "lcd_console.h"
#include "ui_manager.h"
#include "ui_common.h"
#include "screen_log.h"
#include "sdkconfig.h"

#include <M5Unified.h>
#include "esp_log.h"

static const char *TAG = "PLATFORM";

// Store original serial vprintf function (before any LCD redirect)
static vprintf_like_t s_serial_vprintf = NULL;

// Flag to track if UI is initialized
static bool s_ui_initialized = false;

extern "C" esp_err_t platform_init(void)
{
    // Save the real serial vprintf FIRST (before any modifications)
    s_serial_vprintf = esp_log_set_vprintf(NULL);
    esp_log_set_vprintf(s_serial_vprintf);  // Restore it immediately

    // MIPI-DSI panel + GT911 touch, installed into M5.Lcd.
    esp_err_t ret = crowpanel::display_init();
    if (ret != ESP_OK) {
        // Nothing to draw on. The console is on UART here (never on USB), so
        // the board is still perfectly usable over the serial link.
        ESP_LOGE(TAG, "Display init failed, continuing without a UI");
        return ESP_OK;
    }

    // Set backlight level
#ifdef CONFIG_USB_MIDI_LCD_BACKLIGHT_LEVEL
    M5.Lcd.setBrightness(CONFIG_USB_MIDI_LCD_BACKLIGHT_LEVEL);
#else
    M5.Lcd.setBrightness(200);
#endif

    // Initialize LCD console (for boot messages before UI is ready)
    ret = lcd_console_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Redirect ESP logs to LCD for boot messages
    esp_log_set_vprintf(lcd_console_vprintf);

    // Initialize UI system
    ret = ui_init();
    if (ret == ESP_OK) {
        s_ui_initialized = true;
        // Route ESP logs to Screen Log buffer + serial (bypass LCD console)
        ui_log_hook_init_with_serial(s_serial_vprintf);
    } else {
        ESP_LOGW(TAG, "Failed to initialize UI system");
        // If UI fails, redirect stdout to LCD for PicoRuby
        lcd_console_redirect_stdout();
    }

    return ESP_OK;
}

extern "C" void platform_deinit(void)
{
    // Restore original serial vprintf if set
    if (s_serial_vprintf != NULL) {
        esp_log_set_vprintf(s_serial_vprintf);
        s_serial_vprintf = NULL;
    }
}

extern "C" void platform_update(void)
{
    // The touch refresh is all this board needs out of M5.update(); the rest
    // of it polls buttons, a PMIC and an RTC that M5.begin() never set up.
    crowpanel::touch_update();

    if (s_ui_initialized) {
        ui_update();
    }
}

extern "C" void platform_show_status(const char *status)
{
    lcd_console_set_status(status);
}

extern "C" void platform_show_midi_activity(bool active)
{
    lcd_console_set_midi_indicator(active);
}
