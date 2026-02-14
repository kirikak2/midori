#include "lcd_console.h"
#include "sdkconfig.h"

#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs.h"
#include "esp_log.h"

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

// ============================================================================
// VFS driver for redirecting stdout to LCD
// ============================================================================

static const char *TAG = "LCD_VFS";

// Buffer for accumulating partial writes
static char s_line_buffer[256];
static size_t s_line_pos = 0;

/**
 * @brief VFS write function - outputs to LCD console
 */
static ssize_t lcd_vfs_write(int fd, const void *data, size_t size)
{
    const char *str = (const char *)data;

    for (size_t i = 0; i < size; i++) {
        char c = str[i];

        if (c == '\n' || s_line_pos >= sizeof(s_line_buffer) - 1) {
            // Flush the line to LCD
            s_line_buffer[s_line_pos] = '\0';
            if (s_line_pos > 0) {
                lcd_console_write(s_line_buffer);
            }
            if (c == '\n') {
                lcd_console_write("\n");
            }
            s_line_pos = 0;
        } else if (c != '\r') {
            // Accumulate character (skip CR)
            s_line_buffer[s_line_pos++] = c;
        }
    }

    return size;
}

/**
 * @brief VFS open function (stub)
 */
static int lcd_vfs_open(const char *path, int flags, int mode)
{
    // Return a dummy fd (we only support stdout)
    return 0;
}

/**
 * @brief VFS close function (stub)
 */
static int lcd_vfs_close(int fd)
{
    return 0;
}

/**
 * @brief VFS fstat function (stub)
 */
static int lcd_vfs_fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR;
    return 0;
}

extern "C" esp_err_t lcd_console_redirect_stdout(void)
{
    // Register VFS driver that will handle stdout (fd 1)
    // We use esp_vfs_register_fd_range to directly claim fd 1
    esp_vfs_t vfs = {};
    vfs.flags = ESP_VFS_FLAG_DEFAULT;
    vfs.write = &lcd_vfs_write;
    vfs.open = &lcd_vfs_open;
    vfs.close = &lcd_vfs_close;
    vfs.fstat = &lcd_vfs_fstat;

    // Register VFS for /dev/lcd path first
    esp_err_t ret = esp_vfs_register("/dev/lcd", &vfs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LCD VFS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Use freopen to redirect stdout to our VFS
    // This is supported in ESP-IDF's newlib implementation
    FILE *new_stdout = freopen("/dev/lcd", "w", stdout);
    if (new_stdout == NULL) {
        ESP_LOGE(TAG, "Failed to freopen stdout to LCD");
        return ESP_FAIL;
    }

    // Disable buffering for immediate output
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI(TAG, "stdout redirected to LCD console");
    return ESP_OK;
}
