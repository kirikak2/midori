#ifndef LCD_CONSOLE_H
#define LCD_CONSOLE_H

#include "esp_err.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LCD console
 * @return ESP_OK on success
 */
esp_err_t lcd_console_init(void);

/**
 * @brief Write a line to the LCD console (with auto-scroll)
 * @param text The text to display
 */
void lcd_console_write(const char *text);

/**
 * @brief vprintf-compatible function for esp_log_set_vprintf
 * @param fmt Format string
 * @param args Variable argument list
 * @return Number of characters written
 */
int lcd_console_vprintf(const char *fmt, va_list args);

/**
 * @brief Update status bar area
 * @param status Status message to display
 */
void lcd_console_set_status(const char *status);

/**
 * @brief Show/hide MIDI activity indicator
 * @param active true to show active indicator
 */
void lcd_console_set_midi_indicator(bool active);

/**
 * @brief Clear the console area
 */
void lcd_console_clear(void);

/**
 * @brief Redirect stdout to LCD console using ESP-IDF VFS
 * This allows PicoRuby puts/print to output to the LCD
 * @return ESP_OK on success
 */
esp_err_t lcd_console_redirect_stdout(void);

#ifdef __cplusplus
}
#endif

#endif // LCD_CONSOLE_H
