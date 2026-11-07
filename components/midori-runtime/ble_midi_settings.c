/*
 * Midori: BLE-MIDI settings (ble_midi_settings.h)
 */

#include "ble_midi_settings.h"

#include "sdkconfig.h"

#if CONFIG_USB_MIDI_BLE_MIDI

#include "esp_log.h"
#include "nvs.h"

#include "ble_midi_device.h"
#include "ble_midi_host.h"
#include "nimble_owner.h"

void initialize_nvs(void);   /* picoruby-esp32.c */

static const char *TAG = "BLE_MIDI";

#define NVS_NS "ble_midi"

static bool s_loaded;
static bool s_device = true;
static bool s_host = true;

static void load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "dev_on", &v) == ESP_OK) s_device = v != 0;
    if (nvs_get_u8(h, "host_on", &v) == ESP_OK) s_host = v != 0;
    nvs_close(h);
}

static void save(const char *key, bool on)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool ble_midi_settings_available(void) { return true; }

void ble_midi_settings_start(void)
{
    /* Before anything reads NVS (these settings, the host's remembered
     * peripherals): app_main runs this ahead of the supervisor */
    initialize_nvs();
    load();
    BLE_MIDI_DEVICE_init();
    BLE_MIDI_HOST_init();
    ESP_LOGI(TAG, "BLE-MIDI device %s, host %s", s_device ? "on" : "off", s_host ? "on" : "off");
    /* The device first: its GATT service needs a host (re)start, the host
     * role does not */
    if (s_device) BLE_MIDI_DEVICE_enable(true);
    if (s_host) BLE_MIDI_HOST_enable(true);
}

bool ble_midi_settings_device(void) { load(); return s_device; }
bool ble_midi_settings_host(void) { load(); return s_host; }

void ble_midi_settings_set_device(bool on)
{
    load();
    s_device = on;
    save("dev_on", on);
    BLE_MIDI_DEVICE_enable(on);
}

void ble_midi_settings_set_host(bool on)
{
    load();
    s_host = on;
    save("host_on", on);
    BLE_MIDI_HOST_enable(on);
}

void ble_midi_settings_restore(void)
{
    load();
    /* A script's BLE class holds the host exclusively and never lets go
     * (its VM is gone now) */
    picoruby_nimble_stop();
    /* both are no-ops when nothing changed */
    BLE_MIDI_DEVICE_enable(s_device);
    BLE_MIDI_HOST_enable(s_host);
}

#else

bool ble_midi_settings_available(void) { return false; }
void ble_midi_settings_start(void) {}
bool ble_midi_settings_device(void) { return false; }
bool ble_midi_settings_host(void) { return false; }
void ble_midi_settings_set_device(bool on) { (void)on; }
void ble_midi_settings_set_host(bool on) { (void)on; }
void ble_midi_settings_restore(void) {}

#endif
