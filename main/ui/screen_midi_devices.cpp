// MIDI Devices screen: a crossbar of the devices in ui_midi_devices.cpp
// (docs/UI_MIDI_DEVICES.md).
//
//   rows    = devices with a MIDI IN (route sources), then "Prog"
//   columns = devices with a MIDI OUT, then "Prog"
//
// A cell between two devices is a route in picoruby-midi's table -- tap to
// patch / unpatch, hold for a channel. The Prog column picks the program's
// IN, the Prog row its OUT. Hold a device's name for its details.

#include "sdkconfig.h"
#include "screen_midi_devices.h"
#include "ui_strip.h"
#include "midi_route.h"
#include "midi_transport.h"
#include "usb_midi_host.h"
#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
#include "usb_midi_device.h"
#endif
#include "esp_timer.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>

// Global instance
static ScreenMidiDevices s_screenMidiDevices;

ScreenMidiDevices& getScreenMidiDevices()
{
    return s_screenMidiDevices;
}

// Layout constants - Board-specific
#if defined(UI_LAYOUT_LARGE)
static constexpr int TEXT_SIZE = 2;
static constexpr int MARGIN = 16;
static constexpr int NAME_W = 240;
static constexpr int HEADER_H = 40;
static constexpr int CELL_W_MAX = 140;
static constexpr int CELL_H_MAX = 64;
static constexpr int DOT_R = 7;
static constexpr int PICK_BTN_W = 120;
static constexpr int PICK_BTN_H = 56;
static constexpr int MOVE_SLOP = 24;
#else
static constexpr int TEXT_SIZE = 1;
static constexpr int MARGIN = 4;
static constexpr int NAME_W = 76;
static constexpr int HEADER_H = 16;
static constexpr int CELL_W_MAX = 44;
static constexpr int CELL_H_MAX = 24;
static constexpr int DOT_R = 3;
static constexpr int PICK_BTN_W = 44;
static constexpr int PICK_BTN_H = 30;
static constexpr int MOVE_SLOP = 10;
#endif

static constexpr int CHAR_W = 6 * TEXT_SIZE;
static constexpr int CHAR_H = 8 * TEXT_SIZE;
static constexpr uint32_t CHECK_MS = 200;
static constexpr uint32_t LONG_PRESS_MS = 500;
static constexpr uint16_t CELL_BG_A = 0x1082;
static constexpr uint16_t CELL_BG_B = 0x18E3;
static constexpr uint16_t PROG_COLOR = UI_COLOR_CYAN;

// Channel picker: "All", 1..16, "Off" on a 6 x 3 grid
static constexpr int PICK_COLS = 6;
static constexpr int PICK_ROWS = 3;
static constexpr int PICK_OFF = -3;

static void print_fit(const char* text, int maxChars)
{
    char buf[32];
    int n = (int)strlen(text);
    if (maxChars > (int)sizeof(buf) - 1) maxChars = sizeof(buf) - 1;
    if (maxChars < 1) return;
    if (n > maxChars) n = maxChars;
    memcpy(buf, text, n);
    buf[n] = '\0';
    ui_gfx().print(buf);
}

ScreenMidiDevices::ScreenMidiDevices()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_lastCheckMs(0)
    , m_lastDiagMs(0)
    , m_signature(0)
    , m_grid()
    , m_touchId(-1)
    , m_touchX(0)
    , m_touchY(0)
    , m_touchMs(0)
    , m_longFired(false)
    , m_overlay(OVERLAY_NONE)
    , m_overlayFrom(-1)
    , m_overlayTo(-1)
    , m_overlayDev(-1)
{
}

void ScreenMidiDevices::enter()
{
    m_isActive = true;
    m_overlay = OVERLAY_NONE;
    m_touchId = -1;
    rebuild();
    m_signature = signature();
}

void ScreenMidiDevices::leave()
{
    m_isActive = false;
    m_overlay = OVERLAY_NONE;
}

