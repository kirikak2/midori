#include "sdkconfig.h"
#include "screen_settings.h"
#include "ui_strip.h"
#include "board_audio.h"
#include "ble_midi_settings.h"
#include "ble_midi_device.h"
#include "ble_midi_host.h"
#include "esp_timer.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"


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

// One number for what the BLE-MIDI section shows (redrawn when it changes)
static uint32_t ble_status_key()
{
    ble_midi_device_stats_t ds;
    BLE_MIDI_DEVICE_stats(&ds);
    uint32_t k = (uint32_t)ds.state;
    k = k * 3 + (BLE_MIDI_DEVICE_is_enabled() ? 1 : 0);
    k = k * 3 + (BLE_MIDI_HOST_is_enabled() ? 1 : 0);
    for (int s = 0; s < BLE_MIDI_HOST_MAX_DEVICES; s++) {
        k = k * 3 + (BLE_MIDI_HOST_dev_connected(s) ? 1 : 0);
    }
    return k;
}

void ScreenSettings::update()
{
    if (!m_isActive) return;

    // BLE-MIDI state, twice a second
    static int64_t s_lastBleUs = 0;
    static uint32_t s_lastBleKey = 0xFFFFFFFFu;
    int64_t now = esp_timer_get_time();
    if (ble_midi_settings_available() && now - s_lastBleUs > 500000) {
        s_lastBleUs = now;
        uint32_t key = ble_status_key();
        if (key != s_lastBleKey) {
            s_lastBleKey = key;
            m_needsRedraw = true;
        }
    }

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

    int lx = x + 12 * SECTION_TEXT_SIZE;
    int ly = y + 24 * SECTION_TEXT_SIZE;
    int pitch = 14 * SECTION_TEXT_SIZE;
    if (!ble_midi_settings_available()) {
        ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
        ui_gfx().setCursor(lx, ly);
        ui_gfx().print("Not available on this board");
        return;
    }

    // Device role
    static const char* const STATES[] = { "off", "starting", "advertising", "linked", "connected", "error" };
    ble_midi_device_stats_t ds;
    BLE_MIDI_DEVICE_stats(&ds);
    int st = (int)ds.state;
    char line[64];
    snprintf(line, sizeof(line), "Device \"%s\": %s", BLE_MIDI_DEVICE_name(),
             (st >= 0 && st < 6) ? STATES[st] : "error");
    ui_gfx().setTextColor(ds.state == BLE_MIDI_DEVICE_CONNECTED ? UI_COLOR_CYAN : UI_COLOR_WHITE, UI_COLOR_BLACK);
    ui_gfx().setCursor(lx, ly);
    ui_gfx().print(line);

    // Host role
    int linked = 0;
    for (int s = 0; s < BLE_MIDI_HOST_MAX_DEVICES; s++) {
        if (BLE_MIDI_HOST_dev_connected(s)) linked++;
    }
    snprintf(line, sizeof(line), "Host: %s, %d connected", BLE_MIDI_HOST_is_enabled() ? "on" : "off", linked);
    ui_gfx().setTextColor(linked ? UI_COLOR_CYAN : UI_COLOR_WHITE, UI_COLOR_BLACK);
    ui_gfx().setCursor(lx, ly + pitch);
    ui_gfx().print(line);

    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    ui_gfx().setCursor(lx, ly + 2 * pitch);
    ui_gfx().print("Tap here (or Bluetooth) to pair and scan");
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
        if (id < 0) {
            // The BLE-MIDI section opens the Bluetooth screen
            if (ble_midi_settings_available() && y >= SECTION_START_Y && y < SECTION_START_Y + BLE_SECTION_HEIGHT) {
                UIManager::getInstance().requestScreen(UI_SCREEN_BLUETOOTH, -1);
            }
            return;
        }
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
    if (ble_midi_settings_available()) {
        UIManager::getInstance().requestScreen(UI_SCREEN_BLUETOOTH, -1);
    }
}

const char* ScreenSettings::getTitle()
{
    return "Settings";
}

const char* ScreenSettings::getNavCenterLabel()
{
    return ble_midi_settings_available() ? "Bluetooth" : nullptr;
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
