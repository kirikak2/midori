#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "ui_common.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus

// Maximum number of simultaneous touch points
static constexpr int MAX_TOUCH_POINTS = 5;

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
    // touchId: unique identifier for this touch point (0-4)
    // x, y are relative to screen (0,0 is top-left)
    // pressed: true for touch start, false for release
    virtual void onTouch(int touchId, int x, int y, bool pressed) = 0;

    // Called while a touch that started in the content area is dragged.
    // Screens that only care about taps can ignore it.
    virtual void onTouchMove(int touchId, int x, int y) { (void)touchId; (void)x; (void)y; }

    // Called for center nav button press (screen-specific action)
    virtual void onNavCenter() {}

    // Get screen title for status bar
    virtual const char* getTitle() = 0;

    // Get center nav button label (if any)
    virtual const char* getNavCenterLabel() { return nullptr; }

    // Screen-specific argument of a switch to this screen (UI.show(:knobs,
    // bank: 2) -> the bank). Called before enter(), and on its own when the
    // screen is already showing. -1 is never passed.
    virtual void onShowArg(int arg) { (void)arg; }

    // One short line of state for this screen's Home tile ("120 BPM",
    // "Bank A"). Empty for none. Called on the UI task.
    virtual void getTileStatus(char* buf, int len) { if (len > 0) buf[0] = '\0'; }
};

// One entry of the Home menu: a screen, plus which layout for UI_SCREEN_LAYOUT
// (each script-defined layout is a tile of its own).
struct MenuItem {
    ui_screen_index_t screen;
    int arg;          // Layout slot, or -1
    uint8_t group;    // MENU_GROUP_*
};

enum {
    MENU_GROUP_PERFORM = 0,
    MENU_GROUP_STATE,
    MENU_GROUP_SYSTEM,
};

// Upper bound of the menu: the fixed screens plus every layout slot
static constexpr int MENU_MAX_ITEMS = 16;

// UIManager singleton class
class UIManager {
public:
    static UIManager& getInstance();

    // Initialize the UI system
    esp_err_t init();

    // Main update loop (call from app main loop)
    void update();

    // Screen navigation. Everything that actually switches runs on the UI
    // task: other tasks go through requestScreen(), which update() applies.
    void setScreen(ui_screen_index_t index, int arg = -1,
                   ui_screen_source_t source = UI_SCREEN_SOURCE_TOUCH);
    void goHome();
    void goBack();
    ui_screen_index_t getCurrentScreenIndex() const;
    int getCurrentArg() const;   // Layout slot while on UI_SCREEN_LAYOUT, else -1
    Screen* getCurrentScreen();
    Screen* getScreen(ui_screen_index_t index);

    // Any task. False (and nothing queued) when the screen does not exist or
    // is hidden from the menu.
    bool requestScreen(ui_screen_index_t index, int arg);
    // Any task. Resolve Back / the menu-order neighbour now and queue it.
    bool requestBack();
    bool requestStep(int dir);
    // The screen a pending request will switch to, else the current one.
    ui_screen_index_t getRequestedScreen(int* arg) const;

    // Home menu. hidden is a bitmask of ui_screen_index_t; Home and Scripts
    // can never be hidden.
    void setMenuHidden(uint32_t hidden);
    uint32_t getMenuHidden() const;
    bool isAvailable(ui_screen_index_t index, int arg) const;
    int buildMenu(MenuItem* out, int max) const;   // Visible items, menu order

    // Script ended: put the menu back to its defaults. Any task.
    void scriptCleanup();

    // BPM management
    void setBpm(float bpm);
    float getBpm() const;
    void setExternalBpm(float bpm);
    float getExternalBpm() const;
    void setExternalBpmSource(midi_interface_t source, float bpm);  // Source-specific BPM update
    float getExternalBpmBySource(midi_interface_t source) const;    // Get BPM by source
    void setExternalBpmSourceSelection(midi_interface_t source);    // Select which source to display
    midi_interface_t getExternalBpmSourceSelection() const;         // Get currently selected source
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
    void applyPendingRequest();
    bool resolveStep(int dir, ui_screen_index_t& index, int& arg) const;
    bool canGoBack() const;

    Screen* m_screens[UI_SCREEN_COUNT];
    int m_currentIndex;
    int m_currentArg;       // Layout slot shown while on UI_SCREEN_LAYOUT
    // Where Back goes: the last screen left for anything but Home, so Back
    // flips between the two most recent screens.
    volatile int m_previousIndex;
    volatile int m_previousArg;
    volatile uint32_t m_menuHidden;

    // Switch requested from another task, applied at the top of update().
    // One slot: a newer request replaces one not yet applied.
    portMUX_TYPE m_requestMux;
    volatile int m_pendingIndex;   // -1 when none
    volatile int m_pendingArg;
    bool m_initialized;
    bool m_needsRedraw;

    // BPM state
    float m_internalBpm;
    float m_externalBpm;
    float m_externalBpmBySource[3];  // USB, DIN, BLE
    midi_interface_t m_selectedExternalBpmSource;
    bool m_syncMode;
    bpm_change_cb_t m_bpmChangeCallback;

    // Beat tracking
    uint32_t m_bar;
    uint8_t m_beat;
    uint8_t m_beatProgress;

    // Pad callback
    pad_event_cb_t m_padEventCallback;

    // Multi-touch state tracking
    struct TouchState {
        bool isPressed;
        bool inContent;   // Press landed in the content area, so moves and the
                          // release belong to the screen even if the finger
                          // later strays onto the nav bar
        int x;
        int y;
    };
    TouchState m_touchStates[MAX_TOUCH_POINTS];
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
void ui_set_external_bpm_source(midi_interface_t source, float bpm);
float ui_get_external_bpm_by_source(midi_interface_t source);
void ui_set_external_bpm_source_selection(midi_interface_t source);
midi_interface_t ui_get_external_bpm_source_selection(void);
void ui_set_sync_mode(bool enabled);
bool ui_get_sync_mode(void);
void ui_set_bpm_change_callback(bpm_change_cb_t cb);

// Beat tracking
void ui_set_bar_beat(uint32_t bar, uint8_t beat);
void ui_set_beat_progress(uint8_t progress);

// Screen control. ui_set_screen and ui_request_screen queue the switch for
// the UI task and may be called from any task.
void ui_set_screen(ui_screen_index_t index);
ui_screen_index_t ui_get_current_screen(void);
bool ui_request_screen(ui_screen_index_t index, int arg);
bool ui_request_back(void);
bool ui_request_step(int dir);           // +1 next / -1 previous, in menu order
int  ui_get_requested_screen(int* arg);  // Pending target, else the current one
void ui_menu_set_hidden(uint32_t hidden);
uint32_t ui_menu_get_hidden(void);
// A script ended: menu back to defaults, its layouts dropped. Any task.
void ui_script_cleanup(void);

// Pad callback
void ui_set_pad_event_callback(pad_event_cb_t cb);

// Request screen redraw
void ui_request_redraw(void);

#ifdef __cplusplus
}
#endif

#endif // UI_MANAGER_H