// --- Model -----------------------------------------------------------------------

bool ScreenMidiDevices::isUp(const ui_mididev_t& d) const
{
    return d.bit != 0 && (ui_mididev_connected_mask() & d.bit) != 0;
}

void ScreenMidiDevices::displayName(const ui_mididev_t& d, char* buf, int len) const
{
    // The USB host port is named after what is plugged in
    if (strcmp(d.key, "usb_host") == 0 && isUp(d)) {
        usb_midi_host_device_info_t info;
        if (USB_MIDI_HOST_get_device_info(&info) && info.product[0]) {
            snprintf(buf, len, "%s", info.product);
            return;
        }
    }
    snprintf(buf, len, "%s", d.name);
}

void ScreenMidiDevices::rebuild()
{
    Grid& g = m_grid;
    g.count = 0;
    int n = ui_mididev_count();
    for (int i = 0; i < n && g.count < MAX_DEVS; i++) {
        if (ui_mididev_get(i, &g.devs[g.count])) g.count++;
    }
    uint8_t sources = MIDI_route_sources();
    g.rowCount = 0;
    g.colCount = 0;
    for (int i = 0; i < g.count; i++) {
        if (g.devs[i].bit && (g.devs[i].bit & sources)) g.rows[g.rowCount++] = i;
        g.cols[g.colCount++] = i;   // Every transport (and a standby one) takes MIDI
    }

    g.nameW = NAME_W;
    g.headerH = HEADER_H;
    int availW = UI_SCREEN_WIDTH - 2 * MARGIN - g.nameW;
    int availH = UI_CONTENT_HEIGHT - 2 * MARGIN - g.headerH;
    g.cellW = availW / (g.colCount + 1);
    g.cellH = availH / (g.rowCount + 1);
    if (g.cellW > CELL_W_MAX) g.cellW = CELL_W_MAX;
    if (g.cellH > CELL_H_MAX) g.cellH = CELL_H_MAX;
    g.x0 = MARGIN + g.nameW;
    g.y0 = UI_CONTENT_Y + MARGIN + g.headerH;
}

uint32_t ScreenMidiDevices::signature() const
{
    // FNV-1a over everything the grid shows: the model, connections, the route
    // table (a script may route behind the model's back), filters, selection
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; i++) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 16777619u;
        }
    };
    mix(ui_mididev_serial());
    mix(ui_mididev_connected_mask());
    mix((uint32_t)ui_mididev_selected(false) + 1);
    mix((uint32_t)ui_mididev_selected(true) + 1);
    mix(MIDI_route_sources());
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        uint8_t src = 0, dst = 0;
        int ch = 0;
        if (MIDI_route_get(i, &src, &dst, &ch)) mix(src | (dst << 8) | ((ch & 0xFF) << 16));
        else mix(0);
    }
    for (int bit = 1; bit < 0x100; bit <<= 1) mix(MIDI_route_filtered((uint8_t)bit));
    return h;
}

void ScreenMidiDevices::update()
{
    if (!m_isActive) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    // Long press: a finger held still
    if (m_touchId >= 0 && !m_longFired && now - m_touchMs >= LONG_PRESS_MS) {
        m_longFired = true;
        longPress(m_touchX, m_touchY);
    }

    if (now - m_lastCheckMs >= CHECK_MS) {
        m_lastCheckMs = now;
        uint32_t sig = signature();
        if (sig != m_signature) {
            m_signature = sig;
            rebuild();
            m_needsRedraw = true;
        }
    }

#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    // The USB device's diagnostics refresh once a second while shown
    if (m_overlay == OVERLAY_DETAIL && m_overlayDev >= 0 && m_overlayDev < m_grid.count
        && strcmp(m_grid.devs[m_overlayDev].key, "usb_dev") == 0
        && now - m_lastDiagMs >= 1000) {
        m_lastDiagMs = now;
        m_needsRedraw = true;
    }
#endif

    if (m_needsRedraw) {
        ui_strip::repaint_content([&]() { draw(); });
        m_needsRedraw = false;
    }
}

