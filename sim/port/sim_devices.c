// The device side of the transports, for the simulator.
//
// picoruby-midi's ESP32 port reaches the hardware through the USB-MIDI Host,
// USB-MIDI Device and UART gems. Here those calls go to the page:
//
//   USB-MIDI Host   -> the Web MIDI ports the page assigns to the "USB-MIDI Host" role
//   USB-MIDI Device -> the Web MIDI ports assigned to the "USB-MIDI Device" role
//   UART (SAM2695)  -> the page's built-in synth (or a Web MIDI output)
//
// Sending calls Module.simMidiOut(role, cin, b1, b2, b3, timeUs); receiving
// is sim_midi_in() from the page into a ring per role. BLE-MIDI is absent.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <emscripten.h>

#include "usb_midi_host.h"
#include "usb_midi_device.h"
#include "uart_midi.h"
#include "ble_midi_device.h"
#include "ble_midi_host.h"
#include "esp_timer.h"

enum { ROLE_USB_HOST = 0, ROLE_USB_DEVICE = 1, ROLE_SAM2695 = 2, ROLE_COUNT };

EM_JS(void, sim_js_midi_out, (int role, int cin, int b1, int b2, int b3, double t), {
  if (Module.simMidiOut) Module.simMidiOut(role, cin, b1, b2, b3, t);
});

#define RING 4096   // bytes, a power of two: a SysEx dump of ~3 KB fits
static struct { uint8_t data[RING]; uint32_t head, tail; bool connected; } s_rx[ROLE_COUNT];

EMSCRIPTEN_KEEPALIVE void sim_midi_connect(int role, int connected)
{
  if (role < 0 || role >= ROLE_COUNT) return;
  s_rx[role].connected = connected != 0;
  if (!connected) s_rx[role].head = s_rx[role].tail = 0;
}

// One USB-MIDI packet from the page. Dropped when the ring is full.
EMSCRIPTEN_KEEPALIVE void sim_midi_in(int role, int b0, int b1, int b2, int b3)
{
  if (role < 0 || role >= ROLE_COUNT) return;
  uint32_t used = s_rx[role].head - s_rx[role].tail;
  if (used + 4 > RING) return;
  uint8_t p[4] = { (uint8_t)b0, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3 };
  for (int i = 0; i < 4; i++) s_rx[role].data[(s_rx[role].head + i) & (RING - 1)] = p[i];
  s_rx[role].head += 4;
}

static int rx_available(int role) { return (int)(s_rx[role].head - s_rx[role].tail); }

static int rx_read(int role, uint8_t* out, size_t max_len)
{
  int n = 0;
  while ((size_t)n + 4 <= max_len && rx_available(role) >= 4) {
    for (int i = 0; i < 4; i++) out[n + i] = s_rx[role].data[(s_rx[role].tail + i) & (RING - 1)];
    s_rx[role].tail += 4;
    n += 4;
  }
  return n;
}

static int send(int role, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  sim_js_midi_out(role, cin & 0x0F, b1, b2, b3, (double)esp_timer_get_time());
  return 0;
}

// ---- USB-MIDI Host (slot 0 only)

usb_midi_host_status_t USB_MIDI_HOST_get_status(void)
{
  return s_rx[ROLE_USB_HOST].connected ? USB_MIDI_HOST_CONNECTED : USB_MIDI_HOST_DISCONNECTED;
}
int USB_MIDI_HOST_send_packet(uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  (void)cable;
  return s_rx[ROLE_USB_HOST].connected ? send(ROLE_USB_HOST, cin, b1, b2, b3) : -1;
}
int USB_MIDI_HOST_bytes_available(void) { return rx_available(ROLE_USB_HOST); }
int USB_MIDI_HOST_read_packet(uint8_t* out, size_t max_len) { return rx_read(ROLE_USB_HOST, out, max_len); }
int USB_MIDI_HOST_init(void) { return 0; }
usb_midi_host_status_t USB_MIDI_HOST_dev_status(int slot)
{
  return slot == 0 ? USB_MIDI_HOST_get_status() : USB_MIDI_HOST_DISCONNECTED;
}
int USB_MIDI_HOST_dev_send_packet(int slot, uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  return slot == 0 ? USB_MIDI_HOST_send_packet(cable, cin, b1, b2, b3) : -1;
}
int USB_MIDI_HOST_dev_bytes_available(int slot) { return slot == 0 ? USB_MIDI_HOST_bytes_available() : 0; }
int USB_MIDI_HOST_dev_read_packet(int slot, uint8_t* out, size_t max_len)
{
  return slot == 0 ? USB_MIDI_HOST_read_packet(out, max_len) : 0;
}
uint16_t USB_MIDI_HOST_dev_bit(int slot) { return slot == 0 ? 0x01 : 0; }
bool USB_MIDI_HOST_dev_info(int slot, usb_midi_host_device_info_t* info)
{
  if (slot != 0 || !s_rx[ROLE_USB_HOST].connected) return false;
  memset(info, 0, sizeof(*info));
  strcpy(info->manufacturer, "Web MIDI");
  strcpy(info->product, "USB-MIDI Host (Web MIDI)");
  return true;
}
bool USB_MIDI_HOST_dev_stats(int slot, usb_midi_host_stats_t* st)
{
  if (slot != 0) return false;
  memset(st, 0, sizeof(*st));
  st->last_status = -1;
  return true;
}

