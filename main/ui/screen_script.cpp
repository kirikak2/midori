#include "sdkconfig.h"
#include "screen_script.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

extern "C" {
#include "../../components/picoruby-esp32/picoruby-esp32.h"
}

static const char* TAG = "SCREEN_SCRIPT";

// Global instance
static ScreenScripts s_screenScripts;

ScreenScripts& getScreenScripts()
{
    return s_screenScripts;
}

// Layout constants - Board-specific
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
static constexpr int LIST_START_Y = UI_CONTENT_Y + 10;
static constexpr int LIST_MARGIN = 15;
static constexpr int ITEM_TEXT_SIZE = 2;
static constexpr int REFRESH_BUTTON_HEIGHT = 50;
static constexpr int REFRESH_BUTTON_WIDTH = 180;
static constexpr int REFRESH_BUTTON_MARGIN = 10;
#else
static constexpr int LIST_START_Y = UI_CONTENT_Y + 5;
static constexpr int LIST_MARGIN = 8;
static constexpr int ITEM_TEXT_SIZE = 1;
static constexpr int REFRESH_BUTTON_HEIGHT = 30;
static constexpr int REFRESH_BUTTON_WIDTH = 100;
static constexpr int REFRESH_BUTTON_MARGIN = 5;
#endif

ScreenScripts::ScreenScripts()
    : m_scriptCount(0)
    , m_selectedIndex(-1)
    , m_scrollOffset(0)
    , m_isActive(false)
    , m_needsRedraw(false)
    , m_refreshButtonPressed(false)
    , m_scriptListVersion(0)
{
    memset(m_scripts, 0, sizeof(m_scripts));
    m_currentScript[0] = '\0';
}

void ScreenScripts::enter()
{
    m_isActive = true;

    // Load current script list if Ruby has already populated it
    uint32_t currentVersion = picoruby_esp32_get_script_list_version();
    if (currentVersion != m_scriptListVersion && picoruby_esp32_script_list_ready()) {
        m_scriptListVersion = currentVersion;
        clearScripts();
        int count = picoruby_esp32_get_script_count();
        for (int i = 0; i < count && m_scriptCount < MAX_SCRIPTS; i++) {
            const char* name = picoruby_esp32_get_script_name(i);
            if (name != NULL) {
                addScript(name);
            }
        }
        if (m_scriptCount > 0 && m_selectedIndex < 0) {
            m_selectedIndex = 0;
        }
    }

    draw();
    m_needsRedraw = false;
}

void ScreenScripts::leave()
{
    m_isActive = false;
}

void ScreenScripts::update()
{
    if (!m_isActive) return;

    // Reload list if Ruby has updated the script list since we last saw it
    uint32_t currentVersion = picoruby_esp32_get_script_list_version();
    if (currentVersion != m_scriptListVersion && picoruby_esp32_script_list_ready()) {
        m_scriptListVersion = currentVersion;
        clearScripts();
        int count = picoruby_esp32_get_script_count();
        for (int i = 0; i < count && m_scriptCount < MAX_SCRIPTS; i++) {
            const char* name = picoruby_esp32_get_script_name(i);
            if (name != NULL) {
                addScript(name);
            }
        }
        ESP_LOGI(TAG, "Script list updated: %d script(s) (version %lu)", m_scriptCount, (unsigned long)currentVersion);
        if (m_scriptCount > 0 && m_selectedIndex < 0) {
            m_selectedIndex = 0;
        }
        m_needsRedraw = true;
    }

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

void ScreenScripts::draw()
{
    ui_clear_content_area();
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
        M5.Lcd.setTextSize(ITEM_TEXT_SIZE);
        M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
        int textWidth = strlen("No scripts found on SD card") * 6 * ITEM_TEXT_SIZE;
        M5.Lcd.setCursor((UI_SCREEN_WIDTH - textWidth) / 2, LIST_START_Y + 100);
        M5.Lcd.print("No scripts found on SD card");
    }

    // Draw refresh button at the bottom
    drawRefreshButton();
}