// --- Drawing -----------------------------------------------------------------------

void ScreenMidiDevices::draw()
{
    drawGrid();
    if (m_overlay == OVERLAY_CHANNEL) drawChannelPicker();
    if (m_overlay == OVERLAY_DETAIL) drawDetail();
}

void ScreenMidiDevices::drawGrid()
{
    const Grid& g = m_grid;
    auto& gfx = ui_gfx();
    gfx.fillRect(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT, UI_COLOR_BLACK);
    gfx.setTextSize(TEXT_SIZE);

    if (g.count == 0) {
        gfx.setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
        gfx.setCursor(MARGIN, UI_CONTENT_Y + MARGIN);
        gfx.print("No MIDI devices");
        return;
    }

    char name[40];
    int colChars = (g.cellW - 2) / CHAR_W;

    // Column headers (OUT)
    for (int c = 0; c <= g.colCount; c++) {
        int x = g.x0 + c * g.cellW;
        const char* label = "Prog";
        uint16_t color = PROG_COLOR;
        if (c < g.colCount) {
            const ui_mididev_t& d = g.devs[g.cols[c]];
            displayName(d, name, sizeof(name));
            label = name;
            color = isUp(d) ? UI_COLOR_WHITE : UI_COLOR_GRAY;
        }
        int n = (int)strlen(label);
        if (n > colChars) n = colChars;
        gfx.setTextColor(color, UI_COLOR_BLACK);
        gfx.setCursor(x + (g.cellW - n * CHAR_W) / 2, g.y0 - g.headerH + (g.headerH - CHAR_H) / 2);
        print_fit(label, colChars);
    }

    // Row headers (IN)
    int nameChars = (g.nameW - 4 * DOT_R) / CHAR_W;
    for (int r = 0; r <= g.rowCount; r++) {
        int y = g.y0 + r * g.cellH;
        int ty = y + (g.cellH - CHAR_H) / 2;
        if (r == g.rowCount) {
            gfx.setTextColor(PROG_COLOR, UI_COLOR_BLACK);
            gfx.setCursor(MARGIN + 3 * DOT_R, ty);
            gfx.print("Prog");
            continue;
        }
        const ui_mididev_t& d = g.devs[g.rows[r]];
        bool up = isUp(d);
        gfx.fillCircle(MARGIN + DOT_R, y + g.cellH / 2, DOT_R, up ? UI_COLOR_GREEN : UI_COLOR_DARKGRAY);
        displayName(d, name, sizeof(name));
        gfx.setTextColor(up ? UI_COLOR_WHITE : UI_COLOR_GRAY, UI_COLOR_BLACK);
        gfx.setCursor(MARGIN + 3 * DOT_R, ty);
        print_fit(name, nameChars);
    }

    // Device status for the OUT-only ones (AMY): a dot under the header
    for (int c = 0; c < g.colCount; c++) {
        const ui_mididev_t& d = g.devs[g.cols[c]];
        uint16_t color = (d.flags & UI_MIDIDEV_STANDBY) ? UI_COLOR_YELLOW
                       : isUp(d) ? UI_COLOR_GREEN : UI_COLOR_DARKGRAY;
        gfx.fillRect(g.x0 + c * g.cellW + 2, g.y0 - 3, g.cellW - 4, 2, color);
    }

    for (int r = 0; r <= g.rowCount; r++) {
        for (int c = 0; c <= g.colCount; c++) drawCell(r, c);
    }

    // Separators before the Prog row / column
    int gridW = (g.colCount + 1) * g.cellW;
    int gridH = (g.rowCount + 1) * g.cellH;
    gfx.drawFastVLine(g.x0 + g.colCount * g.cellW, g.y0 - g.headerH, gridH + g.headerH, PROG_COLOR);
    gfx.drawFastHLine(MARGIN, g.y0 + g.rowCount * g.cellH, g.nameW + gridW - MARGIN, PROG_COLOR);
}

