#include "platform.h"
#include "lcd_console.h"
#include "sdkconfig.h"

#include <M5Unified.h>
#include "esp_log.h"

static const char *TAG = "PLATFORM";

// Store original vprintf function for potential restoration
static vprintf_like_t s_original_vprintf = NULL;

extern "C" esp_err_t platform_init(void)
{
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

    // Initialize LCD console
    esp_err_t ret = lcd_console_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Redirect ESP logs to LCD
    s_original_vprintf = esp_log_set_vprintf(lcd_console_vprintf);

    // Log startup message (this will go to LCD)
    ESP_LOGI(TAG, "Platform: M5Stack CoreS3 SE");
    ESP_LOGI(TAG, "Logging to LCD display");

    return ESP_OK;
}

extern "C" void platform_deinit(void)
{
    // Restore original vprintf if set
    if (s_original_vprintf != NULL) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = NULL;
    }
}

extern "C" void platform_update(void)
{
    M5.update();
}

extern "C" void platform_show_status(const char *status)
{
    lcd_console_set_status(status);
}

extern "C" void platform_show_midi_activity(bool active)
{
    lcd_console_set_midi_indicator(active);
}
