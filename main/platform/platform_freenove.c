#include "platform.h"
#include "esp_log.h"
#include "ui/ui_common.h"

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

// UI stubs for Freenove (no display)
// These are required by picoruby-esp32/picoruby/mrbgems/picoruby-ui/ports/esp32/ui.c

bool ui_event_pop(ui_event_t *event)
{
    // No UI on Freenove - always return false (no events)
    (void)event;
    return false;
}

int ui_event_available(void)
{
    // No UI on Freenove - always return 0 (no events)
    return 0;
}

float ui_get_bpm(void)
{
    // No UI on Freenove - return default BPM
    return 120.0f;
}

void ui_add_log(const char *text)
{
    // No UI on Freenove - just ignore log requests
    (void)text;
}
