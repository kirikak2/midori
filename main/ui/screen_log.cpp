#include "screen_log.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "SCREEN_LOG";

// Global instance
static ScreenLog s_screenLog;

ScreenLog& getScreenLog()
{
    return s_screenLog;
}

// Mutex for thread safety
static portMUX_TYPE s_log_mutex = portMUX_INITIALIZER_UNLOCKED;

ScreenLog::ScreenLog()
    : m_logHead(0)
    , m_logCount(0)
    , m_scrollOffset(0)
    , m_needsRedraw(false)
    , m_isActive(false)
{
    memset(m_logBuffer, 0, sizeof(m_logBuffer));
}

void ScreenLog::enter()
{
    m_isActive = true;
    m_scrollOffset = 0;  // Reset to bottom (newest)
    ui_clear_content_area();
    draw();
}

void ScreenLog::leave()
{
    m_isActive = false;
}

void ScreenLog::update()
{
    if (!m_isActive) return;

    // Check needsRedraw with synchronization
    portENTER_CRITICAL(&s_log_mutex);
    bool shouldRedraw = m_needsRedraw;
    m_needsRedraw = false;
    portEXIT_CRITICAL(&s_log_mutex);

    if (shouldRedraw) {
        draw();
    }
}

void ScreenLog::draw()
{
    ui_clear_content_area();
    drawLogLines();
}

void ScreenLog::drawLogLines()
{
    portENTER_CRITICAL(&s_log_mutex);

    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    M5.Lcd.setTextSize(1);

    int y = UI_CONTENT_Y + 2;
    int lineHeight = 16;

    for (int i = 0; i < VISIBLE_LINES && i < m_logCount; i++) {
        int lineIndex = getDisplayLine(i);
        if (lineIndex >= 0) {
            M5.Lcd.setCursor(4, y);
            M5.Lcd.print(m_logBuffer[lineIndex]);
        }
        y += lineHeight;
    }

    portEXIT_CRITICAL(&s_log_mutex);
}

int ScreenLog::getDisplayLine(int displayIndex)
{
    // Display lines from oldest to newest, with scroll offset
    // displayIndex 0 = top of screen
    // m_scrollOffset 0 = show newest at bottom

    if (m_logCount == 0) return -1;

    int visibleStart;
    if (m_logCount <= VISIBLE_LINES) {
        // Not enough logs to fill screen
        visibleStart = 0;
    } else {
        // Calculate start position based on scroll
        visibleStart = m_logCount - VISIBLE_LINES - m_scrollOffset;
        if (visibleStart < 0) visibleStart = 0;
    }

    int logIndex = visibleStart + displayIndex;
    if (logIndex >= m_logCount) return -1;

    // Convert to ring buffer index
    int bufferIndex = (m_logHead - m_logCount + logIndex + MAX_LOG_LINES) % MAX_LOG_LINES;
    return bufferIndex;
}

void ScreenLog::onTouch(int x, int y, bool pressed)
{
    if (!pressed) return;

    // Touch in upper half = scroll up (older)
    // Touch in lower half = scroll down (newer)
    int contentMid = UI_CONTENT_Y + UI_CONTENT_HEIGHT / 2;

    if (y < contentMid) {
        // Scroll up (show older logs)
        if (m_logCount > VISIBLE_LINES && m_scrollOffset < m_logCount - VISIBLE_LINES) {
            m_scrollOffset++;
            m_needsRedraw = true;
        }
    } else {
        // Scroll down (show newer logs)
        if (m_scrollOffset > 0) {
            m_scrollOffset--;
            m_needsRedraw = true;
        }
    }
}

void ScreenLog::onNavCenter()
{
    // Clear logs
    clearLogs();
}

const char* ScreenLog::getTitle()
{
    return "Logs";
}

const char* ScreenLog::getNavCenterLabel()
{
    return "Clear";
}

void ScreenLog::addLog(const char* text)
{
    if (!text || !text[0]) return;

    portENTER_CRITICAL(&s_log_mutex);

    // Strip ANSI escape codes into a larger buffer
    static constexpr int STRIPPED_BUF_SIZE = 256;
    char stripped[STRIPPED_BUF_SIZE];
    int d = 0;
    bool in_escape = false;

    for (int s = 0; text[s] && d < STRIPPED_BUF_SIZE - 1; s++) {
        if (text[s] == '\033') {
            in_escape = true;
            continue;
        }
        if (in_escape) {
            if ((text[s] >= 'A' && text[s] <= 'Z') ||
                (text[s] >= 'a' && text[s] <= 'z')) {
                in_escape = false;
            }
            continue;
        }
        // Skip control characters except space
        if (text[s] >= 32) {
            stripped[d++] = text[s];
        }
    }
    stripped[d] = '\0';

    // Skip empty lines after stripping
    if (d == 0) {
        portEXIT_CRITICAL(&s_log_mutex);
        return;
    }

    // Split into multiple lines if needed (wrap long text)
    const char* ptr = stripped;
    int remaining = d;

    while (remaining > 0) {
        int lineLen = (remaining > MAX_LINE_LENGTH - 1) ? MAX_LINE_LENGTH - 1 : remaining;

        strncpy(m_logBuffer[m_logHead], ptr, lineLen);
        m_logBuffer[m_logHead][lineLen] = '\0';

        m_logHead = (m_logHead + 1) % MAX_LOG_LINES;
        if (m_logCount < MAX_LOG_LINES) {
            m_logCount++;
        }

        ptr += lineLen;
        remaining -= lineLen;
    }

    // Auto-scroll to bottom if we were at bottom
    if (m_scrollOffset == 0) {
        m_needsRedraw = true;
    }

    portEXIT_CRITICAL(&s_log_mutex);
}

void ScreenLog::clearLogs()
{
    portENTER_CRITICAL(&s_log_mutex);
    m_logHead = 0;
    m_logCount = 0;
    m_scrollOffset = 0;
    memset(m_logBuffer, 0, sizeof(m_logBuffer));
    portEXIT_CRITICAL(&s_log_mutex);

    if (m_isActive) {
        m_needsRedraw = true;
    }
}

// C API implementation
extern "C" {

void ui_add_log(const char* text)
{
    getScreenLog().addLog(text);
}

void ui_clear_logs(void)
{
    getScreenLog().clearLogs();
}

} // extern "C"
