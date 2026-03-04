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
    void onTouch(int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Set current running script
    void setCurrentScript(const char* filename);

    // Add a script to the list
    void addScript(const char* filename);

    // Clear the script list
    void clearScripts();

private:
    static constexpr int MAX_SCRIPTS = 20;
    static constexpr int MAX_FILENAME_LEN = 32;
    static constexpr int VISIBLE_ITEMS = 7;
    static constexpr int ITEM_HEIGHT = 24;

    char m_scripts[MAX_SCRIPTS][MAX_FILENAME_LEN];
    int m_scriptCount;
    int m_selectedIndex;
    int m_scrollOffset;
    char m_currentScript[MAX_FILENAME_LEN];
    bool m_isActive;
    bool m_needsRedraw;

    void drawScriptList();
    void drawScriptItem(int index, int y);
    int hitTestItem(int y);
};

// Global instance
ScreenScripts& getScreenScripts();

extern "C" {
#endif

// C API
void ui_script_set_current(const char* filename);
void ui_script_add(const char* filename);
void ui_script_clear_list(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_SCRIPT_H
