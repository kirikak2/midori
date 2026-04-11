#include "sdkconfig.h"
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

// Layout constants - dynamically calculated for different screen sizes
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
// Tab5: larger spacing and text for bigger screen
static constexpr int PAD_TEXT_SIZE = 3;
static constexpr int PAD_SPACING_X = 20;
static constexpr int PAD_SPACING_Y = 20;
// Center the grid horizontally
static constexpr int GRID_WIDTH = UI_PAD_COLS * UI_PAD_WIDTH + (UI_PAD_COLS - 1) * PAD_SPACING_X;
static constexpr int PAD_START_X = (UI_SCREEN_WIDTH - GRID_WIDTH) / 2;
// Center the grid vertically in content area
static constexpr int GRID_HEIGHT = UI_PAD_ROWS * UI_PAD_HEIGHT + (UI_PAD_ROWS - 1) * PAD_SPACING_Y;
static constexpr int PAD_START_Y = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - GRID_HEIGHT) / 2;
#else
// CoreS3: original layout
static constexpr int PAD_TEXT_SIZE = 1;
static constexpr int PAD_START_X = 10;
static constexpr int PAD_START_Y = UI_CONTENT_Y + 8;
static constexpr int PAD_SPACING_X = 5;
static constexpr int PAD_SPACING_Y = 5;
#endif

ScreenPads::ScreenPads()
    : m_isActive(false)
    , m_needsRedraw(false)
{
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchToPad[i] = -1;
    }
}

void ScreenPads::enter()
{
    m_isActive = true;
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchToPad[i] = -1;
    }
    ui_clear_content_area();
    draw();
}

void ScreenPads::leave()
{
    m_isActive = false;

    // Release all pressed pads
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (m_touchToPad[i] >= 0) {
            handlePadPress(m_touchToPad[i], false);
            m_touchToPad[i] = -1;
        }
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
    M5.Lcd.setTextSize(PAD_TEXT_SIZE);
    M5.Lcd.setTextColor(textColor, bgColor);

    const char* label = pad->assigned ? pad->label : "";
    if (!pad->assigned) {
        // Show pad number for unassigned
        char numStr[8];
        snprintf(numStr, sizeof(numStr), "Pad %d", index + 1);
        label = numStr;

        int textW = strlen(label) * 6 * PAD_TEXT_SIZE;
        int textH = 8 * PAD_TEXT_SIZE;
        M5.Lcd.setCursor(x + (w - textW) / 2, y + (h - textH) / 2);
        M5.Lcd.print(label);
    } else {
        int textW = strlen(label) * 6 * PAD_TEXT_SIZE;
        int textH = 8 * PAD_TEXT_SIZE;
        M5.Lcd.setCursor(x + (w - textW) / 2, y + (h - textH) / 2);
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
            M5.Lcd.setCursor(x + 4 * PAD_TEXT_SIZE, y + 4 * PAD_TEXT_SIZE);
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

void ScreenPads::onTouch(int touchId, int x, int y, bool pressed)
{
    if (touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;

    int padIndex = hitTestPad(x, y);

    if (pressed) {
        if (padIndex >= 0) {
            // Check if this touch is already pressing a different pad
            if (m_touchToPad[touchId] >= 0 && m_touchToPad[touchId] != padIndex) {
                // Release the old pad first
                handlePadPress(m_touchToPad[touchId], false);
            }

            // Only press if not already pressed by another touch
            bool alreadyPressed = false;
            for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
                if (i != touchId && m_touchToPad[i] == padIndex) {
                    alreadyPressed = true;
                    break;
                }
            }

            if (!alreadyPressed && m_touchToPad[touchId] != padIndex) {
                m_touchToPad[touchId] = padIndex;
                handlePadPress(padIndex, true);
            } else {
                m_touchToPad[touchId] = padIndex;
            }
        }
    } else {
        // Release
        int releasedPad = m_touchToPad[touchId];
        m_touchToPad[touchId] = -1;

        if (releasedPad >= 0) {
            // Only release if no other touch is pressing the same pad
            bool stillPressed = false;
            for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
                if (m_touchToPad[i] == releasedPad) {
                    stillPressed = true;
                    break;
                }
            }

            if (!stillPressed) {
                handlePadPress(releasedPad, false);
            }
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

    // Push event to queue for Ruby to consume
    ui_event_t event;
    event.type = pressed ? UI_EVENT_PAD_PRESS : UI_EVENT_PAD_RELEASE;
    event.data.pad.index = padIndex;
    event.data.pad.state = ui_pad_get_state(padIndex);
    ui_event_push(&event);

    // Call callback if registered (legacy support)
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
