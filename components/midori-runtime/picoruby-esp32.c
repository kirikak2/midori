#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <nvs_flash.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "picoruby.h"
#include "picoruby-esp32.h"
#include "midi.h"            // MIDI_TRANSPORT_USB_DEVICE, MIDI_Filter_flush (picoruby-midi)
#include "midi_transport.h"  // MIDI_transport_send (picoruby-midi)
#include "midi_route.h"      // MIDI_route_clear (picoruby-midi)
#include "amy_gem.h"         // AMY_GEM_reset (picoruby-amy)
#include "ui.h"              // picoruby_ui_cleanup (picoruby-ui)

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
  // Their filters went with them (MIDI_route_clear drops those too); so do
  // the packets still queued for the script's filter blocks.
  MIDI_Filter_flush();

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

  // Menu customisation (UI.menu) and layouts (UI.layout) belong to the
  // script too. Their callbacks lived in its VM, so the pages would be dead.
  picoruby_ui_cleanup();

  ESP_LOGI(TAG, "MIDI cleanup completed");
}

// Script list management
//
// Written by the PicoRuby task (notify_scripts_to_c) and read by the UI task,
// so every access goes through g_script_list_mutex. The names are heap
// allocated on demand rather than kept in .bss (see docs/MEMORY_ALLOCATION.md).
static char **g_script_list = NULL;
static int g_script_count = 0;
static int g_script_capacity = 0;
static StaticSemaphore_t g_script_list_mutex_buf;
static SemaphoreHandle_t g_script_list_mutex = NULL;
static volatile bool g_script_list_ready = false;
static volatile uint32_t g_script_list_version = 0;

// Static creation only initializes memory, so it is safe before the scheduler
// starts and both tasks always see the same mutex.
__attribute__((constructor))
static void
script_list_mutex_init(void)
{
  g_script_list_mutex = xSemaphoreCreateMutexStatic(&g_script_list_mutex_buf);
}

// SD card refresh request (from UI to Ruby)
// Note: Not static, accessible from picoruby_supervisor.c
volatile bool g_sd_refresh_requested = false;

void
picoruby_esp32_clear_script_list(void)
{
  xSemaphoreTake(g_script_list_mutex, portMAX_DELAY);
  for (int i = 0; i < g_script_count; i++) {
    free(g_script_list[i]);
  }
  free(g_script_list);
  g_script_list = NULL;
  g_script_count = 0;
  g_script_capacity = 0;
  g_script_list_ready = false;
  xSemaphoreGive(g_script_list_mutex);
  ESP_LOGD(TAG, "Script list cleared");
}

bool
picoruby_esp32_add_script(const char *filename)
{
  if (filename == NULL) {
    return false;
  }
  size_t len = strlen(filename);
  if (len == 0 || len >= PICORUBY_MAX_SCRIPT_NAME) {
    // A truncated name would not open, so leave it out instead.
    ESP_LOGW(TAG, "Script name too long (max %d): %s", PICORUBY_MAX_SCRIPT_NAME - 1, filename);
    return false;
  }

  bool added = false;
  xSemaphoreTake(g_script_list_mutex, portMAX_DELAY);
  if (g_script_count < PICORUBY_MAX_SCRIPTS) {
    if (g_script_count == g_script_capacity) {
      int cap = g_script_capacity ? g_script_capacity * 2 : 16;
      if (cap > PICORUBY_MAX_SCRIPTS) cap = PICORUBY_MAX_SCRIPTS;
      char **list = realloc(g_script_list, cap * sizeof(char *));
      if (list != NULL) {
        g_script_list = list;
        g_script_capacity = cap;
      }
    }
    if (g_script_count < g_script_capacity) {
      char *name = strdup(filename);
      if (name != NULL) {
        g_script_list[g_script_count++] = name;
        added = true;
      }
    }
  }
  int count = g_script_count;
  xSemaphoreGive(g_script_list_mutex);

  if (added) {
    ESP_LOGD(TAG, "Added script: %s (total: %d)", filename, count);
  }
  return added;
}

int
picoruby_esp32_get_script_count(void)
{
  xSemaphoreTake(g_script_list_mutex, portMAX_DELAY);
  int count = g_script_count;
  xSemaphoreGive(g_script_list_mutex);
  return count;
}

int
picoruby_esp32_foreach_script(void (*fn)(const char *filename, void *arg), void *arg)
{
  xSemaphoreTake(g_script_list_mutex, portMAX_DELAY);
  int count = g_script_count;
  for (int i = 0; i < count; i++) {
    fn(g_script_list[i], arg);
  }
  xSemaphoreGive(g_script_list_mutex);
  return count;
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
