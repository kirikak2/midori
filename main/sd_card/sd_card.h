#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_USB_MIDI_SDCARD_ENABLED

/**
 * @brief Initialize and mount the SD card
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_init(void);

/**
 * @brief Unmount and deinitialize the SD card
 */
void sd_card_deinit(void);

/**
 * @brief Check if SD card is mounted
 *
 * @return true if mounted, false otherwise
 */
bool sd_card_is_mounted(void);

/**
 * @brief Read a file from SD card into a buffer
 *
 * @param path Path to the file
 * @param buffer Output buffer (will be allocated, caller must free)
 * @param size Output size of the file
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_read_file(const char *path, char **buffer, size_t *size);

/**
 * @brief Callback function for directory enumeration
 *
 * @param filename Name of the file
 * @param is_dir True if this is a directory
 * @param user_data User-provided data passed to sd_card_list_files
 * @return true to continue enumeration, false to stop
 */
typedef bool (*sd_card_file_cb_t)(const char *filename, bool is_dir, void *user_data);

/**
 * @brief List files in a directory
 *
 * @param dir_path Directory path (e.g., "/sdcard" or "/sdcard/scripts")
 * @param callback Callback function called for each file
 * @param user_data User data passed to callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_list_files(const char *dir_path, sd_card_file_cb_t callback, void *user_data);

#endif // CONFIG_USB_MIDI_SDCARD_ENABLED

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_H
