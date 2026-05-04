/*
 * Midori app entry point.
 *
 * The USB-MIDI Host driver lives entirely inside the
 * picoruby-usb_midi_host gem now (see ports/esp32/usb_host_driver.c) -
 * Phase 5a-2 of the picoruby-midi standardization plan moved it out of
 * here so the gem is a self-contained "USB-MIDI Host" component.
 *
 * What remains in this file:
 *   - Platform bring-up (LCD, etc.)
 *   - Optional USB-MIDI Host driver bring-up (skipped on the
 *     CoreS3 USB-Serial board variant)
 *   - PicoRuby supervisor bring-up
 *   - The platform-update tick loop
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "platform.h"
#include "picoruby-esp32.h"
#include "picoruby_supervisor.h"

#ifndef CONFIG_USB_MIDI_BOARD_M5STACK_CORES3_USB_SERIAL
#include "../components/picoruby-esp32/picoruby/mrbgems/picoruby-usb_midi_host/include/usb_midi_host.h"
#endif

static const char *TAG = "MIDORI";

#ifdef CONFIG_USB_MIDI_BOARD_M5STACK_CORES3_USB_SERIAL
/* USB Serial Debug Mode - USB-MIDI Host disabled */
void app_main(void)
{
    ESP_ERROR_CHECK(platform_init());

    ESP_LOGI(TAG, "USB Serial Debug Mode - USB-MIDI disabled");
    ESP_LOGI(TAG, "Starting PicoRuby supervisor...");
    supervisor_init();

    while (1) {
        platform_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#else
/* USB-MIDI Host Mode */
void app_main(void)
{
    ESP_ERROR_CHECK(platform_init());

    ESP_LOGI(TAG, "Starting USB-MIDI Host driver...");
    if (USB_MIDI_HOST_start_driver() != 0) {
        ESP_LOGE(TAG, "Failed to start USB-MIDI Host driver");
    }

    ESP_LOGI(TAG, "Starting PicoRuby supervisor...");
    supervisor_init();

    while (1) {
        platform_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif
