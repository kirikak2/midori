#ifndef SCREEN_TIMER_H
#define SCREEN_TIMER_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Timer (docs/TIMER.md): the time left, the time set and its buttons, Play /
// Reset / Arm. The countdown itself is ui_timer.cpp; this only shows and sets
// it. A layout can show it in a cell (setArea), like the Tempo screen.
class ScreenTimer : public Screen {
public:
    ScreenTimer();

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;
    void getTileStatus(char* buf, int len) override;

    // The rectangle to draw in: the content area on its own screen, a cell
    // when a layout shows it (see ScreenMain::setArea)
    void setArea(int x, int y, int w, int h);
    void resetArea();

private:
    enum ButtonId {
        BTN_MINUS_MIN = 0,    // The time set: -1 min / -10 s / -1 s / +1 s / +10 s / +1 min
        BTN_MINUS_10,
        BTN_MINUS_1,
        BTN_PLUS_1,
        BTN_PLUS_10,
        BTN_PLUS_MIN,
        BTN_PLAY,             // The tempo's Start / Stop (as the Tempo screen's Play)
        BTN_RESET,
        BTN_ARM,
        BTN_COUNT
    };

    struct Rect {
        int x, y, w, h;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    int m_areaX, m_areaY, m_areaW, m_areaH;
    bool m_isActive;
    bool m_needsRedraw;

    // Geometry, from the area (layoutView)
    Rect m_timeRect;
    int m_timeTextSize;
    int m_infoY;
    int m_infoTextSize;
    int m_buttonTextSize;
    Rect m_buttons[BTN_COUNT];

    // What was drawn last, to repaint only on a visible change
    uint32_t m_lastSerial;
    bool m_lastPlaying;
    int m_pressed;            // Button held down (drawn highlighted), -1 if none
    int m_pressedTouch;

    void layoutView();
    void paint();
    void render(LovyanGFX* g, int ox, int oy);
    void renderButton(LovyanGFX* g, int ox, int oy, int id, const char* label,
                      uint16_t bg, uint16_t fg);
    bool adjustable() const;
    void handleButtonPress(int buttonId);
};

ScreenTimer& getScreenTimer();

#endif // __cplusplus

#endif // SCREEN_TIMER_H
