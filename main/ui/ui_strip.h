#ifndef UI_STRIP_H
#define UI_STRIP_H

#include "ui_common.h"

#ifdef __cplusplus

#include <M5Unified.h>

// One off-screen buffer shared by every screen, drawn through band by band.
//
// Screens used to own a sprite the size of what they repaint: on Tab5 that is
// 1280x620 for the XYPad (1.5MB), 620x620 for the Tombola, 1280x240 for the
// Main top area and 1280x120 for the Log batch -- 3.2MB together, and none of
// them fits in the P4's internal RAM (768KB, of which this firmware has well
// under 400KB left once linked). They all fell back to PSRAM, where a buffer
// several times the 128KB L2 cache makes every pixel the CPU touches a miss.
//
// Instead there is a single strip of UI_SCREEN_WIDTH x LINES pixels
// in internal RAM. paint() splits a rectangle into horizontal bands that fit
// it, renders each band and copies it out before reusing the memory for the
// next. Only one band is ever live, and only the UI task draws, so no screen
// can see another's pixels. The strip is reshaped per call: a narrower
// rectangle gets more rows (the Tombola's 620px play area takes 132 at a time
// on Tab5).
//
// The draw callback renders the whole picture every band; LovyanGFX clips
// what falls outside. That costs a little CPU per band and buys back a
// buffer that lives in internal RAM.
//
// Nothing survives between bands or calls, so callers repaint everything they
// own (no incremental erase against the previous frame).
namespace ui_strip {

// Rows of the full-width strip: UI_SCREEN_WIDTH x 64 x 2 bytes = 160KB on
// Tab5, 128KB on CrowPanel, 40KB on CoreS3. Twice that no longer fits in the
// P4's internal RAM (the Log screen's 300KB batch sprite already did not);
// half of it doubles the number of bands, and with them the re-renders and
// copies per frame.
constexpr int LINES = 64;

// Allocates the strip. Called once from UIManager::init(), which runs at the
// very start of app_main, before USB / audio / PicoRuby fragment the heap.
// Tries internal RAM at LINES rows, then fewer rows, and only then
// PSRAM. Safe to call again (no-op).
void init(void);

// Rows of the given width that fit in the strip; 0 if there is no strip or
// the width does not fit at all.
int rows_for(int w);

// Paints the screen rectangle (x, y, w, h). For each band the strip is
// reshaped to w x band rows, filled with bg, and draw(g, ox, oy) is called
// with (ox, oy) the screen position of the strip's top-left pixel -- draw at
// screen coordinates minus that. The band is then copied to the panel.
//
// @return false if there is no strip; nothing was drawn and the caller should
//         draw straight to M5.Lcd instead.
template <typename F>
bool paint(int x, int y, int w, int h, uint16_t bg, F&& draw);

// --- Internals used by paint() -------------------------------------------

// The shared sprite, reshaped to w x h with LovyanGFX's default text state.
// h must not exceed rows_for(w).
LGFX_Sprite* shape(int w, int h);

// Copies a band to (x, y): straight into the DSI framebuffer when there is
// one (ui_fb.h), else pushSprite().
void push(LGFX_Sprite* sprite, int x, int y);

template <typename F>
bool paint(int x, int y, int w, int h, uint16_t bg, F&& draw)
{
    int rows = rows_for(w);
    if (rows <= 0) return false;

    for (int top = 0; top < h; top += rows) {
        int bandH = (h - top < rows) ? (h - top) : rows;
        LGFX_Sprite* s = shape(w, bandH);
        s->fillScreen(bg);
        draw(static_cast<LovyanGFX*>(s), x, y + top);
        push(s, x, y + top);
    }
    return true;
}

}  // namespace ui_strip

#endif // __cplusplus

#endif // UI_STRIP_H