void ScreenMidiDevices::drawCell(int row, int col)
{
    const Grid& g = m_grid;
    auto& gfx = ui_gfx();
    int x = g.x0 + col * g.cellW;
    int y = g.y0 + row * g.cellH;
    int cx = x + g.cellW / 2;
    int cy = y + g.cellH / 2;
    int r = (g.cellW < g.cellH ? g.cellW : g.cellH) / 4;
    if (r < 3) r = 3;
    bool progRow = (row == g.rowCount);
    bool progCol = (col == g.colCount);

    gfx.fillRect(x + 1, y + 1, g.cellW - 2, g.cellH - 2, ((row + col) & 1) ? CELL_BG_B : CELL_BG_A);
    if (progRow && progCol) return;

    // Prog: the program's IN (column) / OUT (row), one of each
    if (progRow || progCol) {
        int dev = progCol ? g.rows[row] : g.cols[col];
        bool selected = ui_mididev_selected(progRow) == dev;
        gfx.drawCircle(cx, cy, r, selected ? PROG_COLOR : (uint16_t)UI_COLOR_DARKGRAY);
        if (selected) gfx.fillCircle(cx, cy, r / 2 > 1 ? r / 2 : 2, PROG_COLOR);
        return;
    }

    int from = g.rows[row];
    int to = g.cols[col];
    if (from == to) {
        gfx.drawFastHLine(cx - r, cy, 2 * r, UI_COLOR_DARKGRAY);
        return;
    }

    int ch = ui_mididev_patch_channel(from, to);
    if (ch == -2) {
        gfx.fillCircle(cx, cy, 1, UI_COLOR_DARKGRAY);
        return;
    }
    const ui_mididev_t& a = g.devs[from];
    const ui_mididev_t& b = g.devs[to];
    bool active = isUp(a) && isUp(b);
    uint16_t color = a.color ? a.color : (uint16_t)UI_COLOR_GREEN;
    if (active) {
        gfx.fillCircle(cx, cy, r, color);
    } else {
        gfx.drawCircle(cx, cy, r, UI_COLOR_GRAY);   // Patched, but an end is down
    }
    if (ch >= 0) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", (ch & 0x0F) + 1);
        gfx.setTextSize(1);
        gfx.setTextColor(UI_COLOR_WHITE);
        gfx.setCursor(x + 2, y + g.cellH - 9);
        gfx.print(buf);
        gfx.setTextSize(TEXT_SIZE);
    }
    // A Ruby filter (MIDI.filter) on this route
    if (a.bit && b.bit && (MIDI_route_filtered(a.bit) & b.bit)) {
        gfx.setTextSize(1);
        gfx.setTextColor(UI_COLOR_YELLOW);
        gfx.setCursor(x + g.cellW - 8, y + 2);
        gfx.print("f");
        gfx.setTextSize(TEXT_SIZE);
    }
}

// Channel picker geometry (shared by drawing and hit testing)
static void picker_rect(int* px, int* py, int* pw, int* ph)
{
    *pw = PICK_COLS * PICK_BTN_W + (PICK_COLS + 1) * MARGIN;
    *ph = PICK_ROWS * PICK_BTN_H + (PICK_ROWS + 1) * MARGIN + CHAR_H + MARGIN;
    *px = (UI_SCREEN_WIDTH - *pw) / 2;
    *py = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - *ph) / 2;
}

static int picker_value(int i)
{
    if (i == 0) return -1;                 // All
    if (i <= 16) return i - 1;             // 1..16
    return PICK_OFF;                       // Off
}

