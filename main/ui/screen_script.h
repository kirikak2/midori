#ifndef SCREEN_SCRIPT_H
#define SCREEN_SCRIPT_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenScripts : public Screen {
public:
    ScreenScripts();
    ~ScreenScripts() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Set current running script
    void setCurrentScript(const char* filename);

    // Add a script to the list
    void addScript(const char* filename);

    // Clear the script list
    void clearScripts();

    // Refresh script list from SD card
    void refreshFromSD();

private:
    static constexpr int MAX_SCRIPTS = 20;
    static constexpr int MAX_FILENAME_LEN = 32;

    // Buttons in the bottom row of the content area
    enum ButtonId {
        BUTTON_REFRESH = 0,
        BUTTON_STOP,
        BUTTON_COUNT,
    };

    char m_scripts[MAX_SCRIPTS][MAX_FILENAME_LEN];
    int m_scriptCount;
    int m_selectedIndex;
    int m_scrollOffset;
    char m_currentScript[MAX_FILENAME_LEN];
    bool m_isActive;
    bool m_needsRedraw;
    bool m_refreshButtonPressed;
    bool m_stopButtonPressed;
    bool m_scriptRunning;      // Mirrors the supervisor's script-mode state
    uint32_t m_scriptListVersion;

    // Touch tracking for drag-to-scroll
    int m_pressStartY;
    int m_pressStartScrollOffset;
    bool m_pressInList;

    int visibleItems() const;
    int maxScrollOffset() const;
    void clampScrollOffset();
    void ensureSelectedVisible();

    void drawScriptList();
    void drawScriptItem(int index, int y);
    void drawButtons();
    void drawScrollIndicator();
    int hitTestItem(int y);

    // Geometry of the bottom-row buttons (shared by drawing and hit testing)
    void buttonRect(ButtonId id, int* x, int* y, int* w, int* h) const;
    int hitTestButton(int x, int y) const;

    // Pick up the supervisor's script-mode state (running script / none)
    void syncRunningState();
    void requestStop();
};

// Global instance
ScreenScripts& getScreenScripts();

extern "C" {
#endif

// C API
void ui_script_set_current(const char* filename);
void ui_script_add(const char* filename);
void ui_script_clear_list(void);
void ui_script_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_SCRIPT_H
