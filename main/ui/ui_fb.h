#ifndef UI_FB_H
#define UI_FB_H

#include "ui_common.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Fast sprite copy into a DSI framebuffer panel (Tab5 / CrowPanel, ESP32-P4).
//
// Those panels are framebuffer panels: LovyanGFX draws straight into the PSRAM
// buffer that the MIPI-DSI controller scans out, so pushSprite() is a copy
// through Panel_FrameBufferBase::writeImage(). With a rotation (Tab5 runs
// setRotation(3) on a 720x1280 portrait buffer) that copy leaves the memcpy
// path and moves every pixel through the rotating pixelcopy, which made it the
// dominant cost of the animated screens.
//
// blit() does the same copy with the rotation folded into two strides, walking
// the sprite in tiles of columns so the writes land on a handful of
// framebuffer lines at a time, and then writes the touched lines back from the
// cache (the DSI DMA reads PSRAM, not the cache).
//
// This used to hand the copy to the P4's pixel accelerator (PPA). With the
// source in internal RAM and several band-sized blits a frame (ui_strip.h),
// the accelerator would now and then never complete a transaction -- on a
// geometry that had just succeeded many times -- and the engine cannot be
// recovered at runtime (2026-10-12). The CPU copy is deterministic.
//
// Everything here degrades safely: on any other board, or if the framebuffer
// or the geometry cannot be handled, blit() returns false and the caller is
// expected to fall back to sprite->pushSprite().
namespace ui_fb {

// Copies the whole sprite to (x, y) in screen (rotated) coordinates. The copy
// is complete and visible to the display when this returns.
// @return false if the fast path is unavailable; nothing was drawn.
bool blit(LGFX_Sprite* sprite, int x, int y);

}  // namespace ui_fb

#endif // __cplusplus

#endif // UI_FB_H
