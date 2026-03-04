#include "platform.h"
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

    // Initialize M5Stack hardware
    auto cfg = M5.config();

    // Disable unused peripherals for CoreS3 SE
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;

    M5.begin(cfg);

    // Enable USB OTG mode for USB Host functionality
    // This sets the USB_OTG_EN pin via AW9523B IO expander
    M5.Power.setUsbOutput(true);
    ESP_LOGI(TAG, "USB OTG mode enabled");

    // Set backlight level
#ifdef CONFIG_USB_MIDI_LCD_BACKLIGHT_LEVEL
    M5.Lcd.setBrightness(CONFIG_USB_MIDI_LCD_BACKLIGHT_LEVEL);
#else
    M5.Lcd.setBrightness(200);
#endif

    // Initialize LCD console (for boot messages before UI is ready)
    esp_err_t ret = lcd_console_init();
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
    M5.update();

    // Update UI if initialized
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
