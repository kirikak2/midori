#include "sdkconfig.h"
#include "screen_script.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

extern "C" {
#include "../../components/picoruby-esp32/picoruby-esp32.h"
#include "../../components/picoruby-esp32/picoruby_supervisor.h"
}

static const char* TAG = "SCREEN_SCRIPT";

// Global instance
static ScreenScripts s_screenScripts;

ScreenScripts& getScreenScripts()
{
    return s_screenScripts;
}

// Layout constants - Board-specific
#if defined(UI_LAYOUT_LARGE)
static constexpr int LIST_START_Y = UI_CONTENT_Y + 10;
static constexpr int LIST_MARGIN = 15;
static constexpr int ITEM_TEXT_SIZE = 2;
static constexpr int ITEM_HEIGHT = 40;
static constexpr int BUTTON_HEIGHT = 50;
static constexpr int BUTTON_WIDTH = 180;
static constexpr int BUTTON_MARGIN = 10;
static constexpr int BUTTON_GAP = 20;
#else
static constexpr int LIST_START_Y = UI_CONTENT_Y + 5;
static constexpr int LIST_MARGIN = 8;
static constexpr int ITEM_TEXT_SIZE = 1;
static constexpr int ITEM_HEIGHT = 20;
static constexpr int BUTTON_HEIGHT = 30;
static constexpr int BUTTON_WIDTH = 100;
static constexpr int BUTTON_MARGIN = 5;
static constexpr int BUTTON_GAP = 10;
#endif

// Vertical area available for list items (between LIST_START_Y and the button row)
static constexpr int LIST_BOTTOM_PADDING = 4;
static constexpr int LIST_AREA_HEIGHT =
    UI_CONTENT_HEIGHT - (LIST_START_Y - UI_CONTENT_Y)
    - BUTTON_HEIGHT - BUTTON_MARGIN - LIST_BOTTOM_PADDING;

ScreenScripts::ScreenScripts()
    : m_scriptCount(0)
    , m_selectedIndex(-1)
    , m_scrollOffset(0)
    , m_isActive(false)
    , m_needsRedraw(false)
    , m_refreshButtonPressed(false)
    , m_stopButtonPressed(false)
    , m_scriptRunning(false)
    , m_scriptListVersion(0)
    , m_pressStartY(0)
    , m_pressStartScrollOffset(0)
    , m_pressInList(false)
{
    memset(m_scripts, 0, sizeof(m_scripts));
    m_currentScript[0] = '\0';
}

int ScreenScripts::visibleItems() const
{
    int n = LIST_AREA_HEIGHT / ITEM_HEIGHT;
    if (n < 1) n = 1;
    return n;
}

int ScreenScripts::maxScrollOffset() const
{
    int excess = m_scriptCount - visibleItems();
    return excess > 0 ? excess : 0;
}

void ScreenScripts::clampScrollOffset()
{
    int maxOff = maxScrollOffset();
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    if (m_scrollOffset > maxOff) m_scrollOffset = maxOff;
}

