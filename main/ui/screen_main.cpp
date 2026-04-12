#include "sdkconfig.h"
#include "screen_main.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "SCREEN_MAIN";

// Global instance
static ScreenMain s_screenMain;

ScreenMain& getScreenMain()
{
    return s_screenMain;
}

// Layout constants - Board-specific
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
// Tab5: Larger fonts and spacious layout for 1280x720
static constexpr int BPM_TEXT_SIZE = 6;
static constexpr int BPM_LABEL_TEXT_SIZE = 2;
static constexpr int BAR_BEAT_TEXT_SIZE = 4;
static constexpr int EXTERNAL_BPM_TEXT_SIZE = 2;
static constexpr int BPM_Y = UI_CONTENT_Y + 60;
static constexpr int BPM_BUTTON_Y = UI_CONTENT_Y + 30;
static constexpr int BPM_BUTTON_W = 80;
static constexpr int BPM_BUTTON_H = 50;
static constexpr int BPM_BUTTON_SPACING = 90;
static constexpr int EXTERNAL_BPM_Y = UI_CONTENT_Y + 180;
static constexpr int SOURCE_BTN_Y = UI_CONTENT_Y + 140;
static constexpr int SOURCE_BTN_W = 80;
static constexpr int SOURCE_BTN_H = 40;
static constexpr int SYNC_Y = UI_CONTENT_Y + 240;
static constexpr int SYNC_BTN_W = 200;
static constexpr int SYNC_BTN_H = 50;
static constexpr int BAR_BEAT_Y = UI_CONTENT_Y + 340;
static constexpr int PROGRESS_Y = UI_CONTENT_Y + 420;
static constexpr int PROGRESS_W = 800;
static constexpr int PROGRESS_H = 30;
#else
// CoreS3: Original compact layout for 320x240
static constexpr int BPM_TEXT_SIZE = 3;
static constexpr int BPM_LABEL_TEXT_SIZE = 1;
static constexpr int BAR_BEAT_TEXT_SIZE = 2;
static constexpr int EXTERNAL_BPM_TEXT_SIZE = 1;
static constexpr int BPM_Y = UI_CONTENT_Y + 20;
static constexpr int BPM_BUTTON_Y = UI_CONTENT_Y + 10;
static constexpr int BPM_BUTTON_W = 45;
static constexpr int BPM_BUTTON_H = 30;
static constexpr int BPM_BUTTON_SPACING = 50;
static constexpr int EXTERNAL_BPM_Y = UI_CONTENT_Y + 65;
static constexpr int SOURCE_BTN_Y = UI_CONTENT_Y + 48;
static constexpr int SOURCE_BTN_W = 40;
static constexpr int SOURCE_BTN_H = 20;
static constexpr int SYNC_Y = UI_CONTENT_Y + 85;
static constexpr int SYNC_BTN_W = 100;
static constexpr int SYNC_BTN_H = 30;
static constexpr int BAR_BEAT_Y = UI_CONTENT_Y + 120;
static constexpr int PROGRESS_Y = UI_CONTENT_Y + 145;
static constexpr int PROGRESS_W = 260;
static constexpr int PROGRESS_H = 16;
#endif

ScreenMain::ScreenMain()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_tapIndex(0)
    , m_tapCount(0)
    , m_lastDrawnBpm(0)
    , m_lastDrawnExternalBpm(0)
    , m_lastDrawnBar(0)
    , m_lastDrawnBeat(0)
    , m_lastDrawnProgress(0)
    , m_lastDrawnSyncMode(false)
    , m_lastDrawnExternalBpmSource(0xFF)  // Invalid value to force initial draw
    , m_lastSyncButtonState(-1)  // Invalid value to force initial draw
    , m_lastExternalBpmDrawTime(0)
{
    memset(m_tapTimes, 0, sizeof(m_tapTimes));
    initButtons();
}

