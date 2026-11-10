/*
 * Bluetooth controller of the board, for BLE-MIDI (docs/BLE_MIDI.md §10)
 *
 * The ESP32-S3 boards have the controller on the chip: nothing to do.
 *
 * The Tab5 has it on its ESP32-C6, over SDIO with ESP-Hosted (esp_hosted
 * 1.4.0). ESP-Hosted starts its transport tasks from a constructor; the
 * first nimble_port_init() (picoruby-ble's owner, on a BLE-MIDI gem's task)
 * resets the C6 and waits for the link (ble_transport_ll_init). The C6 is
 * powered by the IO expander (0x44 P0 = WLAN_PWR_EN), which M5GFX turns on
 * in platform_init(), before BLE-MIDI starts.
 *
 * This only reports how that went: the C6's firmware version once the BLE
 * host is up, or a warning when it does not come up (a C6 firmware without
 * the BT controller, or no ESP-Hosted firmware at all).
 */

#include "board_ble.h"

#include "sdkconfig.h"

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE && CONFIG_USB_MIDI_BLE_MIDI

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted_api_types.h"

/* esp_hosted_api.h itself includes esp_wifi.h (main does not need Wi-Fi) */
esp_err_t esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t *ver_info);

#include "ble_midi_device.h"
#include "ble_midi_host.h"

static const char *TAG = "BOARD_BLE";

/* NimBLE's nimble_port_deinit() calls this; esp_hosted 1.4.0 has only the
 * init side (ble_transport_ll_init, which brings the SDIO link up). The
 * shared NimBLE owner stops and starts the host whenever a client adds
 * GATT services, so the link stays up across that: the next init finds it
 * ready ("Transport is already up") and the host's HCI Reset clears the
 * C6's controller. */
void ble_transport_ll_deinit(void)
{
}

#define WAIT_MS 20000

static bool ble_up(void)
{
    ble_midi_device_stats_t st;
    BLE_MIDI_DEVICE_stats(&st);
    bool dev = st.state == BLE_MIDI_DEVICE_ADVERTISING || st.state == BLE_MIDI_DEVICE_LINKED
               || st.state == BLE_MIDI_DEVICE_CONNECTED;
    return dev || BLE_MIDI_HOST_is_ready();
}

static void report_task(void *arg)
{
    (void)arg;
    int waited = 0;
    while (!ble_up() && waited < WAIT_MS) {
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }
    if (!ble_up()) {
        ESP_LOGW(TAG, "BLE did not come up through the ESP32-C6 (ESP-Hosted) in %d s: "
                 "is BLE-MIDI off, or does the C6 firmware lack the BT controller?",
                 WAIT_MS / 1000);
    } else {
        esp_hosted_coprocessor_fwver_t v = { 0 };
        if (esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK) {
            ESP_LOGI(TAG, "BLE through ESP32-C6, ESP-Hosted firmware %lu.%lu.%lu",
                     (unsigned long)v.major1, (unsigned long)v.minor1, (unsigned long)v.patch1);
        } else {
            ESP_LOGI(TAG, "BLE through ESP32-C6 (firmware version not reported)");
        }
    }
    vTaskDelete(NULL);
}

void board_ble_report(void)
{
    if (!BLE_MIDI_DEVICE_is_enabled() && !BLE_MIDI_HOST_is_enabled()) return;
    xTaskCreate(report_task, "board_ble", 3072, NULL, 2, NULL);
}

#else

void board_ble_report(void) {}

#endif
