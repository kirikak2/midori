#ifndef UI_PPA_H
#define UI_PPA_H

#include "ui_common.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Hardware-accelerated sprite blit for the Tab5 (ESP32-P4).
//
// Tab5's panel is a framebuffer panel: LovyanGFX draws straight into the PSRAM
// buffer that the MIPI-DSI controller scans out, so pushSprite() is a copy
// through Panel_FrameBufferBase::writeImage(). And because the panel is a
// 720x1280 portrait buffer shown as 1280x720 through setRotation(3), that copy
// does not even get the memcpy fast path -- writeImage() only takes it when
// the rotation is zero, otherwise every pixel goes through the rotating
// pixelcopy. For the Tombola play area that is 384,400 pixels a frame.
//
// The P4's Pixel-Processing Accelerator does exactly this operation -- a 2D
// block copy with rotation -- over DMA. blit() hands the sprite to it and
// leaves the CPU free.
//
// Everything here degrades safely: on any other board, or if the accelerator,
// the framebuffer or the geometry cannot be handled, blit() returns false and
// the caller is expected to fall back to sprite->pushSprite().
namespace ui_ppa {

// Copies the whole sprite to (x, y) in panel coordinates.
// @return false if the accelerated path is unavailable; nothing was drawn.
bool blit(LGFX_Sprite* sprite, int x, int y);

// True once the accelerator and the framebuffer have been resolved. Only
// meaningful after the first blit() call, which is what sets them up.
bool available(void);

}  // namespace ui_ppa

#endif // __cplusplus

#endif // UI_PPA_H