void ScreenMain::initButtons()
{
    int centerX = UI_SCREEN_WIDTH / 2;

    // [-10] button
    m_buttons[BTN_MINUS_10].x = centerX - 2 * BPM_BUTTON_SPACING - BPM_BUTTON_W / 2;
    m_buttons[BTN_MINUS_10].y = BPM_BUTTON_Y;
    m_buttons[BTN_MINUS_10].w = BPM_BUTTON_W;
    m_buttons[BTN_MINUS_10].h = BPM_BUTTON_H;
    m_buttons[BTN_MINUS_10].label = "-10";
    m_buttons[BTN_MINUS_10].pressed = false;

    // [-1] button
    m_buttons[BTN_MINUS_1].x = centerX - BPM_BUTTON_SPACING - BPM_BUTTON_W / 2;
    m_buttons[BTN_MINUS_1].y = BPM_BUTTON_Y;
    m_buttons[BTN_MINUS_1].w = BPM_BUTTON_W;
    m_buttons[BTN_MINUS_1].h = BPM_BUTTON_H;
    m_buttons[BTN_MINUS_1].label = "-1";
    m_buttons[BTN_MINUS_1].pressed = false;

    // [+1] button
    m_buttons[BTN_PLUS_1].x = centerX + BPM_BUTTON_SPACING - BPM_BUTTON_W / 2;
    m_buttons[BTN_PLUS_1].y = BPM_BUTTON_Y;
    m_buttons[BTN_PLUS_1].w = BPM_BUTTON_W;
    m_buttons[BTN_PLUS_1].h = BPM_BUTTON_H;
    m_buttons[BTN_PLUS_1].label = "+1";
    m_buttons[BTN_PLUS_1].pressed = false;

    // [+10] button
    m_buttons[BTN_PLUS_10].x = centerX + 2 * BPM_BUTTON_SPACING - BPM_BUTTON_W / 2;
    m_buttons[BTN_PLUS_10].y = BPM_BUTTON_Y;
    m_buttons[BTN_PLUS_10].w = BPM_BUTTON_W;
    m_buttons[BTN_PLUS_10].h = BPM_BUTTON_H;
    m_buttons[BTN_PLUS_10].label = "+10";
    m_buttons[BTN_PLUS_10].pressed = false;

    // [Sync] button
    m_buttons[BTN_SYNC].x = UI_SCREEN_WIDTH / 2 - SYNC_BTN_W / 2;
    m_buttons[BTN_SYNC].y = SYNC_Y;
    m_buttons[BTN_SYNC].w = SYNC_BTN_W;
    m_buttons[BTN_SYNC].h = SYNC_BTN_H;
    m_buttons[BTN_SYNC].label = "Sync";
    m_buttons[BTN_SYNC].pressed = false;

    // Source selection buttons (centered horizontally)
    int sourceStartX = (UI_SCREEN_WIDTH - (3 * SOURCE_BTN_W + 2 * 10)) / 2;
    m_buttons[BTN_SOURCE_USB].x = sourceStartX;
    m_buttons[BTN_SOURCE_USB].y = SOURCE_BTN_Y;
    m_buttons[BTN_SOURCE_USB].w = SOURCE_BTN_W;
    m_buttons[BTN_SOURCE_USB].h = SOURCE_BTN_H;
    m_buttons[BTN_SOURCE_USB].label = "USB";
    m_buttons[BTN_SOURCE_USB].pressed = false;

    m_buttons[BTN_SOURCE_DIN].x = sourceStartX + SOURCE_BTN_W + 10;
    m_buttons[BTN_SOURCE_DIN].y = SOURCE_BTN_Y;
    m_buttons[BTN_SOURCE_DIN].w = SOURCE_BTN_W;
    m_buttons[BTN_SOURCE_DIN].h = SOURCE_BTN_H;
    m_buttons[BTN_SOURCE_DIN].label = "DIN";
    m_buttons[BTN_SOURCE_DIN].pressed = false;

    m_buttons[BTN_SOURCE_BLE].x = sourceStartX + (SOURCE_BTN_W + 10) * 2;
    m_buttons[BTN_SOURCE_BLE].y = SOURCE_BTN_Y;
    m_buttons[BTN_SOURCE_BLE].w = SOURCE_BTN_W;
    m_buttons[BTN_SOURCE_BLE].h = SOURCE_BTN_H;
    m_buttons[BTN_SOURCE_BLE].label = "BLE";
    m_buttons[BTN_SOURCE_BLE].pressed = false;
}

