#include "sd_card.h"

#ifdef CONFIG_USB_MIDI_SDCARD_ENABLED

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static const char *TAG = "SD_CARD";

// SD card handle
static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

// Get board-specific pin configuration
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3)
    #define SD_CLK_PIN  CONFIG_USB_MIDI_SDCARD_M5_CLK
    #define SD_MOSI_PIN CONFIG_USB_MIDI_SDCARD_M5_MOSI
    #define SD_MISO_PIN CONFIG_USB_MIDI_SDCARD_M5_MISO
    #define SD_CS_PIN   CONFIG_USB_MIDI_SDCARD_M5_CS
#elif defined(CONFIG_USB_MIDI_BOARD_FREENOVE)
    #define SD_CLK_PIN  CONFIG_USB_MIDI_SDCARD_FREENOVE_CLK
    #define SD_MOSI_PIN CONFIG_USB_MIDI_SDCARD_FREENOVE_MOSI
    #define SD_MISO_PIN CONFIG_USB_MIDI_SDCARD_FREENOVE_MISO
    #define SD_CS_PIN   CONFIG_USB_MIDI_SDCARD_FREENOVE_CS
#else
    #error "No board selected"
#endif

#define MOUNT_POINT CONFIG_USB_MIDI_SDCARD_MOUNT_POINT
#define SPI_DMA_CHAN SPI_DMA_CH_AUTO

esp_err_t sd_card_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Pin configuration: CLK=%d, MOSI=%d, MISO=%d, CS=%d",
             SD_CLK_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_CS_PIN);

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Initialize SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CHAN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // SD card SPI device configuration
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = SPI2_HOST;

    // SD card host configuration
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    ESP_LOGI(TAG, "Mounting filesystem at %s", MOUNT_POINT);

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card is inserted.", esp_err_to_name(ret));
        }
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "Filesystem mounted successfully");

    // Card info
    sdmmc_card_print_info(stdout, s_card);

    return ESP_OK;
}

void sd_card_deinit(void)
{
    if (s_mounted) {
        ESP_LOGI(TAG, "Unmounting SD card");
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        spi_bus_free(SPI2_HOST);
        s_card = NULL;
        s_mounted = false;
    }
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_card_read_file(const char *path, char **buffer, size_t *size)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (path == NULL || buffer == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Reading file: %s", path);

    // Get file size
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    *size = st.st_size;
    ESP_LOGI(TAG, "File size: %zu bytes", *size);

    // Allocate buffer (+1 for null terminator)
    *buffer = (char *)malloc(*size + 1);
    if (*buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file");
        return ESP_ERR_NO_MEM;
    }

    // Open and read file
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        free(*buffer);
        *buffer = NULL;
        return ESP_FAIL;
    }

    size_t read_size = fread(*buffer, 1, *size, f);
    fclose(f);

    if (read_size != *size) {
        ESP_LOGW(TAG, "Read %zu bytes, expected %zu", read_size, *size);
        *size = read_size;
    }

    // Null terminate
    (*buffer)[*size] = '\0';

    ESP_LOGI(TAG, "File read successfully");
    return ESP_OK;
}

#endif // CONFIG_USB_MIDI_SDCARD_ENABLED
