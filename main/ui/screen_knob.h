#ifndef SCREEN_KNOB_H
#define SCREEN_KNOB_H

#include "ui_manager.h"

#ifdef __cplusplus

// A grid of knobs, each a ring gauge turned by sweeping a finger around it.
// The knobs themselves (values, ranges, banks) live behind the ui_knob_* C API
// in ui_common.cpp; this class draws them and turns touches into value changes.
// Nothing here sends MIDI -- the Ruby block attached to a knob does that, the
// same way pads work.
class ScreenKnobs : public Screen {
public:
    ScreenKnobs();
    ~ScreenKnobs() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

private:
    bool m_isActive;

    // Which knob each finger is turning, so several can move at once. A knob
    // already claimed by one finger ignores the others: two fingers on one ring
    // would add their angular velocities together and make it lurch.
    int m_touchToKnob[MAX_TOUCH_POINTS];
    int m_lastX[MAX_TOUCH_POINTS];
    int m_lastY[MAX_TOUCH_POINTS];
    // The value being turned, kept unquantized. Feeding the knob's own snapped
    // value back in would discard every movement smaller than one step, which
    // is what a slow drag is made of.
    float m_accum[MAX_TOUCH_POINTS];
    bool m_moved[MAX_TOUCH_POINTS];   // Did this drag change the value at all

    // The value each ring is currently painted with, so a change can be drawn
    // as the wedge between the two instead of repainting the whole ring.
    float m_drawn[UI_KNOB_COUNT];
    bool m_hasDrawn[UI_KNOB_COUNT];
    uint8_t m_drawnBank;
    uint8_t m_drawnBanksInUse;   // Which letters the strip is showing as live

    char m_title[16];   // "Knobs A"

    void cellRect(int index, int& x, int& y, int& w, int& h);
    void ringCenter(int index, int& cx, int& cy);
    int  hitTestKnob(int x, int y);
    int  hitTestBank(int x, int y);
    void bankRect(int bank, int& x, int& y, int& w, int& h);

    float valueAngle(const knob_config_t* k, float value);
    float originAngle(const knob_config_t* k);
    void  paintSpan(int cx, int cy, float from, float to, uint16_t color);

    void drawKnob(int index, bool grabbed);
    void drawKnobValue(int index);          // Gauge delta + the number
    void drawValueText(int index, const knob_config_t* k);
    void drawGrabRing(int index, bool on);
    void drawBankStrip();
    void drawAll();
    void releaseAllTouches();
    bool isGrabbed(int index);
    uint8_t banksInUseMask();
};

// Global instance
ScreenKnobs& getScreenKnobs();

#endif // __cplusplus

#endif // SCREEN_KNOB_H