void ScreenMain::enter()
{
    m_isActive = true;
    m_lastDrawnBpm = -1;  // Force full redraw
    m_lastSyncButtonState = -1;  // Force sync button redraw
    m_lastDrawnExternalBpmSource = 0xFF;  // Force source buttons redraw
    ui_clear_content_area();
    draw();
}

void ScreenMain::leave()
{
    m_isActive = false;
}

void ScreenMain::update()
{
    if (!m_isActive) return;

    UIManager& ui = UIManager::getInstance();
    int64_t now = esp_timer_get_time();

    // Check for changes that need redraw
    bool needsFullRedraw = m_needsRedraw;
    bool needsBpmUpdate = false;
    bool needsExternalBpmUpdate = false;
    bool needsBarBeatUpdate = false;
    bool needsProgressUpdate = false;

    float currentBpm = ui.getBpm();
    float externalBpm = ui.getExternalBpm();
    bool syncMode = ui.getSyncMode();

    // Check what changed - separate immediate vs throttled updates
    if (currentBpm != m_lastDrawnBpm || syncMode != m_lastDrawnSyncMode) {
        // User-initiated changes (BPM adjustment, sync toggle) - immediate update
        needsBpmUpdate = true;
    }

    if (externalBpm != m_lastDrawnExternalBpm) {
        // External BPM changes from MIDI clock - throttle to reduce flicker
        // At 120 BPM, MIDI clock sends 48 updates/sec. Throttle to ~10 FPS.
        const int64_t EXTERNAL_BPM_UPDATE_INTERVAL_US = 100000;  // 100ms = 10 FPS
        if (now - m_lastExternalBpmDrawTime >= EXTERNAL_BPM_UPDATE_INTERVAL_US) {
            needsExternalBpmUpdate = true;
            m_lastExternalBpmDrawTime = now;
        }
    }

    if (ui.getBar() != m_lastDrawnBar || ui.getBeat() != m_lastDrawnBeat) {
        needsBarBeatUpdate = true;
    }

    if (ui.getBeatProgress() != m_lastDrawnProgress) {
        needsProgressUpdate = true;
    }

    if (needsFullRedraw) {
        draw();
        m_needsRedraw = false;
    } else {
        // Batch partial updates in a single transaction to prevent flickering
        if (needsBpmUpdate || needsExternalBpmUpdate || needsBarBeatUpdate || needsProgressUpdate) {
            M5.Lcd.startWrite();
            if (needsBpmUpdate || needsExternalBpmUpdate) {
                drawBpmDisplay();
                drawExternalBpm();
                drawSyncButton();  // Will skip redraw if visual state unchanged
            }
            if (needsBarBeatUpdate) {
                drawBarBeat();
            }
            if (needsProgressUpdate) {
                drawBeatProgress();
            }
            M5.Lcd.endWrite();
        }
    }
}

void ScreenMain::draw()
{
    drawButtons();
    drawBpmDisplay();
    drawExternalBpm();
    drawSyncButton();
    drawBarBeat();
    drawBeatProgress();
}

