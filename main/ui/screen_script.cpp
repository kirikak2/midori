#include "screen_script.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "SCREEN_SCRIPT";

// Global instance
static ScreenScripts s_screenScripts;

ScreenScripts& getScreenScripts()
{
    return s_screenScripts;
}

// Layout constants
static constexpr int LIST_START_Y = UI_CONTENT_Y + 5;
static constexpr int LIST_MARGIN = 8;

ScreenScripts::ScreenScripts()
    : m_scriptCount(0)
    , m_selectedIndex(-1)
    , m_scrollOffset(0)
    , m_isActive(false)
    , m_needsRedraw(false)
{
    memset(m_scripts, 0, sizeof(m_scripts));
    m_currentScript[0] = '\0';

    // Add default script for testing
    addScript("app.rb");
}

void ScreenScripts::enter()
{
    m_isActive = true;
    ui_clear_content_area();
    draw();
}

void ScreenScripts::leave()
{
    m_isActive = false;
}

void ScreenScripts::update()
{
    if (!m_isActive) return;

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

void ScreenScripts::draw()
{
    drawScriptList();
}

void ScreenScripts::drawScriptList()
{
    int y = LIST_START_Y;

    // Draw visible items
    for (int i = 0; i < VISIBLE_ITEMS && (i + m_scrollOffset) < m_scriptCount; i++) {
        int scriptIndex = i + m_scrollOffset;
        drawScriptItem(scriptIndex, y);
        y += ITEM_HEIGHT;
    }

    // If no scripts, show message
    if (m_scriptCount == 0) {
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
        M5.Lcd.setCursor(60, LIST_START_Y + 60);
        M5.Lcd.print("No scripts found on SD card");
    }
}

void ScreenScripts::drawScriptItem(int index, int y)
{
    if (index < 0 || index >= m_scriptCount) return;

    int x = LIST_MARGIN;
    int w = UI_SCREEN_WIDTH - 2 * LIST_MARGIN;
    int h = ITEM_HEIGHT - 2;

    const char* filename = m_scripts[index];
    bool isSelected = (index == m_selectedIndex);
    bool isRunning = (strcmp(filename, m_currentScript) == 0);

    // Background color
    uint16_t bgColor = UI_COLOR_BLACK;
    if (isSelected) {
        bgColor = UI_COLOR_NAVY;
    }

    // Draw background
    M5.Lcd.fillRect(x, y, w, h, bgColor);

    // Draw running indicator
    M5.Lcd.setTextSize(1);
    if (isRunning) {
        M5.Lcd.setTextColor(UI_COLOR_GREEN, bgColor);
        M5.Lcd.setCursor(x + 4, y + (h - 8) / 2);
        M5.Lcd.print(">");
    }

    // Draw filename
    M5.Lcd.setTextColor(isRunning ? UI_COLOR_GREEN : UI_COLOR_WHITE, bgColor);
    M5.Lcd.setCursor(x + 16, y + (h - 8) / 2);
    M5.Lcd.print(filename);

    // Draw [Running] badge if running
    if (isRunning) {
        int badgeX = UI_SCREEN_WIDTH - LIST_MARGIN - 60;
        M5.Lcd.fillRoundRect(badgeX, y + 2, 55, h - 4, 3, UI_COLOR_GREEN);
        M5.Lcd.setTextColor(UI_COLOR_BLACK, UI_COLOR_GREEN);
        M5.Lcd.setCursor(badgeX + 4, y + (h - 8) / 2);
        M5.Lcd.print("Running");
    }
}

int ScreenScripts::hitTestItem(int y)
{
    if (y < LIST_START_Y) return -1;

    int relY = y - LIST_START_Y;
    int itemIndex = relY / ITEM_HEIGHT + m_scrollOffset;

    if (itemIndex >= 0 && itemIndex < m_scriptCount) {
        return itemIndex;
    }
    return -1;
}

void ScreenScripts::onTouch(int x, int y, bool pressed)
{
    if (!pressed) return;

    int itemIndex = hitTestItem(y);
    if (itemIndex >= 0) {
        m_selectedIndex = itemIndex;
        m_needsRedraw = true;
    }
}

void ScreenScripts::onNavCenter()
{
    // Run selected script
    if (m_selectedIndex >= 0 && m_selectedIndex < m_scriptCount) {
        const char* filename = m_scripts[m_selectedIndex];

        // Don't run if already running
        if (strcmp(filename, m_currentScript) == 0) {
            ESP_LOGI(TAG, "Script %s is already running", filename);
            return;
        }

        ESP_LOGI(TAG, "Requesting to run script: %s", filename);

        // TODO: Send script change request to PicoRuby task
        // For now, just update the display
        setCurrentScript(filename);
    }
}

const char* ScreenScripts::getTitle()
{
    return "Scripts";
}

const char* ScreenScripts::getNavCenterLabel()
{
    return "Run";
}

void ScreenScripts::setCurrentScript(const char* filename)
{
    if (filename) {
        strncpy(m_currentScript, filename, MAX_FILENAME_LEN - 1);
        m_currentScript[MAX_FILENAME_LEN - 1] = '\0';
    } else {
        m_currentScript[0] = '\0';
    }

    if (m_isActive) {
        m_needsRedraw = true;
    }
}

void ScreenScripts::addScript(const char* filename)
{
    if (m_scriptCount >= MAX_SCRIPTS) return;

    strncpy(m_scripts[m_scriptCount], filename, MAX_FILENAME_LEN - 1);
    m_scripts[m_scriptCount][MAX_FILENAME_LEN - 1] = '\0';
    m_scriptCount++;

    if (m_isActive) {
        m_needsRedraw = true;
    }
}

void ScreenScripts::clearScripts()
{
    m_scriptCount = 0;
    m_selectedIndex = -1;
    m_scrollOffset = 0;
    memset(m_scripts, 0, sizeof(m_scripts));

    if (m_isActive) {
        m_needsRedraw = true;
    }
}

// C API implementation
extern "C" {

void ui_script_set_current(const char* filename)
{
    getScreenScripts().setCurrentScript(filename);
}

void ui_script_add(const char* filename)
{
    getScreenScripts().addScript(filename);
}

void ui_script_clear_list(void)
{
    getScreenScripts().clearScripts();
}

} // extern "C"
