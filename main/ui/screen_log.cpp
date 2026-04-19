#include "sdkconfig.h"
#include "screen_log.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char* TAG = "SCREEN_LOG";

// Global instance
static ScreenLog s_screenLog;

ScreenLog& getScreenLog()
{
    return s_screenLog;
}

// Mutex for thread safety
static portMUX_TYPE s_log_mutex = portMUX_INITIALIZER_UNLOCKED;

// Board-specific text size and line settings
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
static constexpr int LOG_TEXT_SIZE = 2;
static constexpr int LOG_LINE_HEIGHT = 24;
static constexpr int LOG_LEFT_MARGIN = 10;
// Batch sprite size: 5 lines * 24px * 1280px * 2 bytes = 300KB (in internal RAM)
static constexpr int LOG_BATCH_LINES = 5;
#else
static constexpr int LOG_TEXT_SIZE = 1;
static constexpr int LOG_LINE_HEIGHT = 16;
static constexpr int LOG_LEFT_MARGIN = 4;
// Batch sprite size: 4 lines * 16px * 320px * 2 bytes = 40KB
static constexpr int LOG_BATCH_LINES = 4;
#endif

ScreenLog::ScreenLog()
    : m_logBuffer(nullptr)
    , m_logHead(0)
    , m_logCount(0)
    , m_scrollOffset(0)
    , m_needsRedraw(false)
    , m_isActive(false)
    , m_sprite(nullptr)
{
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
    // Allocate log buffer from PSRAM on Tab5 (11KB buffer)
    m_logBuffer = (char (*)[MAX_LINE_LENGTH])heap_caps_malloc(
        MAX_LOG_LINES * MAX_LINE_LENGTH,
        MALLOC_CAP_SPIRAM
    );
    if (!m_logBuffer) {
        ESP_LOGE(TAG, "Failed to allocate log buffer from PSRAM, trying internal RAM");
        m_logBuffer = (char (*)[MAX_LINE_LENGTH])malloc(MAX_LOG_LINES * MAX_LINE_LENGTH);
    }
#else
    // Use regular malloc for CoreS3 (smaller buffer)
    m_logBuffer = (char (*)[MAX_LINE_LENGTH])malloc(MAX_LOG_LINES * MAX_LINE_LENGTH);
#endif

    if (m_logBuffer) {
        memset(m_logBuffer, 0, MAX_LOG_LINES * MAX_LINE_LENGTH);
        ESP_LOGI(TAG, "Log buffer allocated: %d lines x %d chars = %d bytes",
                 MAX_LOG_LINES, MAX_LINE_LENGTH, MAX_LOG_LINES * MAX_LINE_LENGTH);
    } else {
        ESP_LOGE(TAG, "Failed to allocate log buffer!");
    }
}

ScreenLog::~ScreenLog()
{
    if (m_logBuffer) {
        free(m_logBuffer);
        m_logBuffer = nullptr;
    }
    if (m_sprite) {
        m_sprite->deleteSprite();
        delete m_sprite;
        m_sprite = nullptr;
    }
}

void ScreenLog::enter()
{
    m_isActive = true;
    m_scrollOffset = 0;  // Reset to bottom (newest)

    // Lazy-create batch sprite in internal RAM for faster rendering
    // Internal RAM is ~50-100x faster than PSRAM for pixel operations
    if (!m_sprite) {
        int batchHeight = LOG_BATCH_LINES * LOG_LINE_HEIGHT;
        m_sprite = new LGFX_Sprite(&M5.Lcd);
        m_sprite->setColorDepth(16);
        m_sprite->setPsram(false);  // Use internal RAM for speed
        void* result = m_sprite->createSprite(UI_SCREEN_WIDTH, batchHeight);
        if (!result) {
            ESP_LOGW(TAG, "Failed to create Log batch sprite in internal RAM, trying PSRAM");
            m_sprite->setPsram(true);
            result = m_sprite->createSprite(UI_SCREEN_WIDTH, batchHeight);
        }
        if (!result) {
            ESP_LOGW(TAG, "Failed to create Log sprite");
            delete m_sprite;
            m_sprite = nullptr;
        } else {
            ESP_LOGI(TAG, "Log batch sprite created: %dx%d", UI_SCREEN_WIDTH, batchHeight);
        }
    }

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
    // Don't clear entire content area - just overwrite with new log lines
    // enter() already clears the area when switching screens
    drawLogLines();
}

