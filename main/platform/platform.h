#ifndef PLATFORM_H
#define PLATFORM_H

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the platform-specific hardware and logging
 *
 * On Freenove: No-op (serial logging works by default)
 * On M5Stack: Initialize LCD and redirect logs to display
 *
 * @return ESP_OK on success
 */
esp_err_t platform_init(void);

/**
 * @brief De-initialize platform resources
 */
void platform_deinit(void);

/**
 * @brief Platform-specific update loop (call periodically from main loop)
 *
 * On M5Stack: Refresh LCD if needed
 */
void platform_update(void);

#ifdef CONFIG_USB_MIDI_BOARD_M5STACK_CORES3
/**
 * @brief Show a status message on LCD (non-log display area)
 * @param status Status message to display
 */
void platform_show_status(const char *status);

/**
 * @brief Show MIDI activity indicator
 * @param active true if MIDI is active
 */
void platform_show_midi_activity(bool active);
#endif

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_H