void ScreenMain::drawBpmDisplay()
{
    UIManager& ui = UIManager::getInstance();
    float bpm = ui.getBpm();
    bool syncMode = ui.getSyncMode();

    // Draw BPM value (background color overwrites old content)
    int bpmDisplayX = UI_SCREEN_WIDTH / 2 - (BPM_TEXT_SIZE * 3 * 6) / 2;
    M5.Lcd.setTextSize(BPM_TEXT_SIZE);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);

    char bpmStr[16];
    snprintf(bpmStr, sizeof(bpmStr), "%3d", (int)bpm);

    M5.Lcd.setCursor(bpmDisplayX, BPM_Y);
    M5.Lcd.print(bpmStr);

    // Draw "BPM" label
    M5.Lcd.setTextSize(BPM_LABEL_TEXT_SIZE);
    M5.Lcd.setCursor(bpmDisplayX + BPM_TEXT_SIZE * 3 * 6 + 10, BPM_Y + BPM_TEXT_SIZE * 8 / 2 - BPM_LABEL_TEXT_SIZE * 4);
    M5.Lcd.print("BPM");

    m_lastDrawnBpm = bpm;
    m_lastDrawnSyncMode = syncMode;
}

void ScreenMain::drawExternalBpm()
{
    UIManager& ui = UIManager::getInstance();
    float externalBpm = ui.getExternalBpm();
    midi_interface_t selectedSource = ui.getExternalBpmSourceSelection();

    // Only redraw source buttons if selection changed
    bool sourceChanged = (selectedSource != m_lastDrawnExternalBpmSource);

    if (sourceChanged) {
        // Draw source selection buttons
        const char* sourceLabels[] = {"USB", "DIN", "BLE"};
        for (int i = 0; i < 3; i++) {
            Button& btn = m_buttons[BTN_SOURCE_USB + i];
            bool isSelected = (selectedSource == (midi_interface_t)i);
            float sourceBpm = ui.getExternalBpmBySource((midi_interface_t)i);

            uint16_t bgColor = isSelected ? UI_COLOR_BLUE : UI_COLOR_DARKGRAY;
            uint16_t textColor = (sourceBpm > 0) ? UI_COLOR_WHITE : UI_COLOR_GRAY;

            if (!isSelected && sourceBpm <= 0) {
                bgColor = UI_COLOR_BLACK;
            }

            M5.Lcd.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 2, bgColor);
            M5.Lcd.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 2, UI_COLOR_WHITE);

            M5.Lcd.setTextSize(1);
            M5.Lcd.setTextColor(textColor, bgColor);
            int textW = strlen(sourceLabels[i]) * 6;
            M5.Lcd.setCursor(btn.x + (btn.w - textW) / 2, btn.y + (btn.h - 8) / 2);
            M5.Lcd.print(sourceLabels[i]);
        }
        m_lastDrawnExternalBpmSource = selectedSource;
    }

    // Draw external BPM (background color overwrites old content)
    M5.Lcd.setTextSize(EXTERNAL_BPM_TEXT_SIZE);
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);

    char extStr[32];
    if (externalBpm > 0) {
        snprintf(extStr, sizeof(extStr), "Ext: %.1f BPM", externalBpm);
    } else {
        snprintf(extStr, sizeof(extStr), "Ext: --- BPM");
    }

    int extTextWidth = strlen(extStr) * 6 * EXTERNAL_BPM_TEXT_SIZE;
    M5.Lcd.setCursor((UI_SCREEN_WIDTH - extTextWidth) / 2, EXTERNAL_BPM_Y);
    M5.Lcd.print(extStr);

    m_lastDrawnExternalBpm = externalBpm;
}

