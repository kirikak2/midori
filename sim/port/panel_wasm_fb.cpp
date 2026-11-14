#include "panel_wasm_fb.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <emscripten.h>

// The page's side: copy the rectangle (x, y, w, h) of the RGB565 frame at
// `pixels` (stride `width`) to the canvas. Defined in sim/web/device.js.
EM_JS(void, sim_js_present, (const uint16_t* pixels, int width, int x, int y, int w, int h), {
  if (Module.simPresent) Module.simPresent(pixels, width, x, y, w, h);
});

Panel_WasmFB::Panel_WasmFB(int width, int height)
{
  auto cfg = config();
  cfg.panel_width = cfg.memory_width = width;
  cfg.panel_height = cfg.memory_height = height;
  config(cfg);
  _write_depth = lgfx::color_depth_t::rgb565_2Byte;
  _read_depth = lgfx::color_depth_t::rgb565_2Byte;
}

Panel_WasmFB::~Panel_WasmFB()
{
  free(_lines_buffer);
  free(_pixels);
}

bool Panel_WasmFB::init(bool use_reset)
{
  int w = _cfg.panel_width, h = _cfg.panel_height;
  _pixels = (uint16_t*)calloc((size_t)w * h, sizeof(uint16_t));
  _lines_buffer = (uint8_t**)malloc(sizeof(uint8_t*) * h);
  if (!_pixels || !_lines_buffer) return false;
  for (int y = 0; y < h; ++y) _lines_buffer[y] = (uint8_t*)&_pixels[(size_t)y * w];
  return Panel_FrameBufferBase::init(use_reset);
}

void Panel_WasmFB::display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h)
{
  // Merge the argument into the pending range the way the base does, take
  // the result, then let the base reset it.
  if (0 < w && 0 < h)
  {
    _range_mod.left   = std::min<int_fast16_t>(_range_mod.left  , x        );
    _range_mod.right  = std::max<int_fast16_t>(_range_mod.right , x + w - 1);
    _range_mod.top    = std::min<int_fast16_t>(_range_mod.top   , y        );
    _range_mod.bottom = std::max<int_fast16_t>(_range_mod.bottom, y + h - 1);
  }
  if (!_range_mod.empty())
  {
    sim_js_present(_pixels, _cfg.panel_width,
                   _range_mod.left, _range_mod.top,
                   _range_mod.right - _range_mod.left + 1,
                   _range_mod.bottom - _range_mod.top + 1);
  }
  Panel_FrameBufferBase::display(0, 0, 0, 0);
}
