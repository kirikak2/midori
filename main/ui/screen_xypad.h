#ifndef SCREEN_XYPAD_H
#define SCREEN_XYPAD_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// A touch surface where each of up to five simultaneous fingers is its own
// independently configured "slot" (see the ui_xypad_* API in ui_common.h and
// docs/XYPAD.md). This class turns touches into ui_xypad_touch_* calls and
// composites the result -- an expanding ripple on touchdown, a fading trail
// behind a moving finger, and the current dot/label -- into an offscreen
// sprite every frame. Same technique Tombola uses for its play area
// (see screen_tombola.h/.cpp): the model only knows values, not pixels or
// timing, so all of this animation state lives here.
class ScreenXYPad : public Screen {
public:
    ScreenXYPad();
    ~ScreenXYPad() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    const char* getTitle() override;

private:
    bool m_isActive;
    LGFX_Sprite* m_sprite;
    int64_t m_lastDrawUs;
    bool m_hadContent;   // Whether the previous frame painted anything -- lets
                          // update() paint exactly one more (empty) frame when
                          // everything finishes fading, then go idle for free.

    // Where each slot's touch last was, in screen pixels. The model only
    // keeps the mapped value (note/bend/x/y), not the pixel position that
    // produced it, so the screen has to remember this itself.
    bool m_touchKnown[UI_XYPAD_MAX_TOUCHES];
    int  m_touchX[UI_XYPAD_MAX_TOUCHES];
    int  m_touchY[UI_XYPAD_MAX_TOUCHES];

    // Fading trail: a small ring buffer of recent points per slot. head is
    // the next write index; count is how many are valid (<= TRAIL_MAX_POINTS,
    // oldest first counting back from head).
    struct TrailPoint { int x, y; int64_t atUs; };
    static constexpr int TRAIL_MAX_POINTS = 48;
    TrailPoint m_trail[UI_XYPAD_MAX_TOUCHES][TRAIL_MAX_POINTS];
    int m_trailHead[UI_XYPAD_MAX_TOUCHES];
    int m_trailCount[UI_XYPAD_MAX_TOUCHES];
    int64_t m_lastTrailPushUs[UI_XYPAD_MAX_TOUCHES];

    // Expanding ripple rings, one spawned per touchdown. A flat pool rather
    // than per-slot: several can be in flight on the same slot at once
    // (tap-tap-tap) and each animates independently of the others.
    struct Ripple { bool active; int x, y; int64_t startUs; uint16_t color; };
    static constexpr int RIPPLE_MAX = 8;
    Ripple m_ripples[RIPPLE_MAX];

    void allocateSprite();
    void contentRect(int& x, int& y, int& w, int& h);
    uint16_t slotColor(int index);
    void noteName(uint8_t note, char* buf, int bufSize);

    void pushTrailPoint(int index, int x, int y, int64_t now);
    void spawnRipple(int x, int y, uint16_t color, int64_t now);
    void pruneTrails(int64_t now);
    bool hasAnythingToDraw();

    void renderFrame(LovyanGFX* g, int ox, int oy, int contentW, int64_t now);
    void paintFrame(int64_t now);

    // Degraded fallback used only if the sprite could not be allocated: the
    // plain dot+label from before, no ripple/trail (precise incremental erase
    // for many independently fading shapes is not practical without a
    // buffer). Still gets the edge-of-screen label placement fix.
    struct DrawnSlot {
        bool shown, latched;
        int x, y, eraseX, eraseY, eraseW, eraseH;
    };
    DrawnSlot m_drawnFallback[UI_XYPAD_MAX_TOUCHES];
    void fallbackEraseSlot(int index);
    void fallbackPaintSlot(int index, int x, int y);
    void fallbackRefresh(int index, int x, int y);
};

// Global instance
ScreenXYPad& getScreenXYPad();

#endif // __cplusplus

#endif // SCREEN_XYPAD_H
