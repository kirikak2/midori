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
 *   - Optional USB-MIDI Device bring-up (CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE)
 *   - Optional USB-MIDI Host driver bring-up (CONFIG_USB_MIDI_HOST_ENABLED;
 *     off on the single-port ESP32-S3 boards whenever the USB port is
 *     given to a device role instead)
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
#include "console_input.h"

#if CONFIG_USB_MIDI_HOST_ENABLED
#include "../mrbgems/picoruby-usb_midi_host/include/usb_midi_host.h"
#endif

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
#include "../mrbgems/picoruby-usb_midi_device/include/usb_midi_device.h"
#endif

static const char *TAG = "MIDORI";

void app_main(void)
{
    ESP_ERROR_CHECK(platform_init());

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    /* Start the USB device role (CDC + MIDI) first. On Tab5 this is the
     * USB-C port and the USB-A host comes up right after; on the ESP32-S3
     * boards it owns the only USB port, so no host is started at all. */
    ESP_LOGI(TAG, "Starting USB-MIDI Device driver (CDC + MIDI)...");
    if (USB_MIDI_DEVICE_start() != 0) {
        ESP_LOGE(TAG, "Failed to start USB-MIDI Device driver");
    }
#elif CONFIG_USB_MIDI_USB_MODE_SERIAL
    ESP_LOGI(TAG, "USB-Serial/JTAG console mode");
#endif

#if CONFIG_USB_MIDI_HOST_ENABLED
    ESP_LOGI(TAG, "Starting USB-MIDI Host driver...");
    if (USB_MIDI_HOST_start_driver() != 0) {
        ESP_LOGE(TAG, "Failed to start USB-MIDI Host driver");
    }
#else
    ESP_LOGI(TAG, "USB-MIDI Host disabled (USB port used as a device)");
#endif

    /* Console input task. In USB-MIDI-Device mode it must be started after
     * USB_MIDI_DEVICE_start() so it can hook the gem's CDC receive
     * callback - the gem drains the CDC FIFO itself, so without the hook
     * every keystroke would be discarded. */
    console_input_start();

    ESP_LOGI(TAG, "Starting PicoRuby supervisor...");
    supervisor_init();

    while (1) {
        platform_update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
