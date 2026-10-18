#include "sdkconfig.h"
#include "screen_midi_info.h"
#include "ui_strip.h"
#include "usb_midi_host.h"
#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
#include "usb_midi_device.h"
#include "esp_timer.h"
#endif
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

// Layout constants - Board-specific
#if defined(UI_LAYOUT_LARGE)
static constexpr int CARD_TEXT_SIZE = 2;
static constexpr int CARD_START_Y = UI_CONTENT_Y + 20;
static constexpr int CARD_HEIGHT = 110;
static constexpr int CARD_SPACING = 15;
static constexpr int CARD_MARGIN = 20;
static constexpr int CARD_WIDTH = UI_SCREEN_WIDTH - 2 * CARD_MARGIN;
static constexpr int INDICATOR_RADIUS = 8;
#else
static constexpr int CARD_TEXT_SIZE = 1;
static constexpr int CARD_START_Y = UI_CONTENT_Y + 5;
static constexpr int CARD_HEIGHT = 52;
static constexpr int CARD_SPACING = 5;
static constexpr int CARD_MARGIN = 8;
static constexpr int CARD_WIDTH = UI_SCREEN_WIDTH - 2 * CARD_MARGIN;
static constexpr int INDICATOR_RADIUS = 4;
#endif

ScreenMidiInfo::ScreenMidiInfo()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_lastDiagMs(0)
{
}

void ScreenMidiInfo::enter()
{
    m_isActive = true;
}

void ScreenMidiInfo::leave()
{
    m_isActive = false;
}

void ScreenMidiInfo::update()
{
    if (!m_isActive) return;

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    // USB device diagnostics: refresh once a second
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - m_lastDiagMs >= 1000) {
        m_lastDiagMs = now;
        m_needsRedraw = true;
    }
#endif

    if (m_needsRedraw) {
        ui_strip::repaint_content([&]() { draw(); });
        m_needsRedraw = false;
    }
}

void ScreenMidiInfo::draw()
{
    int y = CARD_START_Y;

    drawUsbMidiCard();
    y += CARD_HEIGHT + CARD_SPACING;

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    // Both placeholders give way to the USB device diagnostics
    drawUsbDeviceDiag();
#else
    drawDinMidiCard();
    y += CARD_HEIGHT + CARD_SPACING;

    drawBleMidiCard();
#endif
}

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
// Diagnostics for the USB port in midi_device mode (TinyUSB CDC + MIDI):
// a snapshot of the stack state, so a wedged port can be inspected on the
// screen while the CDC console itself is unreachable.
void ScreenMidiInfo::drawUsbDeviceDiag()
{
    const int x = CARD_MARGIN;
    const int y = CARD_START_Y + CARD_HEIGHT + CARD_SPACING;
    const int h = 2 * CARD_HEIGHT + CARD_SPACING;
    const int lineH = 10 * CARD_TEXT_SIZE;

    usb_midi_device_stats_t st;
    USB_MIDI_DEVICE_get_stats(&st);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    auto ago = [now](uint32_t t) -> unsigned long {
        return t ? (unsigned long)((now - t) / 1000) : 0;
    };
    auto bits = [](uint8_t b, char* out) {
        for (int i = 0; i < 4; i++) out[i] = (b & (1 << i)) ? '1' : '0';
        out[4] = '\0';
    };
    char busy[5], stall[5];
    bits(st.ep_busy, busy);
    bits(st.ep_stalled, stall);

    char lines[8][64];
    snprintf(lines[0], sizeof(lines[0]), "USB device  up %lus  mnt %d susp %d dtr %d",
             (unsigned long)(now / 1000), st.mounted, st.suspended, st.cdc_connected);
    snprintf(lines[1], sizeof(lines[1]), "EP busy %s stall %s (MO MI CO CI)", busy, stall);
    snprintf(lines[2], sizeof(lines[2]), "ev %lu rst %lu sus %lu res %lu",
             (unsigned long)st.ev_total, (unsigned long)st.ev_bus_reset,
             (unsigned long)st.ev_suspend, (unsigned long)st.ev_resume);
    snprintf(lines[3], sizeof(lines[3]), "setup %lu xfer %lu last %lus ago",
             (unsigned long)st.ev_setup, (unsigned long)st.ev_xfer, ago(st.ev_last_ms));
    snprintf(lines[4], sizeof(lines[4]), "MIDI rx %lu tx %lu fail %lu drop %lu",
             (unsigned long)st.midi_rx_pkts, (unsigned long)st.midi_tx_ok,
             (unsigned long)st.midi_tx_fail, (unsigned long)st.midi_tx_drop);
    snprintf(lines[5], sizeof(lines[5]), "MIDI fifo %lu txq %lu cb %lus ago",
             (unsigned long)st.midi_rx_avail, (unsigned long)st.tx_queue_waiting,
             ago(st.tusb_task_ms));
    snprintf(lines[6], sizeof(lines[6]), "CDC rx %lu last %lus ago",
             (unsigned long)st.cdc_rx_bytes, ago(st.cdc_rx_last_ms));
    snprintf(lines[7], sizeof(lines[7]), "CDC rxfifo %lu txfree %lu",
             (unsigned long)st.cdc_rx_avail, (unsigned long)st.cdc_tx_free);

    ui_gfx().fillRoundRect(x, y, CARD_WIDTH, h, 4, UI_COLOR_DARKGRAY);
    ui_gfx().drawRoundRect(x, y, CARD_WIDTH, h, 4, UI_COLOR_GRAY);
    ui_gfx().setTextSize(CARD_TEXT_SIZE);
    for (int i = 0; i < 8; i++) {
        ui_gfx().setTextColor(i == 0 ? UI_COLOR_WHITE : UI_COLOR_CYAN, UI_COLOR_DARKGRAY);
        ui_gfx().setCursor(x + 6 * CARD_TEXT_SIZE, y + 4 * CARD_TEXT_SIZE + i * lineH);
        ui_gfx().print(lines[i]);
    }
}
#endif


