#ifndef SCREEN_MAIN_H
#define SCREEN_MAIN_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Tempo (the class keeps the old Main name for compatibility): the BPM, the
// external clock source, Sync and TAP tempo (docs/TEMPO.md). The BPM itself is
// set from Ruby (UI::Tempo.bpm) and its clock goes out through routes from the
// tempo device (ui_tempo.cpp). A layout can show it in a cell (setArea).
class ScreenMain : public Screen {
public:
    ScreenMain();

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;
    void getTileStatus(char* buf, int len) override;

    // The rectangle to draw in: the content area on its own screen, a cell
    // when a layout shows it. The layout sets it before enter() and puts it
    // back (resetArea) before anything else can show this screen.
    void setArea(int x, int y, int w, int h);
    void resetArea();

private:
    enum ButtonId {
        BTN_SYNC = 0,
        BTN_PLAY,             // Start / Stop: sends MIDI Start (FA) / Stop (FC)
        BTN_SOURCE_USB,
        BTN_SOURCE_DIN,
        BTN_SOURCE_BLE,
        BTN_SOURCE_USB_DEVICE,
        BTN_COUNT
    };

    struct Rect {
        int x, y, w, h;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    struct Button {
        Rect r;
        const char* label;
    };

    int m_areaX, m_areaY, m_areaW, m_areaH;
    bool m_isActive;
    bool m_needsRedraw;

    // Geometry, from the area (layoutView)
    Rect m_bpmRect;       // BPM display; a tap there is a TAP tempo tap
    int m_bpmTextSize;
    int m_extY;
    int m_extTextSize;
    int m_buttonTextSize;
    Button m_buttons[BTN_COUNT];

    // Source buttons shown, in order (board-dependent)
    static constexpr int SOURCE_SLOTS = 4;
    int m_sourceButtons[SOURCE_SLOTS];
    int m_sourceCount;

    // What was drawn last, to repaint only on a visible change
    int m_lastBpm;
    int m_lastExt10;
    uint32_t m_lastFlags;
    int64_t m_lastExtDrawUs;

    static const char* sourceLabel(int button);
    static midi_interface_t sourceInterface(int button);

    void layoutView();
    uint32_t stateFlags() const;
    void paint();
    void render(LovyanGFX* g, int ox, int oy);
    void renderPlay(LovyanGFX* g, int ox, int oy);
    void handleButtonPress(int buttonId);
};

// Global instance
ScreenMain& getScreenMain();

#endif // __cplusplus

#endif // SCREEN_MAIN_H
