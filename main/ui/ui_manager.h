#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "ui_common.h"
#include "esp_err.h"

#ifdef __cplusplus

// Abstract base class for all screens
class Screen {
public:
    virtual ~Screen() = default;

    // Called when screen becomes active
    virtual void enter() = 0;

    // Called when leaving this screen
    virtual void leave() = 0;

    // Called periodically to update state
    virtual void update() = 0;

    // Called to redraw the screen content area
    virtual void draw() = 0;

    // Called when touch event occurs in content area
    // x, y are relative to screen (0,0 is top-left)
    // pressed: true for touch start, false for release
    virtual void onTouch(int x, int y, bool pressed) = 0;

    // Called for center nav button press (screen-specific action)
    virtual void onNavCenter() {}

    // Get screen title for status bar
    virtual const char* getTitle() = 0;

    // Get center nav button label (if any)
    virtual const char* getNavCenterLabel() { return nullptr; }
};

// UIManager singleton class
class UIManager {
public:
    static UIManager& getInstance();

    // Initialize the UI system
    esp_err_t init();

    // Main update loop (call from app main loop)
    void update();

    // Screen navigation
    void nextScreen();
    void prevScreen();
    void setScreen(ui_screen_index_t index);
    ui_screen_index_t getCurrentScreenIndex() const;
    Screen* getCurrentScreen();

    // BPM management
    void setBpm(float bpm);
    float getBpm() const;
    void setExternalBpm(float bpm);
    float getExternalBpm() const;
    void setSyncMode(bool enabled);
    bool getSyncMode() const;
    void setBpmChangeCallback(bpm_change_cb_t cb);

    // Beat tracking
    void setBarBeat(uint32_t bar, uint8_t beat);
    void setBeatProgress(uint8_t progress);  // 0-23
    uint32_t getBar() const;
    uint8_t getBeat() const;
    uint8_t getBeatProgress() const;

    // Pad event callback
    void setPadEventCallback(pad_event_cb_t cb);
    pad_event_cb_t getPadEventCallback() const;

    // Force redraw
    void requestRedraw();

private:
    UIManager();
    ~UIManager();

    // Non-copyable
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    void handleTouch();
    void drawStatusBar();
    void drawNavBar();
    void initScreens();

    Screen* m_screens[UI_SCREEN_COUNT];
    int m_currentIndex;
    bool m_initialized;
    bool m_needsRedraw;

    // BPM state
    float m_internalBpm;
    float m_externalBpm;
    bool m_syncMode;
    bpm_change_cb_t m_bpmChangeCallback;

    // Beat tracking
    uint32_t m_bar;
    uint8_t m_beat;
    uint8_t m_beatProgress;

    // Pad callback
    pad_event_cb_t m_padEventCallback;

    // Touch state
    bool m_wasTouched;
    int m_lastTouchX;
    int m_lastTouchY;
};

extern "C" {
#endif

// C API for external use
esp_err_t ui_init(void);
void ui_update(void);

// BPM functions
void ui_set_bpm(float bpm);
float ui_get_bpm(void);
void ui_set_external_bpm(float bpm);
float ui_get_external_bpm(void);
void ui_set_sync_mode(bool enabled);
bool ui_get_sync_mode(void);
void ui_set_bpm_change_callback(bpm_change_cb_t cb);

// Beat tracking
void ui_set_bar_beat(uint32_t bar, uint8_t beat);
void ui_set_beat_progress(uint8_t progress);

// Screen control
void ui_set_screen(ui_screen_index_t index);
ui_screen_index_t ui_get_current_screen(void);

// Pad callback
void ui_set_pad_event_callback(pad_event_cb_t cb);

// Request screen redraw
void ui_request_redraw(void);

#ifdef __cplusplus
}
#endif

#endif // UI_MANAGER_H
