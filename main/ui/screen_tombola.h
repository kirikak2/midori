#ifndef SCREEN_TOMBOLA_H
#define SCREEN_TOMBOLA_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Physics-driven sequencer: balls bounce inside a rotating polygon and every
// wall collision fires a note. The model (state + stepping + sounding) lives
// in screen_tombola.cpp behind the ui_tombola_* C API declared in ui_common.h,
// so the sequencer keeps running while the user browses other screens; this
// class only draws it.
class ScreenTombola : public Screen {
public:
    ScreenTombola();
    ~ScreenTombola() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

private:
    bool m_isActive;

    // Off-screen buffer covering the whole play area. Every frame is composed
    // here and pushed in one pushSprite, so the polygon and the balls never
    // appear half-erased (drawing straight to the panel flickers badly at
    // 30-60fps because each line and circle is erased before it is redrawn).
    LGFX_Sprite* m_sprite;

    // Fallback state, used only when the sprite could not be allocated: the
    // previous frame's pixels, so it can be erased before the next is drawn.
    struct DrawnBall {
        bool active;
        int x, y, r;
    };
    DrawnBall m_drawnBalls[UI_TOMBOLA_MAX_BALLS];
    int m_drawnSides;
    float m_drawnAngle;
    float m_drawnRadius;
    bool m_hasDrawnFrame;

    int64_t m_lastDrawUs;

    // Everything the frame actually shows. While the sequencer is stopped
    // nothing moves, so an unchanged key means the repaint can be skipped
    // outright.
    struct FrameKey {
        int sides;
        int ballCount;
        float angle;
        float radius;
        float ballSize;
        float rotationRpm;
        float gravity;
        float bounce;
        bool flash;
        bool running;
    };
    FrameKey m_lastKey;

    void geometry(int& cx, int& cy, float& radius);
    void polygonPoint(int cx, int cy, float radius, float angle, int i, int sides,
                      int& px, int& py);
    void drawPolygon(LovyanGFX* g, int cx, int cy, float radius, float angle,
                     int sides, uint16_t color);
    // Composes one frame into g. ox/oy is g's origin in screen coordinates
    // (the sprite's top-left, or 0,0 when drawing straight to the panel).
    void renderFrame(LovyanGFX* g, int ox, int oy);
    void eraseFrame(LovyanGFX* g, int ox, int oy);
    void paintFrame(bool fullRepaint);
    bool frameChanged();
};

// Global instance
ScreenTombola& getScreenTombola();

#endif // __cplusplus

#endif // SCREEN_TOMBOLA_H
