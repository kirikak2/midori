#include "platform.h"
#include "esp_log.h"

static const char *TAG = "PLATFORM";

esp_err_t platform_init(void)
{
    // Freenove uses default serial logging - nothing to do
    ESP_LOGI(TAG, "Platform: Freenove ESP32-S3");
    ESP_LOGI(TAG, "Logging to serial console");
    return ESP_OK;
}

void platform_deinit(void)
{
    // Nothing to clean up
}

void platform_update(void)
{
    // No periodic updates needed
}