void ScreenMidiDevices::drawChannelPicker()
{
    auto& gfx = ui_gfx();
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph);
    gfx.fillRoundRect(px, py, pw, ph, 6, UI_COLOR_DARKGRAY);
    gfx.drawRoundRect(px, py, pw, ph, 6, UI_COLOR_WHITE);

    char from[24], to[24], title[64];
    displayName(m_grid.devs[m_overlayFrom], from, sizeof(from));
    displayName(m_grid.devs[m_overlayTo], to, sizeof(to));
    snprintf(title, sizeof(title), "%s > %s", from, to);
    gfx.setTextSize(TEXT_SIZE);
    gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    gfx.setCursor(px + MARGIN, py + MARGIN);
    print_fit(title, (pw - 2 * MARGIN) / CHAR_W);

    int current = ui_mididev_patch_channel(m_overlayFrom, m_overlayTo);
    for (int i = 0; i < PICK_COLS * PICK_ROWS; i++) {
        int bx = px + MARGIN + (i % PICK_COLS) * (PICK_BTN_W + MARGIN);
        int by = py + 2 * MARGIN + CHAR_H + (i / PICK_COLS) * (PICK_BTN_H + MARGIN);
        int v = picker_value(i);
        bool on = (v == PICK_OFF) ? (current == -2) : (current == v);
        uint16_t bg = on ? UI_COLOR_GREEN : UI_COLOR_BLACK;
        gfx.fillRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, bg);
        gfx.drawRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_GRAY);
        char label[6];
        if (v == -1) snprintf(label, sizeof(label), "All");
        else if (v == PICK_OFF) snprintf(label, sizeof(label), "Off");
        else snprintf(label, sizeof(label), "%d", v + 1);
        int n = (int)strlen(label);
        gfx.setTextColor(on ? UI_COLOR_BLACK : UI_COLOR_WHITE, bg);
        gfx.setCursor(bx + (PICK_BTN_W - n * CHAR_W) / 2, by + (PICK_BTN_H - CHAR_H) / 2);
        gfx.print(label);
    }
}

bool ScreenMidiDevices::channelPickerHit(int x, int y, int* channel) const
{
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph);
    for (int i = 0; i < PICK_COLS * PICK_ROWS; i++) {
        int bx = px + MARGIN + (i % PICK_COLS) * (PICK_BTN_W + MARGIN);
        int by = py + 2 * MARGIN + CHAR_H + (i / PICK_COLS) * (PICK_BTN_H + MARGIN);
        if (x >= bx && x < bx + PICK_BTN_W && y >= by && y < by + PICK_BTN_H) {
            *channel = picker_value(i);
            return true;
        }
    }
    return false;
}