void ScreenLog::drawLogLines()
{
    if (!m_logBuffer) return;

    // Step 1: Take a quick snapshot of log lines inside critical section
    // (keep critical section as short as possible - no rendering here)
    // Use static buffer to avoid stack overflow (2750 bytes on Tab5)
    // Safe because drawLogLines is only called from main task
    static char snapshot[VISIBLE_LINES][MAX_LINE_LENGTH];

    portENTER_CRITICAL(&s_log_mutex);
    for (int i = 0; i < VISIBLE_LINES; i++) {
        if (i < m_logCount) {
            int lineIndex = getDisplayLine(i);
            if (lineIndex >= 0) {
                strncpy(snapshot[i], m_logBuffer[lineIndex], MAX_LINE_LENGTH);
                snapshot[i][MAX_LINE_LENGTH - 1] = '\0';
            } else {
                snapshot[i][0] = '\0';
            }
        } else {
            snapshot[i][0] = '\0';
        }
    }
    portEXIT_CRITICAL(&s_log_mutex);

    // Step 2: Render to sprite in batches (outside critical section)
    if (m_sprite) {
        // Batch rendering: process LOG_BATCH_LINES lines per sprite push
        // Sprite is in internal RAM (fast), batch keeps total memory small
        m_sprite->setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
        m_sprite->setTextSize(LOG_TEXT_SIZE);

        int batchHeight = LOG_BATCH_LINES * LOG_LINE_HEIGHT;

        for (int batch = 0; batch < VISIBLE_LINES; batch += LOG_BATCH_LINES) {
            int linesInBatch = LOG_BATCH_LINES;
            if (batch + linesInBatch > VISIBLE_LINES) {
                linesInBatch = VISIBLE_LINES - batch;
            }

            // Clear sprite for this batch
            m_sprite->fillSprite(UI_COLOR_BLACK);

            // Draw lines for this batch
            for (int i = 0; i < linesInBatch; i++) {
                m_sprite->setCursor(LOG_LEFT_MARGIN, i * LOG_LINE_HEIGHT);
                m_sprite->print(snapshot[batch + i]);
            }

            // Push batch to LCD
            int lcd_y = UI_CONTENT_Y + 5 + batch * LOG_LINE_HEIGHT;
            m_sprite->pushSprite(0, lcd_y);
        }
    } else {
        // Fallback: direct LCD rendering
        M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
        M5.Lcd.setTextSize(LOG_TEXT_SIZE);

        int y = UI_CONTENT_Y + 5;
        for (int i = 0; i < VISIBLE_LINES; i++) {
            M5.Lcd.fillRect(LOG_LEFT_MARGIN, y, UI_SCREEN_WIDTH - LOG_LEFT_MARGIN, LOG_LINE_HEIGHT, UI_COLOR_BLACK);
            M5.Lcd.setCursor(LOG_LEFT_MARGIN, y);
            M5.Lcd.print(snapshot[i]);
            y += LOG_LINE_HEIGHT;
        }
    }
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

void ScreenLog::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;  // Single touch for scrolling
    (void)x;
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
    if (!text || !text[0] || !m_logBuffer) return;

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
    if (!m_logBuffer) return;

    portENTER_CRITICAL(&s_log_mutex);
    m_logHead = 0;
    m_logCount = 0;
    m_scrollOffset = 0;
    memset(m_logBuffer, 0, MAX_LOG_LINES * MAX_LINE_LENGTH);
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
