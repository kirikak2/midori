/*
 * Midori: whether BLE-MIDI advertises (device) and reconnects remembered
 * peripherals (host), kept in NVS ("ble_midi": dev_on / host_on, both on by
 * default). See docs/BLE_MIDI.md.
 *
 * Scripts may switch either role for themselves (BLE_MIDI_DEVICE.enable =,
 * BLE_MIDI_HOST.enable =); the cleanup after the script puts the saved
 * settings back (ble_midi_settings_restore). Stubs without
 * CONFIG_USB_MIDI_BLE_MIDI.
 */

#ifndef MIDORI_BLE_MIDI_SETTINGS_H_
#define MIDORI_BLE_MIDI_SETTINGS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether this build has BLE-MIDI */
bool ble_midi_settings_available(void);

/* Register the transports and switch both roles on as saved (app_main) */
void ble_midi_settings_start(void);

bool ble_midi_settings_device(void);
bool ble_midi_settings_host(void);
/* Change and save (the UI) */
void ble_midi_settings_set_device(bool on);
void ble_midi_settings_set_host(bool on);

/* Back to the saved settings (after a script; also releases the BLE class's
 * hold on the NimBLE host, which a script may have left) */
void ble_midi_settings_restore(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDORI_BLE_MIDI_SETTINGS_H_ */