void ScreenMidiInfo::drawDeviceCard(int y, const char* icon, const char* type,
                                    const char* name, bool inConnected, bool outConnected)
{
    int x = CARD_MARGIN;

    // Draw card background
    ui_gfx().fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    ui_gfx().drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    // Draw icon and type
    ui_gfx().setTextSize(CARD_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    ui_gfx().setCursor(x + 6 * CARD_TEXT_SIZE, y + 6 * CARD_TEXT_SIZE);
    ui_gfx().print(icon);
    ui_gfx().print(" ");
    ui_gfx().print(type);

    // Draw device name
    ui_gfx().setTextColor(UI_COLOR_CYAN, UI_COLOR_DARKGRAY);
    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 22 * CARD_TEXT_SIZE);

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
    ui_gfx().print(truncName);

    // Draw IN/OUT status
    ui_gfx().setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("IN ");

    // IN indicator
    uint16_t inColor = inConnected ? UI_COLOR_GREEN : UI_COLOR_GRAY;
    ui_gfx().fillCircle(x + 38 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, inColor);

    ui_gfx().setCursor(x + 60 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("OUT ");

    // OUT indicator
    uint16_t outColor = outConnected ? UI_COLOR_GREEN : UI_COLOR_GRAY;
    ui_gfx().fillCircle(x + 92 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, outColor);
}

void ScreenMidiInfo::drawUsbMidiCard()
{
    int y = CARD_START_Y;

    usb_midi_host_device_info_t info;
    bool connected = USB_MIDI_HOST_get_device_info(&info);

    if (connected) {
        // Use product name, or "USB MIDI Device" if empty
        const char* name = info.product[0] ? info.product : "USB MIDI Device";
        bool hasIn = info.midi_in_ep != 0;
        bool hasOut = info.midi_out_ep != 0;

        drawDeviceCard(y, "USB", "USB-MIDI", name, hasIn, hasOut);
    } else {
        // Draw disconnected card
        int x = CARD_MARGIN;
        ui_gfx().fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
        ui_gfx().drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

        ui_gfx().setTextSize(CARD_TEXT_SIZE);
        ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
        ui_gfx().setCursor(x + 6 * CARD_TEXT_SIZE, y + 6 * CARD_TEXT_SIZE);
        ui_gfx().print("USB USB-MIDI");

        ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 22 * CARD_TEXT_SIZE);
        ui_gfx().print("Not connected");

        ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
        ui_gfx().print("IN ");
        ui_gfx().fillCircle(x + 38 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
        ui_gfx().setCursor(x + 60 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
        ui_gfx().print("OUT ");
        ui_gfx().fillCircle(x + 92 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
    }
}

void ScreenMidiInfo::drawDinMidiCard()
{
    int y = CARD_START_Y + CARD_HEIGHT + CARD_SPACING;
    int x = CARD_MARGIN;

    // DIN-MIDI (SAM2695) - placeholder for future
    ui_gfx().fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    ui_gfx().drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    ui_gfx().setTextSize(CARD_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    ui_gfx().setCursor(x + 6 * CARD_TEXT_SIZE, y + 6 * CARD_TEXT_SIZE);
    ui_gfx().print("DIN DIN-MIDI");

    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 22 * CARD_TEXT_SIZE);
    ui_gfx().print("(Not implemented)");

    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("IN ");
    ui_gfx().fillCircle(x + 38 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
    ui_gfx().setCursor(x + 60 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("OUT ");
    ui_gfx().fillCircle(x + 92 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
}

void ScreenMidiInfo::drawBleMidiCard()
{
    int y = CARD_START_Y + 2 * (CARD_HEIGHT + CARD_SPACING);
    int x = CARD_MARGIN;

    // BLE-MIDI - placeholder for future
    ui_gfx().fillRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_DARKGRAY);
    ui_gfx().drawRoundRect(x, y, CARD_WIDTH, CARD_HEIGHT, 4, UI_COLOR_GRAY);

    ui_gfx().setTextSize(CARD_TEXT_SIZE);
    ui_gfx().setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
    ui_gfx().setCursor(x + 6 * CARD_TEXT_SIZE, y + 6 * CARD_TEXT_SIZE);
    ui_gfx().print("BLE BLE-MIDI");

    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 22 * CARD_TEXT_SIZE);
    ui_gfx().print("(Not implemented)");

    ui_gfx().setCursor(x + 12 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("IN ");
    ui_gfx().fillCircle(x + 38 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
    ui_gfx().setCursor(x + 60 * CARD_TEXT_SIZE, y + 38 * CARD_TEXT_SIZE);
    ui_gfx().print("OUT ");
    ui_gfx().fillCircle(x + 92 * CARD_TEXT_SIZE, y + 41 * CARD_TEXT_SIZE, INDICATOR_RADIUS, UI_COLOR_GRAY);
}

void ScreenMidiInfo::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;
    (void)x;
    (void)y;
    (void)pressed;
    // No touch handling for now
    // Future: tap on card to see more details
}

const char* ScreenMidiInfo::getTitle()
{
    return "MIDI Devices";
}

void ScreenMidiInfo::getTileStatus(char* buf, int len)
{
    snprintf(buf, len, "%s", ui_get_midi_indicator() ? "Connected" : "");
}