void ScreenScripts::drawRefreshButton()
{
    int buttonY = UI_CONTENT_Y + UI_CONTENT_HEIGHT - REFRESH_BUTTON_HEIGHT - REFRESH_BUTTON_MARGIN;
    int buttonX = (UI_SCREEN_WIDTH - REFRESH_BUTTON_WIDTH) / 2;  // Center horizontally

    ui_draw_button(buttonX, buttonY, REFRESH_BUTTON_WIDTH, REFRESH_BUTTON_HEIGHT, "Refresh",
                   UI_COLOR_BLUE, UI_COLOR_WHITE, m_refreshButtonPressed);
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

    // Draw running indicator and filename
    M5.Lcd.setTextSize(ITEM_TEXT_SIZE);
    int textHeight = 8 * ITEM_TEXT_SIZE;
    int textY = y + (h - textHeight) / 2;

    if (isRunning) {
        M5.Lcd.setTextColor(UI_COLOR_GREEN, bgColor);
        M5.Lcd.setCursor(x + 4 * ITEM_TEXT_SIZE, textY);
        M5.Lcd.print(">");
    }

    // Draw filename
    M5.Lcd.setTextColor(isRunning ? UI_COLOR_GREEN : UI_COLOR_WHITE, bgColor);
    M5.Lcd.setCursor(x + 16 * ITEM_TEXT_SIZE, textY);
    M5.Lcd.print(filename);

    // Draw [Running] badge if running
    if (isRunning) {
        int badgeWidth = 55 * ITEM_TEXT_SIZE;
        int badgeX = UI_SCREEN_WIDTH - LIST_MARGIN - badgeWidth;
        M5.Lcd.fillRoundRect(badgeX, y + 2, badgeWidth, h - 4, 3, UI_COLOR_GREEN);
        M5.Lcd.setTextColor(UI_COLOR_BLACK, UI_COLOR_GREEN);
        M5.Lcd.setCursor(badgeX + 4 * ITEM_TEXT_SIZE, textY);
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

void ScreenScripts::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;  // Single touch for script selection
    // Check if refresh button was touched
    int buttonY = UI_CONTENT_Y + UI_CONTENT_HEIGHT - REFRESH_BUTTON_HEIGHT - REFRESH_BUTTON_MARGIN;
    int buttonX = (UI_SCREEN_WIDTH - 100) / 2;
    int buttonW = 100;
    int buttonH = REFRESH_BUTTON_HEIGHT;

    if (x >= buttonX && x <= buttonX + buttonW &&
        y >= buttonY && y <= buttonY + buttonH) {
        if (pressed) {
            m_refreshButtonPressed = true;
            m_needsRedraw = true;
        } else {
            // Release - trigger refresh
            if (m_refreshButtonPressed) {
                ESP_LOGI(TAG, "Refresh button clicked");
                refreshFromSD();
                m_refreshButtonPressed = false;
                m_needsRedraw = true;
            }
        }
        return;
    }

    // Reset button state if touch is outside button
    if (!pressed && m_refreshButtonPressed) {
        m_refreshButtonPressed = false;
        m_needsRedraw = true;
    }

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

        // Allow re-running the same script (will cleanup VM and restart)
        ESP_LOGI(TAG, "Requesting to run script: %s", filename);

        // Build full path
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 CONFIG_USB_MIDI_SDCARD_MOUNT_POINT, filename);

        // Request script change from PicoRuby task
        if (picoruby_esp32_request_script_change(full_path)) {
            ESP_LOGI(TAG, "Script change request sent: %s", full_path);
            setCurrentScript(filename);
        } else {
            ESP_LOGE(TAG, "Failed to request script change");
        }
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

void ScreenScripts::refreshFromSD()
{
#ifdef CONFIG_USB_MIDI_SDCARD_ENABLED
    // Request Ruby to re-scan the SD card. update() will detect the new
    // version when notify_scripts_to_c completes.
    picoruby_esp32_request_sd_refresh();
    ESP_LOGI(TAG, "SD card refresh requested (version %lu)", (unsigned long)picoruby_esp32_get_script_list_version());
#else
    ESP_LOGW(TAG, "SD card support not enabled");
    addScript("app.rb");
    m_selectedIndex = 0;
#endif
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

void ui_script_refresh(void)
{
    getScreenScripts().refreshFromSD();
}

} // extern "C"
