#ifndef SCREEN_LOG_H
#define SCREEN_LOG_H

#include "ui_manager.h"

#ifdef __cplusplus

class ScreenLog : public Screen {
public:
    ScreenLog();
    ~ScreenLog() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int x, int y, bool pressed) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

    // Add a log line
    void addLog(const char* text);

    // Clear all logs
    void clearLogs();

private:
    static constexpr int MAX_LOG_LINES = 100;
    static constexpr int MAX_LINE_LENGTH = 54;
    static constexpr int VISIBLE_LINES = 11;  // Lines visible in content area

    char m_logBuffer[MAX_LOG_LINES][MAX_LINE_LENGTH];
    int m_logHead;      // Next write position
    int m_logCount;     // Total lines in buffer
    int m_scrollOffset; // Scroll position (0 = bottom/newest)
    bool m_needsRedraw;
    bool m_isActive;

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