void ScreenMain::drawSyncButton()
{
    UIManager& ui = UIManager::getInstance();
    bool syncMode = ui.getSyncMode();
    float externalBpm = ui.getExternalBpm();

    // Determine visual state: 0=disabled, 1=off, 2=on
    int8_t newState;
    if (externalBpm <= 0) {
        newState = 0;  // disabled
    } else if (syncMode) {
        newState = 2;  // on
    } else {
        newState = 1;  // off
    }

    // Skip redraw if visual state hasn't changed (prevents flicker from frequent external BPM updates)
    if (newState == m_lastSyncButtonState) {
        return;
    }
    m_lastSyncButtonState = newState;

    Button& btn = m_buttons[BTN_SYNC];

    // Determine button color based on state
    uint16_t bgColor;
    uint16_t textColor = UI_COLOR_WHITE;

    if (newState == 0) {
        // No external clock - gray out
        bgColor = UI_COLOR_DARKGRAY;
        textColor = UI_COLOR_GRAY;
    } else if (newState == 2) {
        // Sync mode on
        bgColor = UI_COLOR_GREEN;
    } else {
        // Sync mode off
        bgColor = UI_COLOR_NAVY;
    }

    // Draw button
    M5.Lcd.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, bgColor);
    M5.Lcd.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 4, UI_COLOR_WHITE);

    // Draw label
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(textColor, bgColor);

    const char* label = syncMode ? "SYNC" : "Sync";
    int textW = strlen(label) * 6;
    M5.Lcd.setCursor(btn.x + (btn.w - textW) / 2, btn.y + (btn.h - 8) / 2);
    M5.Lcd.print(label);
}

void ScreenMain::drawBarBeat()
{
    UIManager& ui = UIManager::getInstance();
    uint32_t bar = ui.getBar();
    uint8_t beat = ui.getBeat();

    // Draw bar/beat (background color overwrites old content)
    M5.Lcd.setTextSize(BAR_BEAT_TEXT_SIZE);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);

    char barBeatStr[32];
    snprintf(barBeatStr, sizeof(barBeatStr), "Bar: %3lu  Beat: %d", (unsigned long)bar, beat);

    int barBeatWidth = strlen(barBeatStr) * 6 * BAR_BEAT_TEXT_SIZE;
    M5.Lcd.setCursor((UI_SCREEN_WIDTH - barBeatWidth) / 2, BAR_BEAT_Y);
    M5.Lcd.print(barBeatStr);

    m_lastDrawnBar = bar;
    m_lastDrawnBeat = beat;
}

void ScreenMain::drawBeatProgress()
{
    UIManager& ui = UIManager::getInstance();
    uint8_t progress = ui.getBeatProgress();

    int progressX = (UI_SCREEN_WIDTH - PROGRESS_W) / 2;

    // Draw progress bar background
    M5.Lcd.fillRect(progressX, PROGRESS_Y, PROGRESS_W, PROGRESS_H, UI_COLOR_DARKGRAY);

    // Draw progress bar fill
    int fillWidth = (progress * PROGRESS_W) / 24;
    if (fillWidth > 0) {
        M5.Lcd.fillRect(progressX, PROGRESS_Y, fillWidth, PROGRESS_H, UI_COLOR_GREEN);
    }

    // Draw border
    M5.Lcd.drawRect(progressX, PROGRESS_Y, PROGRESS_W, PROGRESS_H, UI_COLOR_WHITE);

    m_lastDrawnProgress = progress;
}

void ScreenMain::drawButtons()
{
    UIManager& ui = UIManager::getInstance();
    bool syncMode = ui.getSyncMode();

    // Draw BPM adjustment buttons (disabled when in sync mode)
    for (int i = BTN_MINUS_10; i <= BTN_PLUS_10; i++) {
        Button& btn = m_buttons[i];
        uint16_t bgColor = syncMode ? UI_COLOR_DARKGRAY : UI_COLOR_NAVY;
        uint16_t textColor = syncMode ? UI_COLOR_GRAY : UI_COLOR_WHITE;

        if (btn.pressed && !syncMode) {
            bgColor = ui_lighten_color(bgColor);
        }

        M5.Lcd.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, bgColor);
        M5.Lcd.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 4, UI_COLOR_WHITE);

        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(textColor, bgColor);

        int textW = strlen(btn.label) * 6;
        M5.Lcd.setCursor(btn.x + (btn.w - textW) / 2, btn.y + (btn.h - 8) / 2);
        M5.Lcd.print(btn.label);
    }
}

