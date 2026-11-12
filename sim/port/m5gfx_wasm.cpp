// M5GFX for the simulator.
//
// M5GFX.cpp's non-ESP branch is built around SDL (Panel_sdl, a window, a
// picture frame); the simulator has none of that. This file gives the few
// M5GFX members main/ui links against. The panel is set from outside with
// M5GFX::init(Panel_Device*), as the CrowPanel port does on the device.
#include <M5GFX.h>

namespace m5gfx
{
  M5GFX* M5GFX::_instance = nullptr;

  M5GFX::M5GFX(void) : LGFX_Device()
  {
    if (_instance == nullptr) _instance = this;
  }

  bool M5GFX::init_impl(bool use_reset, bool use_clear)
  {
    // Only reached through init(Panel_Device*), which sets the panel first
    if (_panel == nullptr) return false;
    return LGFX_Device::init_impl(use_reset, use_clear);
  }

  board_t M5GFX::autodetect(bool, board_t board) { return board; }
  void M5GFX::_set_backlight(lgfx::ILight*) {}
  void M5GFX::_set_pwm_backlight(int16_t, uint8_t, uint32_t, bool, uint8_t) {}

  void M5GFX::progressBar(int x, int y, int w, int h, uint8_t val)
  {
    drawRect(x, y, w, h, 0x09F1);
    fillRect(x + 1, y + 1, w * (((float)val) / 100.0f), h - 1, 0x09F1);
  }

  void M5GFX::pushState(void)
  {
    DisplayState s;
    s.gfxFont = _font;
    s.style = _text_style;
    s.metrics = _font_metrics;
    s.cursor_x = _cursor_x;
    s.cursor_y = _cursor_y;
    _displayStateStack.push_back(s);
  }

  void M5GFX::popState(void)
  {
    if (_displayStateStack.empty()) return;
    DisplayState s = _displayStateStack.back();
    _displayStateStack.pop_back();
    _font = s.gfxFont;
    _text_style = s.style;
    _font_metrics = s.metrics;
    _cursor_x = s.cursor_x;
    _cursor_y = s.cursor_y;
  }
}
