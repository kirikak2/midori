// The simulator's platform layer (main/platform/platform.h) and its
// entry points for the page (sim/web/device.js).
//
// On the device platform_init() starts M5Unified and the UI and the main
// task calls platform_update() in a loop. Here the page calls sim_frame()
// from requestAnimationFrame instead, which also runs the esp_timer
// callbacks and the queues' consumers that are due (sim_rtos_poll()).
#include "platform.h"
#include "ui_common.h"
#include "ui_manager.h"
#include "panel_wasm_fb.hpp"
#include "sim_rtos.h"

#include <M5Unified.h>
#include <emscripten.h>
#include "esp_log.h"

m5::M5Unified M5;

static bool s_ui_initialized = false;

void m5::Touch_Class::sim_set(uint8_t count, const touch_detail_t* details)
{
  _count = count > TOUCH_MAX ? TOUCH_MAX : count;
  for (int i = 0; i < _count; ++i) _detail[i] = details[i];
}

extern "C" esp_err_t platform_init(void)
{
  M5.Lcd.init(new Panel_WasmFB(UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT));
  M5.Lcd.setBrightness(200);
  if (ui_init() == ESP_OK) s_ui_initialized = true;
  else ESP_LOGW("PLATFORM", "Failed to initialize UI system");
  return ESP_OK;
}

extern "C" void platform_deinit(void) {}

extern "C" void platform_update(void)
{
  if (s_ui_initialized) ui_update();
}

extern "C" void platform_show_status(const char* status) { (void)status; }
extern "C" void platform_show_midi_activity(bool active) { (void)active; }

// ---- page entry points

extern "C" EMSCRIPTEN_KEEPALIVE int sim_screen_width(void) { return UI_SCREEN_WIDTH; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_screen_height(void) { return UI_SCREEN_HEIGHT; }

// Pointers for the next frame: `n` entries of (id, x, y) in screen pixels,
// at `data` (int32 triples). A pointer that disappears is released.
static m5::touch_detail_t s_touch[m5::Touch_Class::TOUCH_MAX];
static uint8_t s_touch_count;

extern "C" EMSCRIPTEN_KEEPALIVE void sim_touch(const int32_t* data, int n)
{
  if (n > m5::Touch_Class::TOUCH_MAX) n = m5::Touch_Class::TOUCH_MAX;
  m5::touch_detail_t next[m5::Touch_Class::TOUCH_MAX];
  for (int i = 0; i < n; ++i)
  {
    auto& d = next[i];
    d.id = (uint8_t)data[i * 3];
    d.x = (int16_t)data[i * 3 + 1];
    d.y = (int16_t)data[i * 3 + 2];
    d.state = 1;
    bool found = false;
    for (int j = 0; j < s_touch_count; ++j)
    {
      if (s_touch[j].id != d.id) continue;
      d.prev_x = s_touch[j].x; d.prev_y = s_touch[j].y;
      d.base_x = s_touch[j].base_x; d.base_y = s_touch[j].base_y;
      d.base_msec = s_touch[j].base_msec;
      found = true;
    }
    if (!found)
    {
      d.prev_x = d.base_x = d.x;
      d.prev_y = d.base_y = d.y;
      d.base_msec = lgfx::millis();
      d.state = 3;   // pressed this frame
    }
  }
  s_touch_count = (uint8_t)n;
  for (int i = 0; i < n; ++i) s_touch[i] = next[i];
  M5.Touch.sim_set(s_touch_count, s_touch);
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_boot(void)
{
  platform_init();
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_frame(void)
{
  sim_rtos_poll();
  platform_update();
}