// ---- USB-MIDI Device

int USB_MIDI_DEVICE_init(void) { return 0; }
bool USB_MIDI_DEVICE_connected(void) { return s_rx[ROLE_USB_DEVICE].connected; }
int USB_MIDI_DEVICE_send_packet(uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  (void)cable;
  return s_rx[ROLE_USB_DEVICE].connected ? send(ROLE_USB_DEVICE, cin, b1, b2, b3) : -1;
}
int USB_MIDI_DEVICE_bytes_available(void) { return rx_available(ROLE_USB_DEVICE); }
int USB_MIDI_DEVICE_read_packet(uint8_t* out, size_t max_len) { return rx_read(ROLE_USB_DEVICE, out, max_len); }
void USB_MIDI_DEVICE_get_stats(usb_midi_device_stats_t* out) { memset(out, 0, sizeof(*out)); }

// ---- UART (SAM2695): always there, output only

static bool s_uart_input;
static int s_uart_tx = -1, s_uart_rx = -1;
static uint32_t s_uart_baud = 31250;
int UART_MIDI_init(int tx_pin, int rx_pin, uint32_t baud)
{
  s_uart_tx = tx_pin;
  s_uart_rx = rx_pin;
  if (baud) s_uart_baud = baud;
  return 0;
}
bool UART_MIDI_get_device_info(uart_midi_device_info_t* info)
{
  info->baud_rate = s_uart_baud;
  info->uart_num = 1;
  info->tx_pin = s_uart_tx;
  info->rx_pin = s_uart_rx;
  return true;
}
void UART_MIDI_Input_stop(void) { s_uart_input = false; }
uart_midi_status_t UART_MIDI_get_status(void) { return UART_MIDI_READY; }
int UART_MIDI_send_packet(uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
  (void)cable;
  return send(ROLE_SAM2695, cin, b1, b2, b3);
}
int UART_MIDI_bytes_available(void) { return 0; }
int UART_MIDI_read_bytes(uint8_t* out, size_t max_len) { (void)out; (void)max_len; return 0; }
bool UART_MIDI_Input_is_running(void) { return s_uart_input; }
int UART_MIDI_Input_start(void) { s_uart_input = true; return 0; }

// ---- BLE-MIDI: not in the simulator

bool BLE_MIDI_DEVICE_is_enabled(void) { return false; }
bool BLE_MIDI_DEVICE_is_connected(void) { return false; }
const char* BLE_MIDI_DEVICE_name(void) { return ""; }
void BLE_MIDI_DEVICE_stats(ble_midi_device_stats_t* out) { memset(out, 0, sizeof(*out)); }
midi_tmask_t BLE_MIDI_DEVICE_transport_bit(void) { return 0; }
bool BLE_MIDI_HOST_available(void) { return false; }
bool BLE_MIDI_HOST_is_enabled(void) { return false; }
int BLE_MIDI_HOST_connect(const uint8_t addr[6], uint8_t addr_type) { (void)addr; (void)addr_type; return -1; }
midi_tmask_t BLE_MIDI_HOST_dev_bit(int slot) { (void)slot; return 0; }
bool BLE_MIDI_HOST_dev_connected(int slot) { (void)slot; return false; }
int BLE_MIDI_HOST_dev_info(int slot, ble_midi_peer_t* out) { (void)slot; (void)out; return -1; }
int BLE_MIDI_HOST_dev_status(int slot) { (void)slot; return 0; }
void BLE_MIDI_HOST_disconnect(int slot) { (void)slot; }
int BLE_MIDI_HOST_forget(const uint8_t addr[6]) { (void)addr; return -1; }
void BLE_MIDI_HOST_format_addr(const uint8_t addr[6], char out[18]) { (void)addr; out[0] = 0; }
int BLE_MIDI_HOST_known(ble_midi_peer_t* out, int max) { (void)out; (void)max; return 0; }
int BLE_MIDI_HOST_scan_results(ble_midi_peer_t* out, int max) { (void)out; (void)max; return 0; }
int BLE_MIDI_HOST_scan_start(uint32_t duration_ms) { (void)duration_ms; return -1; }
void BLE_MIDI_HOST_scan_stop(void) {}
bool BLE_MIDI_HOST_scanning(void) { return false; }
