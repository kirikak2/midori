#include "lcd_console.h"
#include "sdkconfig.h"

#include <M5Unified.h>
#include <cstring>
#include <cstdio>

// Screen dimensions for M5Stack CoreS3 SE (2.0" IPS)
static constexpr int SCREEN_WIDTH = 320;
static constexpr int SCREEN_HEIGHT = 240;

// Layout constants
static constexpr int STATUS_BAR_HEIGHT = 20;
static constexpr int LOG_AREA_Y = STATUS_BAR_HEIGHT;
static constexpr int LOG_AREA_HEIGHT = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;

// Maximum line length to fit on screen
static constexpr int MAX_LINE_CHARS = 52;

// Buffer for formatting
static char s_format_buffer[256];
static char s_stripped_buffer[MAX_LINE_CHARS + 1];

// MIDI indicator state
static bool s_midi_active = false;

// Mutex for thread safety
static portMUX_TYPE s_log_mutex = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Strip ANSI escape codes from log output
 */
static void strip_ansi_codes(char *dest, const char *src, size_t max_len)
{
    size_t d = 0;
    bool in_escape = false;

    for (size_t s = 0; src[s] && d < max_len - 1; s++) {
        if (src[s] == '\033') {
            in_escape = true;
            continue;
        }
        if (in_escape) {
            if ((src[s] >= 'A' && src[s] <= 'Z') ||
                (src[s] >= 'a' && src[s] <= 'z')) {
                in_escape = false;
            }
            continue;
        }
        // Skip control characters except newline
        if (src[s] >= 32 || src[s] == '\n') {
            dest[d++] = src[s];
        }
    }
    dest[d] = '\0';
}

extern "C" esp_err_t lcd_console_init(void)
{
    // Configure display for text console
#ifdef CONFIG_USB_MIDI_LCD_FONT_SIZE
    M5.Lcd.setTextSize(CONFIG_USB_MIDI_LCD_FONT_SIZE);
#else
    M5.Lcd.setTextSize(1);
#endif

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.fillScreen(TFT_BLACK);

    // Set up scrolling text area (below status bar)
    M5.Lcd.setScrollRect(0, LOG_AREA_Y, SCREEN_WIDTH, LOG_AREA_HEIGHT, TFT_BLACK);
    M5.Lcd.setTextScroll(true);
    M5.Lcd.setCursor(0, LOG_AREA_Y);

    // Draw initial status bar
    lcd_console_set_status("USB MIDI Host");

    return ESP_OK;
}

extern "C" void lcd_console_write(const char *text)
{
    portENTER_CRITICAL(&s_log_mutex);

    // Strip ANSI codes and truncate for LCD
    strip_ansi_codes(s_stripped_buffer, text, MAX_LINE_CHARS);

    // Output to LCD
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.print(s_stripped_buffer);

    portEXIT_CRITICAL(&s_log_mutex);
}

extern "C" int lcd_console_vprintf(const char *fmt, va_list args)
{
    // Format the log message
    int len = vsnprintf(s_format_buffer, sizeof(s_format_buffer), fmt, args);

    // Write to LCD
    lcd_console_write(s_format_buffer);

    return len;
}

extern "C" void lcd_console_set_status(const char *status)
{
    portENTER_CRITICAL(&s_log_mutex);

    // Save current cursor position
    int saved_x = M5.Lcd.getCursorX();
    int saved_y = M5.Lcd.getCursorY();

    // Draw status bar background
    M5.Lcd.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, TFT_NAVY);

    // Draw status text
    M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
    M5.Lcd.setCursor(4, 4);
    M5.Lcd.print(status);

    // Draw MIDI indicator
    uint16_t indicator_color = s_midi_active ? TFT_GREEN : TFT_DARKGREY;
    M5.Lcd.fillCircle(SCREEN_WIDTH - 14, STATUS_BAR_HEIGHT / 2, 6, indicator_color);

    // Restore cursor and text color
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(saved_x, saved_y);

    portEXIT_CRITICAL(&s_log_mutex);
}

extern "C" void lcd_console_set_midi_indicator(bool active)
{
    if (s_midi_active != active) {
        s_midi_active = active;

        portENTER_CRITICAL(&s_log_mutex);

        // Update only the indicator circle
        uint16_t color = active ? TFT_GREEN : TFT_DARKGREY;
        M5.Lcd.fillCircle(SCREEN_WIDTH - 14, STATUS_BAR_HEIGHT / 2, 6, color);

        portEXIT_CRITICAL(&s_log_mutex);
    }
}

extern "C" void lcd_console_clear(void)
{
    portENTER_CRITICAL(&s_log_mutex);

    M5.Lcd.fillRect(0, LOG_AREA_Y, SCREEN_WIDTH, LOG_AREA_HEIGHT, TFT_BLACK);
    M5.Lcd.setCursor(0, LOG_AREA_Y);

    portEXIT_CRITICAL(&s_log_mutex);
}
