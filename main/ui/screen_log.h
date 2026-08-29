#ifndef SCREEN_LOG_H
#define SCREEN_LOG_H

#include "sdkconfig.h"
#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

class ScreenLog : public Screen {
public:
    ScreenLog();
    ~ScreenLog() override;  // Need destructor to free PSRAM buffer

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Add a log line
    void addLog(const char* text);

    // Clear all logs
    void clearLogs();

private:
    // Board-specific display settings
#if defined(UI_LAYOUT_LARGE)
    static constexpr int MAX_LOG_LINES = 100;    // Increased back to 100 with PSRAM
    static constexpr int MAX_LINE_LENGTH = 110;  // Increased back to 110 with PSRAM
    // Lines visible in the content area, at the 24px LOG_LINE_HEIGHT that
    // screen_log.cpp uses on these boards (Tab5 620px -> 25, CrowPanel 500px
    // -> 20).
    static constexpr int VISIBLE_LINES = UI_CONTENT_HEIGHT / 24;
#else
    static constexpr int MAX_LOG_LINES = 100;
    static constexpr int MAX_LINE_LENGTH = 54;
    static constexpr int VISIBLE_LINES = 11;     // Lines visible in content area (180px / 16px)
#endif

    char (*m_logBuffer)[MAX_LINE_LENGTH];  // Pointer to buffer (allocated from PSRAM on Tab5)
    int m_logHead;      // Next write position
    int m_logCount;     // Total lines in buffer
    int m_scrollOffset; // Scroll position (0 = bottom/newest)
    bool m_needsRedraw;
    bool m_isActive;
    LGFX_Sprite* m_sprite;  // Full content area sprite for double-buffered rendering

    void drawLogLines();
    int getDisplayLine(int displayIndex);
};

// Global instance
ScreenLog& getScreenLog();

extern "C" {
#endif

// C API for adding logs
void ui_add_log(const char* text);
void ui_clear_logs(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_LOG_H
