// midori-runtime's C API (picoruby-esp32.c / picoruby_supervisor.c /
// ble_midi_settings.c) and main/platform's audio settings, for the simulator.
//
// The script list comes from the page (the virtual /sd); asking for a
// script or a stop goes to the page, which runs scripts as the device's
// Supervisor does (a fresh VM each, sim/web/sim.js).
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#include "board_audio.h"

EM_JS(void, sim_js_request_script, (const char* path), {
  if (Module.simRequestScript) Module.simRequestScript(UTF8ToString(path));
});
EM_JS(void, sim_js_stop_script, (void), {
  if (Module.simStopScript) Module.simStopScript();
});
EM_JS(void, sim_js_request_sd_refresh, (void), {
  if (Module.simRequestSdRefresh) Module.simRequestSdRefresh();
});

void initialize_nvs(void) {}

// ---- script list (set by the page)

#define MAX_SCRIPTS 64
static char* s_scripts[MAX_SCRIPTS];
static int s_nscripts;
static bool s_ready;
static uint32_t s_version;
static char s_current[256];

EMSCRIPTEN_KEEPALIVE void sim_scripts_clear(void)
{
  for (int i = 0; i < s_nscripts; i++) free(s_scripts[i]);
  s_nscripts = 0;
  s_ready = false;
}

EMSCRIPTEN_KEEPALIVE void sim_scripts_add(const char* name)
{
  if (s_nscripts < MAX_SCRIPTS) s_scripts[s_nscripts++] = strdup(name);
}

EMSCRIPTEN_KEEPALIVE void sim_scripts_done(void)
{
  s_ready = true;
  s_version++;
}

EMSCRIPTEN_KEEPALIVE void sim_set_current_script(const char* path)
{
  strncpy(s_current, path ? path : "", sizeof(s_current) - 1);
}

int picoruby_esp32_foreach_script(void (*fn)(const char* filename, void* arg), void* arg)
{
  for (int i = 0; i < s_nscripts; i++) fn(s_scripts[i], arg);
  return s_nscripts;
}
uint32_t picoruby_esp32_get_script_list_version(void) { return s_version; }
bool picoruby_esp32_script_list_ready(void) { return s_ready; }
bool picoruby_esp32_request_script_change(const char* path) { sim_js_request_script(path); return true; }
void picoruby_esp32_request_sd_refresh(void) { sim_js_request_sd_refresh(); }

const char* supervisor_get_current_script(void) { return s_current[0] ? s_current : NULL; }
bool supervisor_stop_script(void) { sim_js_stop_script(); return true; }

// ---- BLE-MIDI settings: no BLE in the simulator

bool ble_midi_settings_available(void) { return false; }
void ble_midi_settings_set_device(bool on) { (void)on; }
void ble_midi_settings_set_host(bool on) { (void)on; }

// ---- audio volume (the page applies it to the built-in synth)

EM_JS(void, sim_js_volume, (int out, int percent), {
  if (Module.simVolume) Module.simVolume(out, percent);
});

static uint8_t s_volume[BOARD_AUDIO_OUTPUT_COUNT] = { 70, 50 };
bool board_audio_has_output(board_audio_output_t out) { return out == BOARD_AUDIO_SPEAKER; }
bool board_audio_headphone_inserted(void) { return false; }
void board_audio_set_volume(board_audio_output_t out, uint8_t percent)
{
  if (out >= BOARD_AUDIO_OUTPUT_COUNT) return;
  s_volume[out] = percent > 100 ? 100 : percent;
  sim_js_volume(out, s_volume[out]);
}
uint8_t board_audio_get_volume(board_audio_output_t out) { return out < BOARD_AUDIO_OUTPUT_COUNT ? s_volume[out] : 0; }
void board_audio_save(void) {}
