// M5Unified for the MIDoRI simulator: only what main/ui uses.
//
// M5.Lcd is a real M5GFX (LovyanGFX) whose panel is the wasm framebuffer
// (sim/port/panel_wasm_fb.*); M5.Touch reports the pointers the page sends
// (sim/port/platform_sim.cpp), with M5Unified's touch_detail_t semantics.
#pragma once

#include <M5GFX.h>
#include <stdint.h>

namespace m5 {

struct touch_detail_t
{
  int16_t x = 0, y = 0;
  int16_t prev_x = 0, prev_y = 0;
  int16_t base_x = 0, base_y = 0;
  uint32_t base_msec = 0;
  uint8_t id = 0;
  uint8_t state = 0;     // bit0 = pressed (M5Unified's touch_state_t, simplified)

  bool isPressed(void) const { return state & 1; }
  bool wasPressed(void) const { return state == 3; }
  bool wasReleased(void) const { return state == 2; }
  bool isReleased(void) const { return !(state & 1); }
  int deltaX(void) const { return x - prev_x; }
  int deltaY(void) const { return y - prev_y; }
  int distanceX(void) const { return x - base_x; }
  int distanceY(void) const { return y - base_y; }
};

class Touch_Class
{
public:
  static constexpr int TOUCH_MAX = 5;
  uint8_t getCount(void) const { return _count; }
  const touch_detail_t& getDetail(uint8_t i = 0) const { return _detail[i < TOUCH_MAX ? i : 0]; }
  bool isEnabled(void) const { return true; }

  // Simulator: the page's pointers for the next update()
  void sim_set(uint8_t count, const touch_detail_t* details);

private:
  uint8_t _count = 0;
  touch_detail_t _detail[TOUCH_MAX];
};

class Power_Class
{
public:
  void setUsbOutput(bool) {}
  int getBatteryLevel(void) { return 100; }
  bool isCharging(void) { return false; }
};

struct config_t { bool clear_display = true; bool output_power = true; };

class M5Unified
{
public:
  M5GFX Display;
  M5GFX& Lcd = Display;
  Touch_Class Touch;
  Power_Class Power;

  static config_t config(void) { return config_t(); }
  void begin(config_t = config_t()) {}
  void update(void) {}
  static void delay(uint32_t) {}
  static uint32_t millis(void) { return lgfx::millis(); }
};

}  // namespace m5

extern m5::M5Unified M5;
