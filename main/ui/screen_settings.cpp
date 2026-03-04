#include "screen_settings.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "SCREEN_SETTINGS";

// Global instance
static ScreenSettings s_screenSettings;

ScreenSettings& getScreenSettings()
{
    return s_screenSettings;
}

// Layout constants
static constexpr int SECTION_START_Y = UI_CONTENT_Y + 5;
static constexpr int SECTION_MARGIN = 8;
static constexpr int BLE_SECTION_HEIGHT = 100;
static constexpr int BACKLIGHT_Y = SECTION_START_Y + BLE_SECTION_HEIGHT + 10;
static constexpr int BACKLIGHT_SLIDER_X = 100;
static constexpr int BACKLIGHT_SLIDER_W = 180;
static constexpr int BACKLIGHT_SLIDER_H = 20;
static constexpr int VERSION_Y = BACKLIGHT_Y + 35;

// Firmware version
static const char* FIRMWARE_VERSION = "v1.0.0";

ScreenSettings::ScreenSettings()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_backlight(80)  // Default 80%
{
}

void ScreenSettings::enter()
{
    m_isActive = true;
    ui_clear_content_area();
    draw();
}

void ScreenSettings::leave()
{
    m_isActive = false;
}

void ScreenSettings::update()
{
    if (!m_isActive) return;

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

void ScreenSettings::draw()
{
    drawBleMidiSection();
    drawBacklightSlider();
    drawVersionInfo();
}

void ScreenSettings::drawBleMidiSection()
{
    int x = SECTION_MARGIN;
    int y = SECTION_START_Y;
    int w = UI_SCREEN_WIDTH - 2 * SECTION_MARGIN;
    int h = BLE_SECTION_HEIGHT;

    // Draw section frame
    M5.Lcd.drawRoundRect(x, y, w, h, 4, UI_COLOR_GRAY);

    // Draw section title
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    M5.Lcd.setCursor(x + 6, y + 6);
    M5.Lcd.print("BLE-MIDI");

    // Draw status
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    M5.Lcd.setCursor(x + 12, y + 24);
    M5.Lcd.print("Status: Not implemented");

    // Draw placeholder device list area
    int listY = y + 40;
    int listH = h - 50;
    M5.Lcd.fillRect(x + 6, listY, w - 12, listH, UI_COLOR_DARKGRAY);
    M5.Lcd.drawRect(x + 6, listY, w - 12, listH, UI_COLOR_GRAY);

    // Placeholder message
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 40, listY + listH / 2 - 4);
    M5.Lcd.print("(BLE scanning not available)");
}

void ScreenSettings::drawBacklightSlider()
{
    int y = BACKLIGHT_Y;

    // Draw label
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    M5.Lcd.setCursor(SECTION_MARGIN, y + 6);
    M5.Lcd.print("Backlight:");

    // Draw slider background
    M5.Lcd.fillRect(BACKLIGHT_SLIDER_X, y, BACKLIGHT_SLIDER_W, BACKLIGHT_SLIDER_H, UI_COLOR_DARKGRAY);
    M5.Lcd.drawRect(BACKLIGHT_SLIDER_X, y, BACKLIGHT_SLIDER_W, BACKLIGHT_SLIDER_H, UI_COLOR_GRAY);

    // Draw slider fill
    int fillW = (m_backlight * BACKLIGHT_SLIDER_W) / 100;
    if (fillW > 0) {
        M5.Lcd.fillRect(BACKLIGHT_SLIDER_X, y, fillW, BACKLIGHT_SLIDER_H, UI_COLOR_GREEN);
    }

    // Draw percentage
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", m_backlight);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    M5.Lcd.setCursor(BACKLIGHT_SLIDER_X + BACKLIGHT_SLIDER_W + 8, y + 6);
    M5.Lcd.print(pctStr);
}

void ScreenSettings::drawVersionInfo()
{
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    M5.Lcd.setCursor(SECTION_MARGIN, VERSION_Y);
    M5.Lcd.print("Version: ");
    M5.Lcd.print(FIRMWARE_VERSION);
}

bool ScreenSettings::hitTestBacklight(int x, int y)
{
    return (x >= BACKLIGHT_SLIDER_X &&
            x < BACKLIGHT_SLIDER_X + BACKLIGHT_SLIDER_W &&
            y >= BACKLIGHT_Y &&
            y < BACKLIGHT_Y + BACKLIGHT_SLIDER_H);
}

void ScreenSettings::handleBacklightTouch(int x)
{
    // Calculate new backlight value
    int relX = x - BACKLIGHT_SLIDER_X;
    if (relX < 0) relX = 0;
    if (relX > BACKLIGHT_SLIDER_W) relX = BACKLIGHT_SLIDER_W;

    uint8_t newLevel = (relX * 100) / BACKLIGHT_SLIDER_W;

    if (newLevel != m_backlight) {
        m_backlight = newLevel;

        // Apply backlight
        int brightness = (m_backlight * 255) / 100;
        M5.Lcd.setBrightness(brightness);

        // Redraw slider
        drawBacklightSlider();
    }
}

void ScreenSettings::onTouch(int x, int y, bool pressed)
{
    if (pressed && hitTestBacklight(x, y)) {
        handleBacklightTouch(x);
    }
}

void ScreenSettings::onNavCenter()
{
    // Scan for BLE devices (placeholder)
    ESP_LOGI(TAG, "BLE scan requested (not implemented)");
}

const char* ScreenSettings::getTitle()
{
    return "Settings";
}

const char* ScreenSettings::getNavCenterLabel()
{
    return "Scan";
}

void ScreenSettings::setBacklight(uint8_t level)
{
    if (level > 100) level = 100;
    m_backlight = level;

    int brightness = (level * 255) / 100;
    M5.Lcd.setBrightness(brightness);

    if (m_isActive) {
        drawBacklightSlider();
    }
}

// C API implementation
extern "C" {

void ui_settings_set_backlight(uint8_t level)
{
    getScreenSettings().setBacklight(level);
}

uint8_t ui_settings_get_backlight(void)
{
    return getScreenSettings().getBacklight();
}

} // extern "C"
