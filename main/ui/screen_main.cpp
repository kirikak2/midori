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

// Layout constants
static constexpr int BPM_Y = UI_CONTENT_Y + 20;
static constexpr int BPM_BUTTON_Y = UI_CONTENT_Y + 10;
static constexpr int BPM_BUTTON_W = 45;
static constexpr int BPM_BUTTON_H = 30;
static constexpr int EXTERNAL_BPM_Y = UI_CONTENT_Y + 65;
static constexpr int SYNC_Y = UI_CONTENT_Y + 85;
static constexpr int BAR_BEAT_Y = UI_CONTENT_Y + 120;
static constexpr int PROGRESS_Y = UI_CONTENT_Y + 145;
static constexpr int PROGRESS_W = 260;
static constexpr int PROGRESS_H = 16;

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
{
    memset(m_tapTimes, 0, sizeof(m_tapTimes));
    initButtons();
}

void ScreenMain::initButtons()
{
    int centerX = UI_SCREEN_WIDTH / 2;
    int spacing = 50;

    // [-10] button
    m_buttons[BTN_MINUS_10].x = centerX - 2 * spacing - BPM_BUTTON_W / 2;
    m_buttons[BTN_MINUS_10].y = BPM_BUTTON_Y;
    m_buttons[BTN_MINUS_10].w = BPM_BUTTON_W;
    m_buttons[BTN_MINUS_10].h = BPM_BUTTON_H;
    m_buttons[BTN_MINUS_10].label = "-10";
    m_buttons[BTN_MINUS_10].pressed = false;

    // [-1] button
    m_buttons[BTN_MINUS_1].x = centerX - spacing - BPM_BUTTON_W / 2;
    m_buttons[BTN_MINUS_1].y = BPM_BUTTON_Y;
    m_buttons[BTN_MINUS_1].w = BPM_BUTTON_W;
    m_buttons[BTN_MINUS_1].h = BPM_BUTTON_H;
    m_buttons[BTN_MINUS_1].label = "-1";
    m_buttons[BTN_MINUS_1].pressed = false;

    // [+1] button
    m_buttons[BTN_PLUS_1].x = centerX + spacing - BPM_BUTTON_W / 2;
    m_buttons[BTN_PLUS_1].y = BPM_BUTTON_Y;
    m_buttons[BTN_PLUS_1].w = BPM_BUTTON_W;
    m_buttons[BTN_PLUS_1].h = BPM_BUTTON_H;
    m_buttons[BTN_PLUS_1].label = "+1";
    m_buttons[BTN_PLUS_1].pressed = false;

    // [+10] button
    m_buttons[BTN_PLUS_10].x = centerX + 2 * spacing - BPM_BUTTON_W / 2;
    m_buttons[BTN_PLUS_10].y = BPM_BUTTON_Y;
    m_buttons[BTN_PLUS_10].w = BPM_BUTTON_W;
    m_buttons[BTN_PLUS_10].h = BPM_BUTTON_H;
    m_buttons[BTN_PLUS_10].label = "+10";
    m_buttons[BTN_PLUS_10].pressed = false;

    // [Sync] button
    m_buttons[BTN_SYNC].x = UI_SCREEN_WIDTH / 2 - 30;
    m_buttons[BTN_SYNC].y = SYNC_Y;
    m_buttons[BTN_SYNC].w = 60;
    m_buttons[BTN_SYNC].h = 25;
    m_buttons[BTN_SYNC].label = "Sync";
    m_buttons[BTN_SYNC].pressed = false;
}

void ScreenMain::enter()
{
    m_isActive = true;
    m_lastDrawnBpm = -1;  // Force full redraw
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

    // Check for changes that need redraw
    bool needsFullRedraw = m_needsRedraw;
    bool needsBpmUpdate = false;
    bool needsBarBeatUpdate = false;
    bool needsProgressUpdate = false;

    float currentBpm = ui.getBpm();
    float externalBpm = ui.getExternalBpm();
    bool syncMode = ui.getSyncMode();

    if (currentBpm != m_lastDrawnBpm || externalBpm != m_lastDrawnExternalBpm || syncMode != m_lastDrawnSyncMode) {
        needsBpmUpdate = true;
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
        if (needsBpmUpdate) {
            drawBpmDisplay();
            drawExternalBpm();
            drawSyncButton();
        }
        if (needsBarBeatUpdate) {
            drawBarBeat();
        }
        if (needsProgressUpdate) {
            drawBeatProgress();
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

    // Clear BPM area
    int bpmDisplayX = UI_SCREEN_WIDTH / 2 - 40;
    M5.Lcd.fillRect(bpmDisplayX, BPM_Y, 80, 35, UI_COLOR_BLACK);

    // Draw BPM value
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);

    char bpmStr[16];
    snprintf(bpmStr, sizeof(bpmStr), "%3d", (int)bpm);

    M5.Lcd.setCursor(bpmDisplayX, BPM_Y + 5);
    M5.Lcd.print(bpmStr);

    // Draw "BPM" label
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(bpmDisplayX + 60, BPM_Y + 15);
    M5.Lcd.print("BPM");

    m_lastDrawnBpm = bpm;
    m_lastDrawnSyncMode = syncMode;
}

void ScreenMain::drawExternalBpm()
{
    UIManager& ui = UIManager::getInstance();
    float externalBpm = ui.getExternalBpm();

    // Clear external BPM area
    M5.Lcd.fillRect(60, EXTERNAL_BPM_Y, 200, 16, UI_COLOR_BLACK);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);

    char extStr[32];
    if (externalBpm > 0) {
        snprintf(extStr, sizeof(extStr), "External: %.1f BPM", externalBpm);
    } else {
        snprintf(extStr, sizeof(extStr), "External: ---.-- BPM");
    }

    M5.Lcd.setCursor(90, EXTERNAL_BPM_Y);
    M5.Lcd.print(extStr);

    m_lastDrawnExternalBpm = externalBpm;
}

void ScreenMain::drawSyncButton()
{
    UIManager& ui = UIManager::getInstance();
    bool syncMode = ui.getSyncMode();
    float externalBpm = ui.getExternalBpm();

    Button& btn = m_buttons[BTN_SYNC];

    // Determine button color based on state
    uint16_t bgColor;
    uint16_t textColor = UI_COLOR_WHITE;

    if (externalBpm <= 0) {
        // No external clock - gray out
        bgColor = UI_COLOR_DARKGRAY;
        textColor = UI_COLOR_GRAY;
    } else if (syncMode) {
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

    // Clear area
    M5.Lcd.fillRect(60, BAR_BEAT_Y, 200, 20, UI_COLOR_BLACK);

    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);

    char barBeatStr[32];
    snprintf(barBeatStr, sizeof(barBeatStr), "Bar: %3lu  Beat: %d", (unsigned long)bar, beat);

    M5.Lcd.setCursor(60, BAR_BEAT_Y);
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

void ScreenMain::onTouch(int x, int y, bool pressed)
{
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
