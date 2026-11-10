#ifndef BOARD_BLE_H
#define BOARD_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* After ble_midi_settings_start(): on a board whose Bluetooth controller is
 * a co-processor (Tab5: ESP32-C6 over ESP-Hosted), log whether it came up
 * and its firmware version. No-op elsewhere. */
void board_ble_report(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_BLE_H */
