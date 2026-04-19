#ifndef SCREEN_MAIN_H
#define SCREEN_MAIN_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

class ScreenMain : public Screen {
public:
    ScreenMain();
    ~ScreenMain() override;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
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
        BTN_SOURCE_USB,
        BTN_SOURCE_DIN,
        BTN_SOURCE_BLE,
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

    // Sprite covering the area above the Sync button (BPM adjust buttons,
    // BPM display, source-select buttons, external BPM text). Drawn off-screen
    // and pushed in a single pushSprite to avoid tearing on the Tab5 DSI panel.
    LGFX_Sprite* m_topSprite;

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
    uint8_t m_lastDrawnExternalBpmSource;  // Track which source was last selected
    int8_t m_lastSyncButtonState;  // 0=disabled, 1=off, 2=on (for avoiding unnecessary redraws)
    int64_t m_lastExternalBpmDrawTime;  // For throttling external BPM display updates

    void initButtons();
    void drawTopArea();        // BPM buttons + BPM display + source buttons + external BPM (sprite-backed)
    void drawSyncButton();
    void drawBarBeat();
    void drawBeatProgress();
    int hitTestButton(int x, int y);
    void handleButtonPress(int buttonId);
    void processTapTempo();
};

// Global instance
ScreenMain& getScreenMain();

#endif // __cplusplus

#endif // SCREEN_MAIN_H
