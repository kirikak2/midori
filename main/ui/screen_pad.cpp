#include "screen_pad.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "SCREEN_PAD";

// Global instance
static ScreenPads s_screenPads;

ScreenPads& getScreenPads()
{
    return s_screenPads;
}

// Layout constants
static constexpr int PAD_START_X = 10;
static constexpr int PAD_START_Y = UI_CONTENT_Y + 8;
static constexpr int PAD_SPACING_X = 5;
static constexpr int PAD_SPACING_Y = 5;

ScreenPads::ScreenPads()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_pressedPad(-1)
{
}

void ScreenPads::enter()
{
    m_isActive = true;
    m_pressedPad = -1;
    ui_clear_content_area();
    draw();
}

void ScreenPads::leave()
{
    m_isActive = false;

    // Release any pressed pad
    if (m_pressedPad >= 0) {
        handlePadPress(m_pressedPad, false);
        m_pressedPad = -1;
    }
}

void ScreenPads::update()
{
    if (!m_isActive) return;

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

void ScreenPads::draw()
{
    drawAllPads();
}

void ScreenPads::getPadRect(int index, int& x, int& y, int& w, int& h)
{
    int col = index % UI_PAD_COLS;
    int row = index / UI_PAD_COLS;

    w = UI_PAD_WIDTH;
    h = UI_PAD_HEIGHT;
    x = PAD_START_X + col * (UI_PAD_WIDTH + PAD_SPACING_X);
    y = PAD_START_Y + row * (UI_PAD_HEIGHT + PAD_SPACING_Y);
}

void ScreenPads::drawPad(int index)
{
    if (index < 0 || index >= UI_PAD_COUNT) return;

    int x, y, w, h;
    getPadRect(index, x, y, w, h);

    const pad_config_t* pad = ui_pad_get_config(index);
    if (!pad) return;

    // Determine colors
    uint16_t bgColor;
    uint16_t textColor = UI_COLOR_WHITE;
    uint16_t borderColor = UI_COLOR_WHITE;

    if (!pad->assigned) {
        // Unassigned pad - gray
        bgColor = UI_COLOR_DARKGRAY;
        textColor = UI_COLOR_GRAY;
    } else {
        bgColor = pad->color;

        // Brighten if pressed or toggle ON
        bool highlighted = false;
        if (pad->type == PAD_TYPE_TOGGLE && pad->state) {
            highlighted = true;
        } else if ((pad->type == PAD_TYPE_MOMENTARY || pad->type == PAD_TYPE_TRIGGER) && pad->state) {
            highlighted = true;
        }

        if (highlighted) {
            bgColor = ui_lighten_color(bgColor);
            borderColor = UI_COLOR_YELLOW;
        }
    }

    // Draw pad background
    M5.Lcd.fillRoundRect(x, y, w, h, 6, bgColor);
    M5.Lcd.drawRoundRect(x, y, w, h, 6, borderColor);

    // Draw label
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(textColor, bgColor);

    const char* label = pad->assigned ? pad->label : "";
    if (!pad->assigned) {
        // Show pad number for unassigned
        char numStr[8];
        snprintf(numStr, sizeof(numStr), "Pad %d", index + 1);
        label = numStr;

        int textW = strlen(label) * 6;
        M5.Lcd.setCursor(x + (w - textW) / 2, y + (h - 8) / 2);
        M5.Lcd.print(label);
    } else {
        int textW = strlen(label) * 6;
        M5.Lcd.setCursor(x + (w - textW) / 2, y + (h - 8) / 2);
        M5.Lcd.print(label);

        // Show type indicator in corner for assigned pads
        const char* typeStr = nullptr;
        switch (pad->type) {
            case PAD_TYPE_TRIGGER:   typeStr = "T"; break;
            case PAD_TYPE_MOMENTARY: typeStr = "M"; break;
            case PAD_TYPE_TOGGLE:    typeStr = pad->state ? "ON" : "OFF"; break;
        }
        if (typeStr) {
            M5.Lcd.setTextColor(UI_COLOR_GRAY, bgColor);
            M5.Lcd.setCursor(x + 4, y + 4);
            M5.Lcd.print(typeStr);
        }
    }
}

void ScreenPads::drawAllPads()
{
    for (int i = 0; i < UI_PAD_COUNT; i++) {
        drawPad(i);
    }
}

int ScreenPads::hitTestPad(int x, int y)
{
    for (int i = 0; i < UI_PAD_COUNT; i++) {
        int px, py, pw, ph;
        getPadRect(i, px, py, pw, ph);

        if (x >= px && x < px + pw && y >= py && y < py + ph) {
            return i;
        }
    }
    return -1;
}

void ScreenPads::onTouch(int x, int y, bool pressed)
{
    int padIndex = hitTestPad(x, y);

    if (pressed) {
        if (padIndex >= 0 && padIndex != m_pressedPad) {
            // Release previous pad if any
            if (m_pressedPad >= 0) {
                handlePadPress(m_pressedPad, false);
            }
            m_pressedPad = padIndex;
            handlePadPress(padIndex, true);
        }
    } else {
        // Release
        if (m_pressedPad >= 0) {
            handlePadPress(m_pressedPad, false);
            m_pressedPad = -1;
        }
    }
}

void ScreenPads::handlePadPress(int padIndex, bool pressed)
{
    if (padIndex < 0 || padIndex >= UI_PAD_COUNT) return;

    const pad_config_t* pad = ui_pad_get_config(padIndex);
    if (!pad || !pad->assigned) return;

    bool stateChanged = false;
    bool oldState = pad->state;

    switch (pad->type) {
        case PAD_TYPE_TRIGGER:
            if (pressed) {
                // Trigger fires on press only
                ui_pad_set_state(padIndex, true);
                stateChanged = true;
            } else {
                ui_pad_set_state(padIndex, false);
            }
            break;

        case PAD_TYPE_MOMENTARY:
            // State follows press state
            ui_pad_set_state(padIndex, pressed);
            stateChanged = true;
            break;

        case PAD_TYPE_TOGGLE:
            if (pressed) {
                // Toggle on press
                ui_pad_set_state(padIndex, !oldState);
                stateChanged = true;
            }
            break;
    }

    // Redraw the pad
    drawPad(padIndex);

    // Call callback if registered
    if (stateChanged) {
        pad_event_cb_t callback = UIManager::getInstance().getPadEventCallback();
        if (callback) {
            // For trigger, we fire on press with state=true
            // For momentary, we fire on both press and release
            // For toggle, we fire on press with new state
            if (pad->type == PAD_TYPE_TRIGGER) {
                if (pressed) {
                    callback(padIndex, true);
                }
            } else {
                callback(padIndex, ui_pad_get_state(padIndex));
            }
        }
    }
}

const char* ScreenPads::getTitle()
{
    return "Pads";
}