void ScreenScripts::ensureSelectedVisible()
{
    if (m_selectedIndex < 0) return;
    int visible = visibleItems();
    if (m_selectedIndex < m_scrollOffset) {
        m_scrollOffset = m_selectedIndex;
    } else if (m_selectedIndex >= m_scrollOffset + visible) {
        m_scrollOffset = m_selectedIndex - visible + 1;
    }
    clampScrollOffset();
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

    syncRunningState();

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

    syncRunningState();

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

/*
 * The supervisor owns the truth about what is running: it stores the full
 * path while a script task is alive and clears it when it falls back to UI
 * mode. Mirroring it here means the [Running] badge and the Stop button
 * follow a script that ended on its own, not just one we stopped.
 */
void ScreenScripts::syncRunningState()
{
    const char* path = supervisor_get_current_script();

    char basename[MAX_FILENAME_LEN];
    basename[0] = '\0';
    if (path != nullptr && path[0] != '\0') {
        const char* slash = strrchr(path, '/');
        const char* name = slash ? slash + 1 : path;
        strncpy(basename, name, MAX_FILENAME_LEN - 1);
        basename[MAX_FILENAME_LEN - 1] = '\0';
    }

    bool running = (basename[0] != '\0');
    if (running == m_scriptRunning && strcmp(basename, m_currentScript) == 0) {
        return;
    }

    m_scriptRunning = running;
    memcpy(m_currentScript, basename, sizeof(m_currentScript));
    if (!running) {
        m_stopButtonPressed = false;
    }

    if (m_isActive) {
        m_needsRedraw = true;
    }
}

void ScreenScripts::draw()
{
    ui_clear_content_area();
    drawScriptList();
}

void ScreenScripts::drawScriptList()
{
    clampScrollOffset();
    int visible = visibleItems();
    int y = LIST_START_Y;

    // Draw visible items
    for (int i = 0; i < visible && (i + m_scrollOffset) < m_scriptCount; i++) {
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

    // Scroll indicator on the right edge when overflowing
    if (m_scriptCount > visible) {
        drawScrollIndicator();
    }

    // Draw the button row at the bottom
    drawButtons();
}

void ScreenScripts::drawScrollIndicator()
{
    int visible = visibleItems();
    int trackX = UI_SCREEN_WIDTH - LIST_MARGIN + 2;
    int trackW = 4;
    int trackY = LIST_START_Y;
    int trackH = visible * ITEM_HEIGHT;

    // Track background
    M5.Lcd.fillRect(trackX, trackY, trackW, trackH, UI_COLOR_DARKGRAY);

    // Thumb proportional to visible/total
    int thumbH = (trackH * visible) / m_scriptCount;
    if (thumbH < 8) thumbH = 8;
    int maxOff = maxScrollOffset();
    int thumbY = trackY;
    if (maxOff > 0) {
        thumbY += ((trackH - thumbH) * m_scrollOffset) / maxOff;
    }
    M5.Lcd.fillRect(trackX, thumbY, trackW, thumbH, UI_COLOR_WHITE);
}

// [ Refresh ] [ Stop ], centered as a pair in the bottom row
void ScreenScripts::buttonRect(ButtonId id, int* x, int* y, int* w, int* h) const
{
    int rowWidth = BUTTON_COUNT * BUTTON_WIDTH + (BUTTON_COUNT - 1) * BUTTON_GAP;
    int rowX = (UI_SCREEN_WIDTH - rowWidth) / 2;

    *x = rowX + (int)id * (BUTTON_WIDTH + BUTTON_GAP);
    *y = UI_CONTENT_Y + UI_CONTENT_HEIGHT - BUTTON_HEIGHT - BUTTON_MARGIN;
    *w = BUTTON_WIDTH;
    *h = BUTTON_HEIGHT;
}

int ScreenScripts::hitTestButton(int px, int py) const
{
    for (int id = 0; id < BUTTON_COUNT; id++) {
        int x, y, w, h;
        buttonRect((ButtonId)id, &x, &y, &w, &h);
        if (px >= x && px <= x + w && py >= y && py <= y + h) {
            return id;
        }
    }
    return -1;
}

void ScreenScripts::drawButtons()
{
    int x, y, w, h;

    buttonRect(BUTTON_REFRESH, &x, &y, &w, &h);
    ui_draw_button(x, y, w, h, "Refresh",
                   UI_COLOR_BLUE, UI_COLOR_WHITE, m_refreshButtonPressed);

    // Stop is greyed out while no script is running (nothing to stop)
    buttonRect(BUTTON_STOP, &x, &y, &w, &h);
    ui_draw_button(x, y, w, h, "Stop",
                   m_scriptRunning ? UI_COLOR_RED : UI_COLOR_DARKGRAY,
                   m_scriptRunning ? UI_COLOR_WHITE : UI_COLOR_GRAY,
                   m_stopButtonPressed);
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
    int slot = relY / ITEM_HEIGHT;
    if (slot >= visibleItems()) return -1;  // tapped below visible rows

    int itemIndex = slot + m_scrollOffset;
    if (itemIndex >= 0 && itemIndex < m_scriptCount) {
        return itemIndex;
    }
    return -1;
}

void ScreenScripts::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;  // Single touch for script selection

    int button = hitTestButton(x, y);
    if (button >= 0) {
        if (pressed) {
            if (button == BUTTON_REFRESH) {
                m_refreshButtonPressed = true;
                m_needsRedraw = true;
            } else if (button == BUTTON_STOP && m_scriptRunning) {
                m_stopButtonPressed = true;
                m_needsRedraw = true;
            }
        } else {
            if (button == BUTTON_REFRESH && m_refreshButtonPressed) {
                ESP_LOGI(TAG, "Refresh button clicked");
                refreshFromSD();
                m_refreshButtonPressed = false;
                m_needsRedraw = true;
            } else if (button == BUTTON_STOP && m_stopButtonPressed) {
                ESP_LOGI(TAG, "Stop button clicked");
                requestStop();
                m_stopButtonPressed = false;
                m_needsRedraw = true;
            }
        }
        return;
    }

    // Reset button state if touch drifted off the button
    if (!pressed && (m_refreshButtonPressed || m_stopButtonPressed)) {
        m_refreshButtonPressed = false;
        m_stopButtonPressed = false;
        m_needsRedraw = true;
    }

    if (pressed) {
        m_pressStartY = y;
        m_pressStartScrollOffset = m_scrollOffset;
        m_pressInList = (hitTestItem(y) >= 0);
        return;
    }

    // Release: decide between drag (scroll) and tap (select)
    if (!m_pressInList) return;
    m_pressInList = false;

    int dy = y - m_pressStartY;
    int dragThreshold = ITEM_HEIGHT / 2;
    bool overflowing = m_scriptCount > visibleItems();

    if (overflowing && (dy > dragThreshold || dy < -dragThreshold)) {
        // Drag down moves content down → scrollOffset decreases
        int scrollDelta = -dy / ITEM_HEIGHT;
        m_scrollOffset = m_pressStartScrollOffset + scrollDelta;
        clampScrollOffset();
        m_needsRedraw = true;
    } else {
        int itemIndex = hitTestItem(m_pressStartY);
        if (itemIndex >= 0) {
            m_selectedIndex = itemIndex;
            m_needsRedraw = true;
        }
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

        // Request script change from PicoRuby task. The [Running] badge is
        // not set here: syncRunningState() raises it once the supervisor has
        // actually started the task, and lowers it again when it ends.
        if (picoruby_esp32_request_script_change(full_path)) {
            ESP_LOGI(TAG, "Script change request sent: %s", full_path);
        } else {
            ESP_LOGE(TAG, "Failed to request script change");
        }
    }
}

/*
 * Ask the supervisor to end the running script and fall back to UI mode.
 * Only the request is made here -- stopping the PicoRuby task, the MIDI
 * cleanup and the VM reset all happen on the supervisor task, so the UI
 * task never blocks. syncRunningState() picks up the result.
 */
void ScreenScripts::requestStop()
{
    if (!m_scriptRunning) {
        return;
    }

    if (supervisor_stop_script()) {
        ESP_LOGI(TAG, "Stop request sent");
    } else {
        ESP_LOGW(TAG, "Stop request rejected (no script running?)");
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

// Note: syncRunningState() re-derives this from the supervisor on every
// update(), so an external override only lasts until the next poll.
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
    if (m_scriptCount >= MAX_SCRIPTS || filename == nullptr) return;

    // Find insertion position for case-insensitive alphabetical order
    int pos = m_scriptCount;
    for (int i = 0; i < m_scriptCount; i++) {
        if (strcasecmp(filename, m_scripts[i]) < 0) {
            pos = i;
            break;
        }
    }
    // Shift existing entries one slot down
    for (int i = m_scriptCount; i > pos; i--) {
        memcpy(m_scripts[i], m_scripts[i - 1], MAX_FILENAME_LEN);
    }
    strncpy(m_scripts[pos], filename, MAX_FILENAME_LEN - 1);
    m_scripts[pos][MAX_FILENAME_LEN - 1] = '\0';
    m_scriptCount++;

    // Preserve selection on the same filename after insertion
    if (m_selectedIndex >= pos) {
        m_selectedIndex++;
    }

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
