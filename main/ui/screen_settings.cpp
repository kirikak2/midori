#include "sdkconfig.h"
#include "screen_settings.h"
#include "ui_strip.h"
#include "board_audio.h"
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

// Layout constants - Board-specific
#if defined(UI_LAYOUT_LARGE)
static constexpr int SECTION_TEXT_SIZE = 2;
static constexpr int SECTION_START_Y = UI_CONTENT_Y + 20;
static constexpr int SECTION_MARGIN = 20;
static constexpr int BLE_SECTION_HEIGHT = 200;
static constexpr int BACKLIGHT_Y = SECTION_START_Y + BLE_SECTION_HEIGHT + 30;
static constexpr int BACKLIGHT_LABEL_X = SECTION_MARGIN + 20;
static constexpr int BACKLIGHT_SLIDER_X = 250;
static constexpr int BACKLIGHT_SLIDER_W = 500;
static constexpr int BACKLIGHT_SLIDER_H = 40;
static constexpr int SLIDER_ROW_PITCH = 60;
static constexpr int VERSION_GAP = 80;
#else
static constexpr int SECTION_TEXT_SIZE = 1;
static constexpr int SECTION_START_Y = UI_CONTENT_Y + 5;
static constexpr int SECTION_MARGIN = 8;
static constexpr int BLE_SECTION_HEIGHT = 100;
static constexpr int BACKLIGHT_Y = SECTION_START_Y + BLE_SECTION_HEIGHT + 10;
static constexpr int BACKLIGHT_LABEL_X = SECTION_MARGIN;
static constexpr int BACKLIGHT_SLIDER_X = 100;
static constexpr int BACKLIGHT_SLIDER_W = 180;
static constexpr int BACKLIGHT_SLIDER_H = 20;
static constexpr int SLIDER_ROW_PITCH = 28;
static constexpr int VERSION_GAP = 35;
#endif

// Firmware version
static const char* FIRMWARE_VERSION = "v1.0.0";

ScreenSettings::ScreenSettings()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_backlight(80)  // Default 80%
    , m_dragSlider(-1)
    , m_dragTouchId(-1)
    , m_hpInserted(false)
{
}

void ScreenSettings::enter()
{
    m_isActive = true;
    m_dragSlider = -1;
    m_hpInserted = board_audio_headphone_inserted();
}

void ScreenSettings::leave()
{
    m_isActive = false;
    m_dragSlider = -1;
    board_audio_save();
}

void ScreenSettings::update()
{
    if (!m_isActive) return;

    // Plugging headphones in mutes the speaker: show it on the speaker row
    bool hp = board_audio_headphone_inserted();
    if (hp != m_hpInserted) {
        m_hpInserted = hp;
        if (sliderVisible(SLIDER_SPEAKER)) drawSliderPartial(SLIDER_SPEAKER);
    }

    if (m_needsRedraw) {
        ui_strip::repaint_content([&]() { draw(); });
        m_needsRedraw = false;
    }
}

void ScreenSettings::draw()
{
    m_hpInserted = board_audio_headphone_inserted();
    drawBleMidiSection();
    for (int id = 0; id < SLIDER_COUNT; id++) {
        if (sliderVisible(id)) drawSlider(id);
    }
    drawVersionInfo();
}

