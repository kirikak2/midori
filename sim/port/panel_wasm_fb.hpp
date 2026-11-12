// The simulator's display panel: an RGB565 framebuffer in the wasm heap.
//
// It is LovyanGFX's Panel_FrameBufferBase (the same base as Tab5's MIPI-DSI
// panel) over plain memory. display() hands the changed rectangle to the
// page, which converts it to RGBA and puts it on the <canvas> -- as Tab5's
// display() writes the cache back for the DSI DMA to see.
#pragma once

#define LGFX_USE_V1
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

class Panel_WasmFB : public lgfx::Panel_FrameBufferBase
{
public:
  Panel_WasmFB(int width, int height);
  ~Panel_WasmFB();

  bool init(bool use_reset) override;
  void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override;

  uint16_t* pixels(void) { return _pixels; }

private:
  uint16_t* _pixels = nullptr;
};
