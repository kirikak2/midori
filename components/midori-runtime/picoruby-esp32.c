#include <inttypes.h>
#include <string.h>
#include <nvs_flash.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "picoruby.h"
#include "picoruby-esp32.h"
#include "midi.h"            // MIDI_TRANSPORT_USB_DEVICE (picoruby-midi)
#include "midi_transport.h"  // MIDI_transport_send (picoruby-midi)
#include "midi_route.h"      // MIDI_route_clear (picoruby-midi)
#include "amy_gem.h"         // AMY_GEM_reset (picoruby-amy)

static const char *TAG = "PICORUBY";

#if (defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)) && defined(CONFIG_SPIRAM)
#include "esp_attr.h"
#define HEAP_IN_PSRAM 1
#endif

#ifndef HEAP_SIZE
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_SPIRAM)
// ESP32-P4 with PSRAM (Tab5 has 32MB): Use 4MB heap
#define HEAP_SIZE (1024 * 1024 * 4)
#elif defined(CONFIG_IDF_TARGET_ESP32S3) && defined(CONFIG_SPIRAM)
// ESP32-S3 with PSRAM: Use 2MB heap (plenty of room in 8MB PSRAM)
#define HEAP_SIZE (1024 * 1024 * 2)
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3 without PSRAM: Conservative heap
#define HEAP_SIZE (1024 * 180)
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
// ESP32-P4 without PSRAM: Conservative heap
#define HEAP_SIZE (1024 * 256)
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define HEAP_SIZE (1024 * 120)
#else
#define HEAP_SIZE (1024 * 120)
#endif
#endif

#if defined(HEAP_IN_PSRAM)
// Place heap in PSRAM for ESP32-S3 with external memory
// Note: Not static, accessible from picoruby_supervisor.c
EXT_RAM_BSS_ATTR uint8_t heap_pool[HEAP_SIZE];
#else
uint8_t heap_pool[HEAP_SIZE];
#endif

// The running VM (set by picoruby_supervisor.c). The prism compiler
// allocates through it (prism_xallocator.h), so Kernel.load needs it.
mrb_state *global_mrb = NULL;

// Script management (accessible from picoruby_supervisor.c)
static char g_current_script[128] = {0};
char g_requested_script[128] = {0};
volatile bool g_stop_requested = false;
volatile bool g_script_change_requested = false;

void
initialize_nvs(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

bool
picoruby_esp32_request_script_change(const char *script_path)
{
  if (script_path == NULL) {
    return false;
  }

  strncpy(g_requested_script, script_path, sizeof(g_requested_script) - 1);
  g_requested_script[sizeof(g_requested_script) - 1] = '\0';
  g_script_change_requested = true;
  g_stop_requested = true;

  // Update current script path
  strncpy(g_current_script, script_path, sizeof(g_current_script) - 1);
  g_current_script[sizeof(g_current_script) - 1] = '\0';

  ESP_LOGI(TAG, "Script change requested: %s", script_path);

  /* Bridge to the new supervisor API. The legacy flow expected the
   * Ruby UI loop to poll `sm.get_requested` and call `sm.request_script`,
   * but the supervisor only watches its own flag. Mirror the request
   * here so a UI-side call is honored even if the Ruby loop is stuck. */
  extern void supervisor_ruby_request_script(const char *script_path);
  supervisor_ruby_request_script(script_path);

  return true;
}

const char*
picoruby_esp32_get_current_script(void)
{
  return g_current_script[0] ? g_current_script : NULL;
}

void
picoruby_esp32_request_stop(void)
{
  g_stop_requested = true;
  ESP_LOGI(TAG, "Stop requested");
}

bool
picoruby_esp32_stop_requested(void)
{
  return g_stop_requested;
}

void
picoruby_esp32_clear_stop_flag(void)
{
  g_stop_requested = false;
  g_script_change_requested = false;
}

void
picoruby_esp32_midi_cleanup(void)
{
  ESP_LOGI(TAG, "Performing MIDI cleanup...");

  // Routes (MIDI.route) belong to the script that set them up: drop them
  // first, so nothing more is forwarded from here on.
  MIDI_route_clear();

  // Every registered transport except the USB-MIDI device port, which has
  // never been part of the cleanup (the host PC on the other end is not
  // ours to silence). That is the USB host and UART built-ins plus any
  // transport gem that registered itself (e.g. the AMY synth).
  const uint8_t mask = (uint8_t)~MIDI_TRANSPORT_USB_DEVICE;

  // Send cleanup messages to all MIDI channels (0-15)
  for (uint8_t ch = 0; ch < 16; ch++) {
    uint8_t status_cc = 0xB0 | ch;  // Control Change

    // All Sound Off (CC #120)
    MIDI_transport_send(mask, 0, 0x0B, status_cc, 120, 0);

    // All Notes Off (CC #123)
    MIDI_transport_send(mask, 0, 0x0B, status_cc, 123, 0);
  }

  // Send MIDI Stop (0xFC)
  MIDI_transport_send(mask, 0, 0x05, 0xFC, 0, 0);  // CIN 0x05 for single-byte system common

  // The AMY synth keeps its state (synths, patches, CC mappings, effects)
  // across scripts unless told otherwise, so the next script would start
  // from whatever this one left. Its audio task keeps running. No-op when
  // AMY is not running or not built in.
  AMY_GEM_reset();

  ESP_LOGI(TAG, "MIDI cleanup completed");
}

// Script list management
static char g_script_list[PICORUBY_MAX_SCRIPTS][PICORUBY_MAX_SCRIPT_NAME];
static int g_script_count = 0;
static volatile bool g_script_list_ready = false;
static volatile uint32_t g_script_list_version = 0;

// SD card refresh request (from UI to Ruby)
// Note: Not static, accessible from picoruby_supervisor.c
volatile bool g_sd_refresh_requested = false;

void
picoruby_esp32_clear_script_list(void)
{
  g_script_count = 0;
  g_script_list_ready = false;
  memset(g_script_list, 0, sizeof(g_script_list));
  ESP_LOGD(TAG, "Script list cleared");
}

bool
picoruby_esp32_add_script(const char *filename)
{
  if (filename == NULL || g_script_count >= PICORUBY_MAX_SCRIPTS) {
    return false;
  }

  strncpy(g_script_list[g_script_count], filename, PICORUBY_MAX_SCRIPT_NAME - 1);
  g_script_list[g_script_count][PICORUBY_MAX_SCRIPT_NAME - 1] = '\0';
  g_script_count++;
  ESP_LOGD(TAG, "Added script: %s (total: %d)", filename, g_script_count);
  return true;
}

int
picoruby_esp32_get_script_count(void)
{
  return g_script_count;
}

const char*
picoruby_esp32_get_script_name(int index)
{
  if (index < 0 || index >= g_script_count) {
    return NULL;
  }
  return g_script_list[index];
}

bool
picoruby_esp32_script_list_ready(void)
{
  return g_script_list_ready;
}

void
picoruby_esp32_set_script_list_ready(bool ready)
{
  g_script_list_ready = ready;
  if (ready) {
    g_script_list_version++;
  }
  ESP_LOGI(TAG, "Script list ready: %s (%d scripts, version=%lu)", ready ? "yes" : "no", g_script_count, (unsigned long)g_script_list_version);
}

uint32_t
picoruby_esp32_get_script_list_version(void)
{
  return g_script_list_version;
}

void
picoruby_esp32_request_sd_refresh(void)
{
  g_sd_refresh_requested = true;
  ESP_LOGI(TAG, "SD card refresh requested from UI");
}