void ScreenMidiDevices::drawDetail()
{
    auto& gfx = ui_gfx();
    if (m_overlayDev < 0 || m_overlayDev >= m_grid.count) return;
    const ui_mididev_t& d = m_grid.devs[m_overlayDev];

    char lines[12][64];
    int n = 0;
    char name[40];
    displayName(d, name, sizeof(name));
    snprintf(lines[n++], sizeof(lines[0]), "%s", name);
    if (d.key[0]) snprintf(lines[n++], sizeof(lines[0]), "key %s  bit 0x%02X", d.key, d.bit);
    else snprintf(lines[n++], sizeof(lines[0]), "bit 0x%02X (script)", d.bit);
    snprintf(lines[n++], sizeof(lines[0]), "%s",
             (d.flags & UI_MIDIDEV_STANDBY) ? "Standby (starts when patched)"
             : isUp(d) ? "Connected" : "Not connected");
    snprintf(lines[n++], sizeof(lines[0]), "MIDI IN %s  MIDI OUT yes",
             (d.bit && (d.bit & MIDI_route_sources())) ? "yes" : "no");

    if (strcmp(d.key, "usb_host") == 0) {
        usb_midi_host_device_info_t info;
        if (USB_MIDI_HOST_get_device_info(&info)) {
            snprintf(lines[n++], sizeof(lines[0]), "%.28s / %.28s", info.manufacturer, info.product);
            snprintf(lines[n++], sizeof(lines[0]), "EP in 0x%02X out 0x%02X",
                     info.midi_in_ep, info.midi_out_ep);
        }
    }
#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
    if (strcmp(d.key, "usb_dev") == 0) {
        // A snapshot of the TinyUSB stack, so a wedged port can be inspected
        // on the screen while the CDC console itself is unreachable
        usb_midi_device_stats_t st;
        USB_MIDI_DEVICE_get_stats(&st);
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        auto ago = [now](uint32_t t) -> unsigned long {
            return t ? (unsigned long)((now - t) / 1000) : 0;
        };
        snprintf(lines[n++], sizeof(lines[0]), "mnt %d susp %d dtr %d  rst %lu",
                 st.mounted, st.suspended, st.cdc_connected, (unsigned long)st.ev_bus_reset);
        snprintf(lines[n++], sizeof(lines[0]), "MIDI rx %lu tx %lu fail %lu drop %lu",
                 (unsigned long)st.midi_rx_pkts, (unsigned long)st.midi_tx_ok,
                 (unsigned long)st.midi_tx_fail, (unsigned long)st.midi_tx_drop);
        snprintf(lines[n++], sizeof(lines[0]), "MIDI fifo %lu txq %lu cb %lus ago",
                 (unsigned long)st.midi_rx_avail, (unsigned long)st.tx_queue_waiting,
                 ago(st.tusb_task_ms));
        snprintf(lines[n++], sizeof(lines[0]), "CDC rx %lu last %lus ago",
                 (unsigned long)st.cdc_rx_bytes, ago(st.cdc_rx_last_ms));
    }
#endif
    snprintf(lines[n++], sizeof(lines[0]), "Filter overflows %lu",
             (unsigned long)MIDI_route_filter_overflows());

    int lineH = CHAR_H + 4 * TEXT_SIZE;
    int w = UI_SCREEN_WIDTH - 4 * MARGIN;
    int h = n * lineH + 2 * MARGIN;
    int x = 2 * MARGIN;
    int y = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - h) / 2;
    gfx.fillRoundRect(x, y, w, h, 6, UI_COLOR_DARKGRAY);
    gfx.drawRoundRect(x, y, w, h, 6, UI_COLOR_WHITE);
    gfx.setTextSize(TEXT_SIZE);
    for (int i = 0; i < n; i++) {
        gfx.setTextColor(i == 0 ? UI_COLOR_WHITE : UI_COLOR_CYAN, UI_COLOR_DARKGRAY);
        gfx.setCursor(x + MARGIN, y + MARGIN + i * lineH);
        print_fit(lines[i], (w - 2 * MARGIN) / CHAR_W);
    }
}

// --- Touch ---------------------------------------------------------------------------

bool ScreenMidiDevices::hit(int x, int y, int* row, int* col) const
{
    const Grid& g = m_grid;
    if (g.count == 0) return false;
    int gridW = (g.colCount + 1) * g.cellW;
    int gridH = (g.rowCount + 1) * g.cellH;
    bool inCols = x >= g.x0 && x < g.x0 + gridW;
    bool inRows = y >= g.y0 && y < g.y0 + gridH;
    if (inCols && inRows) {
        *row = (y - g.y0) / g.cellH;
        *col = (x - g.x0) / g.cellW;
        return true;
    }
    if (inRows && x >= MARGIN && x < g.x0) {         // Row header
        *row = (y - g.y0) / g.cellH;
        *col = -1;
        return true;
    }
    if (inCols && y >= g.y0 - g.headerH && y < g.y0) {   // Column header
        *row = -1;
        *col = (x - g.x0) / g.cellW;
        return true;
    }
    return false;
}

