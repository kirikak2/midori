#ifndef SCREEN_MAIN_H
#define SCREEN_MAIN_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenMain : public Screen {
public:
    ScreenMain();
    ~ScreenMain() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

private:
    // Button IDs
    enum ButtonId {
        BTN_MINUS_10 = 0,
        BTN_MINUS_1,
        BTN_PLUS_1,
        BTN_PLUS_10,
        BTN_SYNC,
        BTN_COUNT
    };

    // Button definitions
    struct Button {
        int x, y, w, h;
        const char* label;
        bool pressed;
    };

    Button m_buttons[BTN_COUNT];
    bool m_isActive;
    bool m_needsRedraw;

    // TAP tempo state
    uint32_t m_tapTimes[UI_TAP_TEMPO_SAMPLES];
    int m_tapIndex;
    int m_tapCount;

    // Last drawn values (for partial updates)
    float m_lastDrawnBpm;
    float m_lastDrawnExternalBpm;
    uint32_t m_lastDrawnBar;
    uint8_t m_lastDrawnBeat;
    uint8_t m_lastDrawnProgress;
    bool m_lastDrawnSyncMode;

    void initButtons();
    void drawBpmDisplay();
    void drawExternalBpm();
    void drawSyncButton();
    void drawBarBeat();
    void drawBeatProgress();
    void drawButtons();
    int hitTestButton(int x, int y);
    void handleButtonPress(int buttonId);
    void processTapTempo();
};

// Global instance
ScreenMain& getScreenMain();

#endif // __cplusplus

#endif // SCREEN_MAIN_H
