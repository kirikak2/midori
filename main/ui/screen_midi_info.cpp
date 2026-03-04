#include "screen_midi_info.h"
#include "usb_midi.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"

static const char* TAG = "SCREEN_MIDI_INFO";

// Global instance
static ScreenMidiInfo s_screenMidiInfo;

ScreenMidiInfo& getScreenMidiInfo()
{
    return s_screenMidiInfo;
}

// Layout constants
static constexpr int CARD_START_Y = UI_CONTENT_Y + 5;
static constexpr int CARD_HEIGHT = 52;
static constexpr int CARD_SPACING = 5;
static constexpr int CARD_MARGIN = 8;
static constexpr int CARD_WIDTH = UI_SCREEN_WIDTH - 2 * CARD_MARGIN;

ScreenMidiInfo::ScreenMidiInfo()
    : m_isActive(false)
    , m_needsRedraw(false)
{
}

void ScreenMidiInfo::enter()
{
    m_isActive = true;
    ui_clear_content_area();
    draw();
}

void ScreenMidiInfo::leave()
{
    m_isActive = false;
}

void ScreenMidiInfo::update()
{
    if (!m_isActive) return;

    // Could check for device connection changes here
    // For now, redraw periodically could be added

    if (m_needsRedraw) {
        draw();
        m_needsRedraw = false;
    }
}

void ScreenMidiInfo::draw()
{
    int y = CARD_START_Y;

    drawUsbMidiCard();
    y += CARD_HEIGHT + CARD_SPACING;

    drawDinMidiCard();
    y += CARD_HEIGHT + CARD_SPACING;

    drawBleMidiCard();
}

void ScreenMidiInfo::drawDeviceCard(int y, const char* icon, const char* type,
                                    const char* name, bool inConnected, bool outConnected)
{
    int x = CARD_MARGIN;

    // Draw card background
    M5.Lcd.fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    M5.Lcd.drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    // Draw icon and type
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 6, y + 6);
    M5.Lcd.print(icon);
    M5.Lcd.print(" ");
    M5.Lcd.print(type);

    // Draw device name
    M5.Lcd.setTextColor(UI_COLOR_CYAN, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 12, y + 22);

    // Truncate name if too long
    char truncName[32];
    strncpy(truncName, name, 31);
    truncName[31] = '\0';
    if (strlen(name) > 30) {
        truncName[27] = '.';
        truncName[28] = '.';
        truncName[29] = '.';
        truncName[30] = '\0';
    }
    M5.Lcd.print(truncName);

    // Draw IN/OUT status
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 12, y + 38);
    M5.Lcd.print("IN ");

    // IN indicator
    uint16_t inColor = inConnected ? UI_COLOR_GREEN : UI_COLOR_GRAY;
    M5.Lcd.fillCircle(x + 38, y + 41, 4, inColor);

    M5.Lcd.setCursor(x + 60, y + 38);
    M5.Lcd.print("OUT ");

    // OUT indicator
    uint16_t outColor = outConnected ? UI_COLOR_GREEN : UI_COLOR_GRAY;
    M5.Lcd.fillCircle(x + 92, y + 41, 4, outColor);
}

void ScreenMidiInfo::drawUsbMidiCard()
{
    int y = CARD_START_Y;

    usb_midi_device_info_t info;
    bool connected = USB_MIDI_get_device_info(&info);

    if (connected) {
        // Use product name, or "USB MIDI Device" if empty
        const char* name = info.product[0] ? info.product : "USB MIDI Device";
        bool hasIn = info.midi_in_ep != 0;
        bool hasOut = info.midi_out_ep != 0;

        drawDeviceCard(y, "USB", "USB-MIDI", name, hasIn, hasOut);
    } else {
        // Draw disconnected card
        int x = CARD_MARGIN;
        M5.Lcd.fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
        M5.Lcd.drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
        M5.Lcd.setCursor(x + 6, y + 6);
        M5.Lcd.print("USB USB-MIDI");

        M5.Lcd.setCursor(x + 12, y + 22);
        M5.Lcd.print("Not connected");

        M5.Lcd.setCursor(x + 12, y + 38);
        M5.Lcd.print("IN ");
        M5.Lcd.fillCircle(x + 38, y + 41, 4, UI_COLOR_GRAY);
        M5.Lcd.setCursor(x + 60, y + 38);
        M5.Lcd.print("OUT ");
        M5.Lcd.fillCircle(x + 92, y + 41, 4, UI_COLOR_GRAY);
    }
}

void ScreenMidiInfo::drawDinMidiCard()
{
    int y = CARD_START_Y + CARD_HEIGHT + CARD_SPACING;
    int x = CARD_MARGIN;

    // DIN-MIDI (SAM2695) - placeholder for future
    M5.Lcd.fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    M5.Lcd.drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 6, y + 6);
    M5.Lcd.print("DIN DIN-MIDI");

    M5.Lcd.setCursor(x + 12, y + 22);
    M5.Lcd.print("(Not implemented)");

    M5.Lcd.setCursor(x + 12, y + 38);
    M5.Lcd.print("IN ");
    M5.Lcd.fillCircle(x + 38, y + 41, 4, UI_COLOR_GRAY);
    M5.Lcd.setCursor(x + 60, y + 38);
    M5.Lcd.print("OUT ");
    M5.Lcd.fillCircle(x + 92, y + 41, 4, UI_COLOR_GRAY);
}

void ScreenMidiInfo::drawBleMidiCard()
{
    int y = CARD_START_Y + 2 * (CARD_HEIGHT + CARD_SPACING);
    int x = CARD_MARGIN;

    // BLE-MIDI - placeholder for future
    M5.Lcd.fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    M5.Lcd.drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    M5.Lcd.setCursor(x + 6, y + 6);
    M5.Lcd.print("BLE BLE-MIDI");

    M5.Lcd.setCursor(x + 12, y + 22);
    M5.Lcd.print("(Not implemented)");

    M5.Lcd.setCursor(x + 12, y + 38);
    M5.Lcd.print("IN ");
    M5.Lcd.fillCircle(x + 38, y + 41, 4, UI_COLOR_GRAY);
    M5.Lcd.setCursor(x + 60, y + 38);
    M5.Lcd.print("OUT ");
    M5.Lcd.fillCircle(x + 92, y + 41, 4, UI_COLOR_GRAY);
}

void ScreenMidiInfo::onTouch(int x, int y, bool pressed)
{
    // No touch handling for now
    // Future: tap on card to see more details
}

const char* ScreenMidiInfo::getTitle()
{
    return "MIDI Devices";
}