void ScreenMidiDevices::tap(int x, int y)
{
    if (m_overlay == OVERLAY_CHANNEL) {
        int ch;
        if (channelPickerHit(x, y, &ch)) {
            int current = ui_mididev_patch_channel(m_overlayFrom, m_overlayTo);
            if (ch == PICK_OFF) {
                if (current != -2) ui_mididev_toggle_patch(m_overlayFrom, m_overlayTo, current);
            } else if (current != ch) {
                ui_mididev_toggle_patch(m_overlayFrom, m_overlayTo, ch);
            }
        }
        m_overlay = OVERLAY_NONE;
        m_needsRedraw = true;
        return;
    }
    if (m_overlay == OVERLAY_DETAIL) {
        m_overlay = OVERLAY_NONE;
        m_needsRedraw = true;
        return;
    }

    const Grid& g = m_grid;
    int row, col;
    if (!hit(x, y, &row, &col) || row < 0 || col < 0) return;
    bool progRow = (row == g.rowCount);
    bool progCol = (col == g.colCount);
    if (progRow && progCol) return;

    if (progCol) {
        int dev = g.rows[row];
        ui_mididev_select(false, ui_mididev_selected(false) == dev ? -1 : dev, true);
    } else if (progRow) {
        int dev = g.cols[col];
        ui_mididev_select(true, ui_mididev_selected(true) == dev ? -1 : dev, true);
    } else {
        int from = g.rows[row];
        int to = g.cols[col];
        if (from == to) return;
        int current = ui_mididev_patch_channel(from, to);
        // A patch comes off whatever its channel; a new one is for all
        ui_mididev_toggle_patch(from, to, current == -2 ? -1 : current);
    }
    rebuild();
    m_signature = signature();
    m_needsRedraw = true;
}

void ScreenMidiDevices::longPress(int x, int y)
{
    if (m_overlay != OVERLAY_NONE) return;
    const Grid& g = m_grid;
    int row, col;
    if (!hit(x, y, &row, &col)) return;

    if (row >= 0 && row < g.rowCount && col >= 0 && col < g.colCount) {
        if (g.rows[row] == g.cols[col]) return;
        m_overlay = OVERLAY_CHANNEL;
        m_overlayFrom = g.rows[row];
        m_overlayTo = g.cols[col];
    } else if (col == -1 && row >= 0 && row < g.rowCount) {
        m_overlay = OVERLAY_DETAIL;
        m_overlayDev = g.rows[row];
    } else if (row == -1 && col >= 0 && col < g.colCount) {
        m_overlay = OVERLAY_DETAIL;
        m_overlayDev = g.cols[col];
    } else {
        return;
    }
    m_lastDiagMs = 0;
    m_needsRedraw = true;
}

void ScreenMidiDevices::onTouch(int touchId, int x, int y, bool pressed)
{
    if (pressed) {
        if (m_touchId >= 0) return;   // One finger at a time
        m_touchId = touchId;
        m_touchX = x;
        m_touchY = y;
        m_touchMs = (uint32_t)(esp_timer_get_time() / 1000);
        m_longFired = false;
        return;
    }
    if (touchId != m_touchId) return;
    m_touchId = -1;
    if (!m_longFired) tap(m_touchX, m_touchY);
}

void ScreenMidiDevices::onTouchMove(int touchId, int x, int y)
{
    if (touchId != m_touchId || m_longFired) return;
    int dx = x - m_touchX;
    int dy = y - m_touchY;
    if (dx * dx + dy * dy > MOVE_SLOP * MOVE_SLOP) {
        m_longFired = true;   // A drag: neither a tap nor a hold
    }
}

const char* ScreenMidiDevices::getTitle()
{
    return "MIDI Devices";
}

void ScreenMidiDevices::getTileStatus(char* buf, int len)
{
    int routes = 0;
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        uint8_t dst;
        if (MIDI_route_get(i, nullptr, &dst, nullptr)) {
            for (; dst; dst &= (uint8_t)(dst - 1)) routes++;
        }
    }
    if (routes > 0) snprintf(buf, len, "%d patch%s", routes, routes == 1 ? "" : "es");
    else snprintf(buf, len, "%s", ui_get_midi_indicator() ? "Connected" : "");
}