int ScreenMain::hitTestButton(int x, int y)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        Button& btn = m_buttons[i];
        if (x >= btn.x && x < btn.x + btn.w &&
            y >= btn.y && y < btn.y + btn.h) {
            return i;
        }
    }
    return -1;
}

void ScreenMain::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;  // Only handle first touch for buttons
    if (pressed) {
        int buttonId = hitTestButton(x, y);
        if (buttonId >= 0) {
            handleButtonPress(buttonId);
        }
    }
}

void ScreenMain::handleButtonPress(int buttonId)
{
    UIManager& ui = UIManager::getInstance();
    bool syncMode = ui.getSyncMode();

    switch (buttonId) {
        case BTN_MINUS_10:
            if (!syncMode) {
                ui.setBpm(ui.getBpm() - 10);
            }
            break;

        case BTN_MINUS_1:
            if (!syncMode) {
                ui.setBpm(ui.getBpm() - 1);
            }
            break;

        case BTN_PLUS_1:
            if (!syncMode) {
                ui.setBpm(ui.getBpm() + 1);
            }
            break;

        case BTN_PLUS_10:
            if (!syncMode) {
                ui.setBpm(ui.getBpm() + 10);
            }
            break;

        case BTN_SYNC:
            // Only allow sync toggle if external BPM is available
            if (ui.getExternalBpm() > 0) {
                ui.setSyncMode(!syncMode);
                drawSyncButton();
                drawButtons();  // Update button enabled states
            }
            break;

        case BTN_SOURCE_USB:
            ui.setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
            drawExternalBpm();
            drawSyncButton();
            break;

        case BTN_SOURCE_DIN:
            ui.setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
            drawExternalBpm();
            drawSyncButton();
            break;

        case BTN_SOURCE_BLE:
            ui.setExternalBpmSourceSelection(MIDI_INTERFACE_BLE);
            drawExternalBpm();
            drawSyncButton();
            break;
    }
}

void ScreenMain::onNavCenter()
{
    // TAP tempo
    processTapTempo();
}

void ScreenMain::processTapTempo()
{
    UIManager& ui = UIManager::getInstance();

    // Don't process TAP in sync mode
    if (ui.getSyncMode()) {
        return;
    }

    uint32_t now = esp_timer_get_time() / 1000;  // Convert to ms

    // Check for timeout
    if (m_tapCount > 0) {
        uint32_t lastTap = m_tapTimes[(m_tapIndex - 1 + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES];
        if (now - lastTap > UI_TAP_TEMPO_TIMEOUT_MS) {
            // Reset
            m_tapCount = 0;
            m_tapIndex = 0;
        }
    }

    // Record this tap
    m_tapTimes[m_tapIndex] = now;
    m_tapIndex = (m_tapIndex + 1) % UI_TAP_TEMPO_SAMPLES;
    if (m_tapCount < UI_TAP_TEMPO_SAMPLES) {
        m_tapCount++;
    }

    // Calculate BPM if we have enough samples
    if (m_tapCount >= 2) {
        uint32_t totalInterval = 0;
        int intervals = m_tapCount - 1;

        for (int i = 0; i < intervals; i++) {
            int curr = (m_tapIndex - 1 - i + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES;
            int prev = (curr - 1 + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES;
            totalInterval += m_tapTimes[curr] - m_tapTimes[prev];
        }

        float avgInterval = (float)totalInterval / intervals;
        float newBpm = 60000.0f / avgInterval;

        // Clamp to valid range
        if (newBpm >= UI_BPM_MIN && newBpm <= UI_BPM_MAX) {
            ui.setBpm(newBpm);
            ESP_LOGI(TAG, "TAP tempo: %.1f BPM", newBpm);
        }
    }
}

const char* ScreenMain::getTitle()
{
    return "Main";
}

const char* ScreenMain::getNavCenterLabel()
{
    return "TAP";
}