void ScreenSettings::drawBleMidiSection()
{
    int x = SECTION_MARGIN;
    int y = SECTION_START_Y;
    int w = UI_SCREEN_WIDTH - 2 * SECTION_MARGIN;
    int h = BLE_SECTION_HEIGHT;

    // Draw section frame
    ui_gfx().drawRoundRect(x, y, w, h, 4, UI_COLOR_GRAY);

    // Draw section title
    ui_gfx().setTextSize(SECTION_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    ui_gfx().setCursor(x + 6 * SECTION_TEXT_SIZE, y + 6 * SECTION_TEXT_SIZE);
    ui_gfx().print("BLE-MIDI");

    // Draw status
    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    ui_gfx().setCursor(x + 12 * SECTION_TEXT_SIZE, y + 24 * SECTION_TEXT_SIZE);
    ui_gfx().print("Status: Not implemented");

    // Draw placeholder device list area
    int listY = y + 40 * SECTION_TEXT_SIZE;
    int listH = h - 50 * SECTION_TEXT_SIZE;
    ui_gfx().fillRect(x + 6 * SECTION_TEXT_SIZE, listY, w - 12 * SECTION_TEXT_SIZE, listH, UI_COLOR_DARKGRAY);
    ui_gfx().drawRect(x + 6 * SECTION_TEXT_SIZE, listY, w - 12 * SECTION_TEXT_SIZE, listH, UI_COLOR_GRAY);

    // Placeholder message
    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    int msgWidth = strlen("(BLE scanning not available)") * 6 * SECTION_TEXT_SIZE;
    ui_gfx().setCursor(x + (w - msgWidth) / 2, listY + listH / 2 - 4 * SECTION_TEXT_SIZE);
    ui_gfx().print("(BLE scanning not available)");
}

bool ScreenSettings::sliderVisible(int id) const
{
    switch (id) {
    case SLIDER_BACKLIGHT: return true;
    case SLIDER_SPEAKER:   return board_audio_has_output(BOARD_AUDIO_SPEAKER);
    case SLIDER_HEADPHONE: return board_audio_has_output(BOARD_AUDIO_HEADPHONE);
    default:               return false;
    }
}

int ScreenSettings::sliderY(int id) const
{
    // Rows are packed: a hidden slider takes no space
    int y = BACKLIGHT_Y;
    for (int i = 0; i < id; i++) {
        if (sliderVisible(i)) y += SLIDER_ROW_PITCH;
    }
    return y;
}

void ScreenSettings::drawSlider(int id)
{
    int y = sliderY(id);
    const char* label = "Backlight:";
    uint8_t value = m_backlight;
    uint16_t fill = UI_COLOR_GREEN;
    bool muted = false;

    if (id == SLIDER_SPEAKER) {
        label = "Speaker:";
        value = board_audio_get_volume(BOARD_AUDIO_SPEAKER);
        fill = UI_COLOR_CYAN;
        muted = m_hpInserted;
    } else if (id == SLIDER_HEADPHONE) {
        label = "Headphones:";
        value = board_audio_get_volume(BOARD_AUDIO_HEADPHONE);
        fill = UI_COLOR_CYAN;
    }
    if (muted) fill = UI_COLOR_GRAY;

    // Draw label
    ui_gfx().setTextSize(SECTION_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    int labelY = y + (BACKLIGHT_SLIDER_H - 8 * SECTION_TEXT_SIZE) / 2;
    ui_gfx().setCursor(BACKLIGHT_LABEL_X, labelY);
    ui_gfx().print(label);

    // Draw slider background
    ui_gfx().fillRect(BACKLIGHT_SLIDER_X, y, BACKLIGHT_SLIDER_W, BACKLIGHT_SLIDER_H, UI_COLOR_DARKGRAY);
    ui_gfx().drawRect(BACKLIGHT_SLIDER_X, y, BACKLIGHT_SLIDER_W, BACKLIGHT_SLIDER_H, UI_COLOR_GRAY);

    // Draw slider fill
    int fillW = (value * BACKLIGHT_SLIDER_W) / 100;
    if (fillW > 0) {
        ui_gfx().fillRect(BACKLIGHT_SLIDER_X, y, fillW, BACKLIGHT_SLIDER_H, fill);
    }

    // Draw percentage (clear the old text first: "100%" -> "99%" is shorter)
    int textX = BACKLIGHT_SLIDER_X + BACKLIGHT_SLIDER_W + 8 * SECTION_TEXT_SIZE;
    ui_gfx().fillRect(textX, labelY, UI_SCREEN_WIDTH - textX, 8 * SECTION_TEXT_SIZE, UI_COLOR_BLACK);
    char pctStr[16];
    snprintf(pctStr, sizeof(pctStr), muted ? "%d%% muted" : "%d%%", value);
    ui_gfx().setTextColor(muted ? UI_COLOR_GRAY : UI_COLOR_WHITE, UI_COLOR_BLACK);
    ui_gfx().setCursor(textX, labelY);
    ui_gfx().print(pctStr);
}

void ScreenSettings::drawSliderPartial(int id)
{
    // Partial redraw needs its own transaction
    ui_gfx().startWrite();
    drawSlider(id);
    ui_gfx().endWrite();
}

void ScreenSettings::drawVersionInfo()
{
    int y = BACKLIGHT_Y;
    for (int id = 1; id < SLIDER_COUNT; id++) {
        if (sliderVisible(id)) y += SLIDER_ROW_PITCH;
    }
    ui_gfx().setTextSize(SECTION_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    ui_gfx().setCursor(BACKLIGHT_LABEL_X, y + VERSION_GAP);
    ui_gfx().print("Version: ");
    ui_gfx().print(FIRMWARE_VERSION);
}

int ScreenSettings::hitTestSlider(int x, int y) const
{
    if (x < BACKLIGHT_SLIDER_X || x >= BACKLIGHT_SLIDER_X + BACKLIGHT_SLIDER_W) return -1;
    for (int id = 0; id < SLIDER_COUNT; id++) {
        if (!sliderVisible(id)) continue;
        int sy = sliderY(id);
        if (y >= sy && y < sy + BACKLIGHT_SLIDER_H) return id;
    }
    return -1;
}

void ScreenSettings::handleSliderTouch(int id, int x)
{
    // Calculate new value
    int relX = x - BACKLIGHT_SLIDER_X;
    if (relX < 0) relX = 0;
    if (relX > BACKLIGHT_SLIDER_W) relX = BACKLIGHT_SLIDER_W;

    uint8_t newLevel = (relX * 100) / BACKLIGHT_SLIDER_W;

    if (id == SLIDER_BACKLIGHT) {
        if (newLevel == m_backlight) return;
        m_backlight = newLevel;

        // Apply backlight
        int brightness = (m_backlight * 255) / 100;
        M5.Lcd.setBrightness(brightness);
    } else {
        board_audio_output_t out = (id == SLIDER_SPEAKER) ? BOARD_AUDIO_SPEAKER : BOARD_AUDIO_HEADPHONE;
        if (newLevel == board_audio_get_volume(out)) return;
        board_audio_set_volume(out, newLevel);   // applied to the codec / gain at once
    }

    drawSliderPartial(id);
}

void ScreenSettings::onTouch(int touchId, int x, int y, bool pressed)
{
    if (pressed) {
        if (m_dragSlider >= 0) return;  // one finger per slider drag
        int id = hitTestSlider(x, y);
        if (id < 0) return;
        m_dragSlider = id;
        m_dragTouchId = touchId;
        handleSliderTouch(id, x);
    } else if (touchId == m_dragTouchId && m_dragSlider >= 0) {
        // Volumes are kept in NVS; write once the finger lifts
        if (m_dragSlider != SLIDER_BACKLIGHT) board_audio_save();
        m_dragSlider = -1;
        m_dragTouchId = -1;
    }
}

void ScreenSettings::onTouchMove(int touchId, int x, int y)
{
    (void)y;    // keep following the slider the touch started on
    if (touchId == m_dragTouchId && m_dragSlider >= 0) {
        handleSliderTouch(m_dragSlider, x);
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
        drawSliderPartial(SLIDER_BACKLIGHT);
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
