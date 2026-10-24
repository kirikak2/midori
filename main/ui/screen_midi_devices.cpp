// MIDI Devices screen: a crossbar of the devices in ui_midi_devices.cpp
// (docs/UI_MIDI_DEVICES.md).
//
//   rows    = devices with a MIDI IN (route sources), then "Prog"
//   columns = devices with a MIDI OUT, then "Prog"
//
// A cell between two devices is a route in picoruby-midi's table -- tap to
// patch / unpatch, hold to pick what it passes (all, or any channels and / or the
// clock), or -- on the picker's Filters tab -- the named filters chained on it
// (docs/MIDI_FILTER_REGISTRY.md) and, holding one, its parameters. The Prog
// column picks the program's IN, the Prog row its OUT. Hold a device's name
// for its details.

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
#include <cstdlib>

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
static constexpr int PICK_BTN_W = 40;
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

// Channel picker: "All", "Clock", 1..16, "Off", "Done" on a 7 x 3 grid (one
// slot spare). All and Off pick and close; Clock and the channels toggle, any
// number of them, until Done (or a tap outside).
static constexpr int PICK_COLS = 7;
static constexpr int PICK_ROWS = 3;
static constexpr int PICK_ALL = -1;
static constexpr int PICK_CLOCK = -2;
static constexpr int PICK_OFF = -3;
static constexpr int PICK_NONE = -4;      // An empty slot
static constexpr int PICK_DONE = -5;
static constexpr int PICK_SLOTS = PICK_COLS * PICK_ROWS;

// The picker's header row: the patch's name, then the Channel / Filters tabs
static constexpr int TAB_H = PICK_BTN_H * 2 / 3;

// Parameter page: rows of label, then -/value/+ (or one button for an enum
// or a bool); Prev / Next when they do not fit, Reset and Done at the bottom
static constexpr int PARAM_ROW_H = PICK_BTN_H * 2 / 3;
static constexpr int PARAM_MINUS = 0;
static constexpr int PARAM_PLUS = 1;
static constexpr int PARAM_CYCLE = 2;
static constexpr int PARAM_RESET = 1000;
static constexpr int PARAM_DONE = 1001;
static constexpr int PARAM_PREV = 1002;
static constexpr int PARAM_NEXT = 1003;
static constexpr uint32_t REPEAT_MS = 100;   // A held -/+

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

// A patch's channels as "1-4,10" (1-based, runs of three or more as ranges),
// then "clk" for the clock
static void selection_label(uint32_t sel, char* buf, int len)
{
    int pos = 0;
    buf[0] = '\0';
    auto put = [&](const char* item) {
        int n = snprintf(buf + pos, len - pos, "%s%s", pos ? "," : "", item);
        if (n > 0) pos += n;
        if (pos > len - 1) pos = len - 1;
    };
    char item[24];   // Room for any two ints, as -Wformat-truncation counts
    for (int ch = 0; ch < 16; ch++) {
        if (!(sel & (1u << ch))) continue;
        int end = ch;
        while (end + 1 < 16 && (sel & (1u << (end + 1)))) end++;
        if (end - ch >= 2) {
            snprintf(item, sizeof(item), "%d-%d", ch + 1, end + 1);
            put(item);
        } else {
            for (int c = ch; c <= end; c++) {
                snprintf(item, sizeof(item), "%d", c + 1);
                put(item);
            }
        }
        ch = end;
    }
    if (sel & MIDI_ROUTE_SEL_CLOCK) put("clk");
}

// The Prog header, "Prog(input)" / "Prog(output)": on one line when it fits the
// box, else "Prog" over the "(input)" / "(output)". Centred in the box when
// center, else from its left edge.
static void print_prog(int x, int y, int w, int h, const char* suffix, bool center)
{
    auto& gfx = ui_gfx();
    char one[24];
    snprintf(one, sizeof(one), "Prog%s", suffix);
    int maxChars = w / CHAR_W;
    const char* lines[2] = { one, nullptr };
    int count = 1;
    if ((int)strlen(one) > maxChars) {
        lines[0] = "Prog";
        lines[1] = suffix;
        count = 2;
    }
    int top = y + (h - count * CHAR_H) / 2;
    for (int i = 0; i < count; i++) {
        int n = (int)strlen(lines[i]);
        if (n > maxChars) n = maxChars;
        gfx.setCursor(center ? x + (w - n * CHAR_W) / 2 : x, top + i * CHAR_H);
        print_fit(lines[i], maxChars);
    }
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
    , m_pickTab(TAB_CHANNEL)
    , m_scratch(nullptr)
    , m_paramStage(-1)
    , m_paramPage(0)
    , m_repeatHit(-1)
    , m_repeatMs(0)
{
}

void ScreenMidiDevices::enter()
{
    m_isActive = true;
    m_overlay = OVERLAY_NONE;
    m_touchId = -1;
    m_repeatHit = -1;
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
        // Every transport (and a standby one) takes MIDI, except those that
        // only send (the tempo's clock)
        if (!(g.devs[i].flags & UI_MIDIDEV_NO_OUT)) g.cols[g.colCount++] = i;
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
    mix(ui_filter_serial());
    mix(ui_mididev_connected_mask());
    mix((uint32_t)ui_mididev_selected(false) + 1);
    mix((uint32_t)ui_mididev_selected(true) + 1);
    mix(MIDI_route_sources());
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        uint8_t src = 0, dst = 0;
        uint32_t sel = 0;
        if (MIDI_route_get_selection(i, &src, &dst, &sel)) {
            mix(src | (dst << 8));
            mix(sel);
        } else {
            mix(0);
        }
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

    // A -/+ held down on the parameter page steps on
    if (m_repeatHit >= 0 && m_touchId >= 0 && now - m_repeatMs >= REPEAT_MS) {
        m_repeatMs = now;
        applyParamHit(m_repeatHit);
        m_needsRedraw = true;
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
    if (m_overlay == OVERLAY_PARAMS) drawParams();
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
        if (c == g.colCount) {
            // The program as a destination: picks its IN
            gfx.setTextColor(PROG_COLOR, UI_COLOR_BLACK);
            print_prog(x + 1, g.y0 - g.headerH, g.cellW - 2, g.headerH, "(input)", true);
            continue;
        }
        const ui_mididev_t& d = g.devs[g.cols[c]];
        displayName(d, name, sizeof(name));
        const char* label = name;
        uint16_t color = isUp(d) ? UI_COLOR_WHITE : UI_COLOR_GRAY;
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
            // The program as a source: picks its OUT
            gfx.setTextColor(PROG_COLOR, UI_COLOR_BLACK);
            print_prog(MARGIN + 3 * DOT_R, y, nameChars * CHAR_W, g.cellH, "(output)", false);
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

    uint32_t sel = ui_mididev_patch_selection(from, to);
    if (sel == 0) {
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
    // The channels (1..16) and "clk" for the clock, unless it passes all
    if (!(sel & MIDI_ROUTE_SEL_ALL)) {
        char buf[64];
        selection_label(sel, buf, sizeof(buf));
        int maxChars = (g.cellW - 4) / 6;
        int n = (int)strlen(buf);
        if (maxChars >= 2 && n > maxChars) {   // Cut short: mark that there is more
            buf[maxChars - 1] = '+';
            buf[maxChars] = '\0';
        }
        gfx.setTextSize(1);
        gfx.setTextColor(UI_COLOR_WHITE);
        gfx.setCursor(x + 2, y + g.cellH - 9);
        print_fit(buf, maxChars);
        gfx.setTextSize(TEXT_SIZE);
    }
    // Named filters chained on it ("f2": two of them), or a Ruby filter
    // (MIDI.filter) of a script's
    int chained = (a.bit && b.bit) ? ui_filter_chain_len(a.bit, b.bit)
                                   : ui_filter_saved_len(a.key, b.key);
    if (chained > 0 || (a.bit && b.bit && (MIDI_route_filtered(a.bit) & b.bit))) {
        char f[16];
        if (chained > 0) snprintf(f, sizeof(f), "f%d", chained);
        else snprintf(f, sizeof(f), "f");
        gfx.setTextSize(1);
        gfx.setTextColor(UI_COLOR_YELLOW);
        gfx.setCursor(x + g.cellW - 2 - 6 * (int)strlen(f), y + 2);
        gfx.print(f);
        gfx.setTextSize(TEXT_SIZE);
    }
}

// Channel picker geometry (shared by drawing and hit testing)
static void picker_rect(int* px, int* py, int* pw, int* ph)
{
    *pw = PICK_COLS * PICK_BTN_W + (PICK_COLS + 1) * MARGIN;
    *ph = PICK_ROWS * PICK_BTN_H + (PICK_ROWS + 1) * MARGIN + TAB_H + MARGIN;
    *px = (UI_SCREEN_WIDTH - *pw) / 2;
    *py = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - *ph) / 2;
}

static int picker_value(int i)
{
    if (i == 0) return PICK_ALL;
    if (i == 1) return PICK_CLOCK;                // Clock (sync messages only)
    if (i <= 17) return i - 2;                    // 1..16
    if (i == 18) return PICK_OFF;
    if (i == PICK_COLS * PICK_ROWS - 1) return PICK_DONE;
    return PICK_NONE;
}

// Whether picker value v is part of selection sel (0: no patch)
static bool picker_on(int v, uint32_t sel)
{
    switch (v) {
    case PICK_ALL:   return (sel & MIDI_ROUTE_SEL_ALL) != 0;
    case PICK_OFF:   return sel == 0;
    case PICK_DONE:  return false;
    case PICK_CLOCK: return !(sel & MIDI_ROUTE_SEL_ALL) && (sel & MIDI_ROUTE_SEL_CLOCK);
    default:         return !(sel & MIDI_ROUTE_SEL_ALL) && (sel & (1u << v));
    }
}

// The tabs' boxes, right-aligned in the header row
static void tab_rect(int px, int py, int pw, int tab, int* x, int* y, int* w, int* h)
{
    *w = PICK_BTN_W + PICK_BTN_W / 2;
    *h = TAB_H;
    *x = px + pw - MARGIN - (2 - tab) * (*w + MARGIN) + MARGIN;
    *y = py + MARGIN;
}

static void slot_rect(int px, int py, int i, int* x, int* y)
{
    *x = px + MARGIN + (i % PICK_COLS) * (PICK_BTN_W + MARGIN);
    *y = py + 2 * MARGIN + TAB_H + (i / PICK_COLS) * (PICK_BTN_H + MARGIN);
}

void ScreenMidiDevices::drawPickerHeader(int px, int py, int pw, bool tabs)
{
    auto& gfx = ui_gfx();
    char from[24], to[24], title[64];
    displayName(m_grid.devs[m_overlayFrom], from, sizeof(from));
    displayName(m_grid.devs[m_overlayTo], to, sizeof(to));
    if (m_overlay == OVERLAY_PARAMS && m_scratch) {
        snprintf(title, sizeof(title), "%s  %s > %s", m_scratch->def.label, from, to);
    } else {
        snprintf(title, sizeof(title), "%s > %s", from, to);
    }
    int titleW = pw - 2 * MARGIN;
    if (tabs) {
        int tx, ty, tw, th;
        tab_rect(px, py, pw, 0, &tx, &ty, &tw, &th);
        titleW = tx - px - 2 * MARGIN;
        static const char* const NAMES[2] = { "Ch", "Filters" };
        for (int t = 0; t < 2; t++) {
            tab_rect(px, py, pw, t, &tx, &ty, &tw, &th);
            bool on = (int)m_pickTab == t;
            uint16_t bg = on ? (uint16_t)UI_COLOR_WHITE : (uint16_t)UI_COLOR_BLACK;
            gfx.fillRoundRect(tx, ty, tw, th, 4, bg);
            gfx.drawRoundRect(tx, ty, tw, th, 4, UI_COLOR_GRAY);
            int n = (int)strlen(NAMES[t]);
            int fit = (tw - 4) / CHAR_W;
            if (n > fit) n = fit;
            gfx.setTextColor(on ? UI_COLOR_BLACK : UI_COLOR_WHITE, bg);
            gfx.setCursor(tx + (tw - n * CHAR_W) / 2, ty + (th - CHAR_H) / 2);
            print_fit(NAMES[t], fit);
        }
    }
    gfx.setTextSize(TEXT_SIZE);
    gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    gfx.setCursor(px + MARGIN, py + MARGIN + (TAB_H - CHAR_H) / 2);
    print_fit(title, titleW / CHAR_W);
}

void ScreenMidiDevices::drawChannelPicker()
{
    auto& gfx = ui_gfx();
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph);
    gfx.fillRoundRect(px, py, pw, ph, 6, UI_COLOR_DARKGRAY);
    gfx.drawRoundRect(px, py, pw, ph, 6, UI_COLOR_WHITE);
    gfx.setTextSize(TEXT_SIZE);
    drawPickerHeader(px, py, pw, true);
    if (m_pickTab == TAB_FILTERS) {
        drawFilterButtons(px, py);
        return;
    }

    uint32_t current = ui_mididev_patch_selection(m_overlayFrom, m_overlayTo);
    for (int i = 0; i < PICK_COLS * PICK_ROWS; i++) {
        int bx, by;
        slot_rect(px, py, i, &bx, &by);
        int v = picker_value(i);
        if (v == PICK_NONE) continue;
        bool on = picker_on(v, current);
        uint16_t bg = on ? UI_COLOR_GREEN : UI_COLOR_BLACK;
        gfx.fillRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, bg);
        gfx.drawRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4,
                          v == PICK_DONE ? (uint16_t)UI_COLOR_WHITE : (uint16_t)UI_COLOR_GRAY);
        char label[6];
        if (v == PICK_ALL) snprintf(label, sizeof(label), "All");
        else if (v == PICK_CLOCK) snprintf(label, sizeof(label), "Clock");
        else if (v == PICK_OFF) snprintf(label, sizeof(label), "Off");
        else if (v == PICK_DONE) snprintf(label, sizeof(label), "Done");
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
        int bx, by;
        slot_rect(px, py, i, &bx, &by);
        if (x >= bx && x < bx + PICK_BTN_W && y >= by && y < by + PICK_BTN_H) {
            if (picker_value(i) == PICK_NONE) return false;
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
    snprintf(lines[n++], sizeof(lines[0]), "MIDI IN %s  MIDI OUT %s",
             (d.bit && (d.bit & MIDI_route_sources())) ? "yes" : "no",
             (d.flags & UI_MIDIDEV_NO_OUT) ? "no" : "yes");

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
        // Per bus reset: a host that gives up early leaves only a few SETUPs,
        // one that finishes enumerating (then drops us) leaves dozens
        snprintf(lines[n++], sizeof(lines[0]), "setup %lu xfer %lu susp %lu res %lu",
                 (unsigned long)st.ev_setup, (unsigned long)st.ev_xfer,
                 (unsigned long)st.ev_suspend, (unsigned long)st.ev_resume);
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
    if (m_overlay == OVERLAY_PARAMS) {
        tapParams(x, y);
        return;
    }
    if (m_overlay == OVERLAY_CHANNEL && (tabHit(x, y) >= 0 || m_pickTab == TAB_FILTERS)) {
        tapPicker(x, y);
        return;
    }
    if (m_overlay == OVERLAY_CHANNEL) {
        int v;
        if (!channelPickerHit(x, y, &v)) {
            // Between the buttons: nothing; outside the picker: close it
            int px, py, pw, ph;
            picker_rect(&px, &py, &pw, &ph);
            if (x >= px && x < px + pw && y >= py && y < py + ph) return;
            m_overlay = OVERLAY_NONE;
            m_needsRedraw = true;
            return;
        }
        uint32_t current = ui_mididev_patch_selection(m_overlayFrom, m_overlayTo);
        bool close = true;
        if (v == PICK_OFF) {
            if (current != 0) ui_mididev_set_patch(m_overlayFrom, m_overlayTo, 0);
        } else if (v == PICK_ALL) {
            if (current != MIDI_ROUTE_SEL_ALL) ui_mididev_set_patch(m_overlayFrom, m_overlayTo, MIDI_ROUTE_SEL_ALL);
        } else if (v != PICK_DONE) {
            // Clock / a channel: toggled; All gives way to it. Toggling the
            // last one off removes the patch.
            uint32_t bit = (v == PICK_CLOCK) ? MIDI_ROUTE_SEL_CLOCK : (1u << v);
            uint32_t base = (current & MIDI_ROUTE_SEL_ALL) ? 0 : current;
            ui_mididev_set_patch(m_overlayFrom, m_overlayTo, base ^ bit);
            close = false;
        }
        if (close) m_overlay = OVERLAY_NONE;
        rebuild();
        m_signature = signature();
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
        ui_mididev_toggle_patch(from, to);
    }
    rebuild();
    m_signature = signature();
    m_needsRedraw = true;
}

void ScreenMidiDevices::longPress(int x, int y)
{
    // Filters tab: hold a chained filter for its parameters
    if (m_overlay == OVERLAY_CHANNEL && m_pickTab == TAB_FILTERS) {
        int slot = filterSlotHit(x, y);
        if (slot < 0 || !scratch() || slot >= m_scratch->itemCount) return;
        const FilterItem& it = m_scratch->items[slot];
        int pos = chainPosition(it.name);
        if (pos < 0 || !it.registered || !loadDef(it.name)) return;
        m_overlay = OVERLAY_PARAMS;
        m_paramStage = pos;
        m_paramPage = 0;
        m_needsRedraw = true;
        return;
    }
    // Parameter page: a held -/+ repeats
    if (m_overlay == OVERLAY_PARAMS) {
        int code = paramHit(x, y);
        if (code >= 0 && code < PARAM_RESET && (code & 3) != PARAM_CYCLE) {
            m_repeatHit = code;
            m_repeatMs = (uint32_t)(esp_timer_get_time() / 1000);
            applyParamHit(code);
            m_needsRedraw = true;
        }
        return;
    }
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
    m_repeatHit = -1;
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

// --- Filters tab and parameter page -----------------------------------------------------

bool ScreenMidiDevices::scratch()
{
    if (!m_scratch) m_scratch = (Scratch*)calloc(1, sizeof(Scratch));
    return m_scratch != nullptr;
}

bool ScreenMidiDevices::loadChain()
{
    if (!scratch()) return false;
    const ui_mididev_t& a = m_grid.devs[m_overlayFrom];
    const ui_mididev_t& b = m_grid.devs[m_overlayTo];
    if (a.bit && b.bit) ui_filter_chain_get(a.bit, b.bit, &m_scratch->chain);
    else ui_filter_saved_get(a.key, b.key, &m_scratch->chain);   // An end on standby
    return true;
}

// The scratch chain onto the patch: patched first if it is not, saved when
// both ends are built-ins
void ScreenMidiDevices::saveChain()
{
    const ui_mididev_t& a = m_grid.devs[m_overlayFrom];
    const ui_mididev_t& b = m_grid.devs[m_overlayTo];
    const ui_filter_chain_t& c = m_scratch->chain;
    if (c.len > 0 && ui_mididev_patch_selection(m_overlayFrom, m_overlayTo) == 0) {
        ui_mididev_set_patch(m_overlayFrom, m_overlayTo, MIDI_ROUTE_SEL_ALL);
    }
    if (a.bit && b.bit) {
        ui_filter_chain_set(a.bit, b.bit, &c, UI_FILTER_SAVE_NOW);
    } else if ((a.flags & UI_MIDIDEV_BUILTIN) && (b.flags & UI_MIDIDEV_BUILTIN)) {
        // Put on the patch when the standby end starts (ui_filter_restore)
        ui_filter_saved_set(a.key, b.key, &c);
    }
}

void ScreenMidiDevices::loadItems()
{
    Scratch& sc = *m_scratch;
    int n = 0;
    int count = ui_filter_count();
    for (int i = 0; i < count && n < FILTER_ITEMS; i++) {
        if (!ui_filter_get(i, &sc.def)) continue;
        FilterItem& it = sc.items[n++];
        snprintf(it.name, sizeof(it.name), "%s", sc.def.name);
        snprintf(it.label, sizeof(it.label), "%s", sc.def.label);
        it.registered = true;
    }
    // Names in the chain that nothing registers (a file taken off /sd/filters)
    for (int i = 0; i < sc.chain.len && n < FILTER_ITEMS; i++) {
        if (ui_filter_find(sc.chain.stages[i].name) >= 0) continue;
        FilterItem& it = sc.items[n++];
        snprintf(it.name, sizeof(it.name), "%s", sc.chain.stages[i].name);
        snprintf(it.label, sizeof(it.label), "%s", sc.chain.stages[i].name);
        it.registered = false;
    }
    sc.itemCount = n;
}

int ScreenMidiDevices::chainPosition(const char* name) const
{
    const ui_filter_chain_t& c = m_scratch->chain;
    for (int i = 0; i < c.len; i++) {
        if (strcmp(c.stages[i].name, name) == 0) return i;
    }
    return -1;
}

bool ScreenMidiDevices::loadDef(const char* name)
{
    int i = ui_filter_find(name);
    return i >= 0 && ui_filter_get(i, &m_scratch->def);
}

int ScreenMidiDevices::tabHit(int x, int y) const
{
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph);
    for (int t = 0; t < 2; t++) {
        int tx, ty, tw, th;
        tab_rect(px, py, pw, t, &tx, &ty, &tw, &th);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) return t;
    }
    return -1;
}

int ScreenMidiDevices::filterSlotHit(int x, int y) const
{
    int px, py, pw, ph;
    picker_rect(&px, &py, &pw, &ph);
    for (int i = 0; i < PICK_SLOTS; i++) {
        int bx, by;
        slot_rect(px, py, i, &bx, &by);
        if (x >= bx && x < bx + PICK_BTN_W && y >= by && y < by + PICK_BTN_H) return i;
    }
    return -1;
}

void ScreenMidiDevices::drawFilterButtons(int px, int py)
{
    auto& gfx = ui_gfx();
    if (!loadChain()) return;
    loadItems();
    const Scratch& sc = *m_scratch;
    int fit = (PICK_BTN_W - 4) / CHAR_W;
    for (int i = 0; i < PICK_SLOTS; i++) {
        int bx, by;
        slot_rect(px, py, i, &bx, &by);
        if (i == PICK_SLOTS - 1) {
            gfx.fillRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_BLACK);
            gfx.drawRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_WHITE);
            gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
            gfx.setCursor(bx + (PICK_BTN_W - 4 * CHAR_W) / 2, by + (PICK_BTN_H - CHAR_H) / 2);
            gfx.print("Done");
            continue;
        }
        if (i >= sc.itemCount) continue;
        const FilterItem& it = sc.items[i];
        int pos = chainPosition(it.name);
        bool on = pos >= 0;
        uint16_t bg = on ? (it.registered ? (uint16_t)UI_COLOR_GREEN : (uint16_t)UI_COLOR_GRAY)
                         : (uint16_t)UI_COLOR_BLACK;
        uint16_t fg = on ? (uint16_t)UI_COLOR_BLACK
                         : (it.registered ? (uint16_t)UI_COLOR_WHITE : (uint16_t)UI_COLOR_GRAY);
        gfx.fillRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, bg);
        gfx.drawRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_GRAY);
        int n = (int)strlen(it.label);
        if (n > fit) n = fit;
        gfx.setTextColor(fg, bg);
        gfx.setCursor(bx + (PICK_BTN_W - n * CHAR_W) / 2, by + (PICK_BTN_H - CHAR_H) / 2);
        print_fit(it.label, fit);
        if (on) {
            // Its place in the chain, in the corner
            char num[12];
            snprintf(num, sizeof(num), "%d", pos + 1);
            gfx.setTextSize(1);
            gfx.setTextColor(UI_COLOR_BLACK, bg);
            gfx.setCursor(bx + PICK_BTN_W - 8, by + 2);
            gfx.print(num);
            gfx.setTextSize(TEXT_SIZE);
        }
    }
    if (sc.itemCount == 0) {
        int bx, by;
        slot_rect(px, py, 0, &bx, &by);
        gfx.setTextColor(UI_COLOR_GRAY, UI_COLOR_DARKGRAY);
        gfx.setCursor(bx, by + (PICK_BTN_H - CHAR_H) / 2);
        gfx.print("No filters (put them in /sd/filters)");
    }
}

void ScreenMidiDevices::tapPicker(int x, int y)
{
    int t = tabHit(x, y);
    if (t >= 0) {
        m_pickTab = (PickTab)t;
        m_needsRedraw = true;
        return;
    }
    int slot = filterSlotHit(x, y);
    if (slot < 0) {
        // Between the buttons: nothing; outside the picker: close it
        int px, py, pw, ph;
        picker_rect(&px, &py, &pw, &ph);
        if (x >= px && x < px + pw && y >= py && y < py + ph) return;
        m_overlay = OVERLAY_NONE;
        m_needsRedraw = true;
        return;
    }
    if (slot == PICK_SLOTS - 1) {
        m_overlay = OVERLAY_NONE;
        m_needsRedraw = true;
        return;
    }
    if (!loadChain()) return;
    loadItems();
    if (slot >= m_scratch->itemCount) return;
    FilterItem it = m_scratch->items[slot];
    ui_filter_chain_t& c = m_scratch->chain;
    int pos = chainPosition(it.name);
    if (pos >= 0) {
        // Off the chain; the ones after it move up
        for (int i = pos; i < c.len - 1; i++) c.stages[i] = c.stages[i + 1];
        c.len--;
    } else {
        // On at the end, with the filter's defaults
        if (c.len >= UI_FILTER_CHAIN_MAX || !it.registered || !loadDef(it.name)) return;
        ui_filter_stage_t& st = c.stages[c.len];
        memset(&st, 0, sizeof(st));
        snprintf(st.name, sizeof(st.name), "%s", it.name);
        const ui_filter_def_t& d = m_scratch->def;
        for (int i = 0; i < d.nparams; i++) {
            snprintf(st.pnames[i], sizeof(st.pnames[i]), "%s", d.params[i].name);
            st.values[i] = d.params[i].def;
        }
        st.nvalues = d.nparams;
        c.len++;
    }
    saveChain();
    rebuild();
    m_signature = signature();
    m_needsRedraw = true;
}

// The parameter page fills the content area, as wide as the picker
static void params_rect(int* px, int* py, int* pw, int* ph)
{
    *pw = PICK_COLS * PICK_BTN_W + (PICK_COLS + 1) * MARGIN;
    *ph = UI_CONTENT_HEIGHT - 2 * MARGIN;
    *px = (UI_SCREEN_WIDTH - *pw) / 2;
    *py = UI_CONTENT_Y + MARGIN;
}

int ScreenMidiDevices::paramRows() const
{
    int px, py, pw, ph;
    params_rect(&px, &py, &pw, &ph);
    int top = py + 2 * MARGIN + TAB_H;
    int bottom = py + ph - 2 * MARGIN - PICK_BTN_H;
    int rows = (bottom - top) / (PARAM_ROW_H + MARGIN);
    return rows < 1 ? 1 : rows;
}

static int16_t stage_value(const ui_filter_stage_t& st, const ui_filter_param_t& p)
{
    for (int i = 0; i < st.nvalues; i++) {
        if (strcmp(st.pnames[i], p.name) == 0) return st.values[i];
    }
    return p.def;
}

static void param_text(const ui_filter_param_t& p, int v, char* buf, int len)
{
    static const char* const NOTES[12] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    switch (p.kind) {
    case UI_FILTER_ENUM:
        snprintf(buf, len, "%s", (v >= 0 && v < p.nchoices) ? p.choices[v] : "?");
        break;
    case UI_FILTER_BOOL:
        snprintf(buf, len, "%s", v ? "on" : "off");
        break;
    case UI_FILTER_NOTE:
        snprintf(buf, len, "%s%d", NOTES[((v % 12) + 12) % 12], v / 12 - 1);
        break;
    case UI_FILTER_CHANNEL:
        if (v < 0) snprintf(buf, len, "-");
        else snprintf(buf, len, "%d", v + 1);
        break;
    default:
        snprintf(buf, len, "%d", v);
        break;
    }
}

// A row's boxes: the -, the value and the + (an enum / bool: one button, as wide
// as all three)
static void param_row(int px, int py, int pw, int r, int* lx, int* cx, int* cw, int* ry)
{
    *ry = py + 2 * MARGIN + TAB_H + r * (PARAM_ROW_H + MARGIN);
    *lx = px + MARGIN;
    int labelW = (pw - 2 * MARGIN) * 2 / 5;
    *cx = *lx + labelW + MARGIN;
    *cw = px + pw - MARGIN - *cx;
}

static constexpr int PARAM_BTN_W = PARAM_ROW_H * 3 / 2;

// Bottom row: Prev / Next on the left, Reset / Done on the right
static void param_button(int px, int py, int pw, int ph, int which, int* bx, int* by)
{
    *by = py + ph - MARGIN - PICK_BTN_H;
    switch (which) {
    case PARAM_PREV:  *bx = px + MARGIN; break;
    case PARAM_NEXT:  *bx = px + 2 * MARGIN + PICK_BTN_W; break;
    case PARAM_RESET: *bx = px + pw - 2 * (MARGIN + PICK_BTN_W); break;
    default:          *bx = px + pw - MARGIN - PICK_BTN_W; break;   // Done
    }
}

void ScreenMidiDevices::drawParams()
{
    auto& gfx = ui_gfx();
    if (!loadChain()) return;
    if (m_paramStage < 0 || m_paramStage >= m_scratch->chain.len ||
        strcmp(m_scratch->chain.stages[m_paramStage].name, m_scratch->def.name) != 0) {
        // The chain changed under the page (a script, Ruby): back to the tab
        m_overlay = OVERLAY_CHANNEL;
        drawChannelPicker();
        return;
    }
    const ui_filter_stage_t& st = m_scratch->chain.stages[m_paramStage];
    const ui_filter_def_t& d = m_scratch->def;

    int px, py, pw, ph;
    params_rect(&px, &py, &pw, &ph);
    gfx.fillRoundRect(px, py, pw, ph, 6, UI_COLOR_DARKGRAY);
    gfx.drawRoundRect(px, py, pw, ph, 6, UI_COLOR_WHITE);
    gfx.setTextSize(TEXT_SIZE);
    drawPickerHeader(px, py, pw, false);

    int rows = paramRows();
    int pages = (d.nparams + rows - 1) / rows;
    if (pages < 1) pages = 1;
    if (m_paramPage >= pages) m_paramPage = pages - 1;

    for (int r = 0; r < rows; r++) {
        int idx = m_paramPage * rows + r;
        if (idx >= d.nparams) break;
        const ui_filter_param_t& p = d.params[idx];
        int lx, cx, cw, ry;
        param_row(px, py, pw, r, &lx, &cx, &cw, &ry);
        int ty = ry + (PARAM_ROW_H - CHAR_H) / 2;
        gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
        gfx.setCursor(lx, ty);
        print_fit(p.label, (cx - lx - MARGIN) / CHAR_W);

        char text[16];
        param_text(p, stage_value(st, p), text, sizeof(text));
        int n = (int)strlen(text);
        if (p.kind == UI_FILTER_ENUM || p.kind == UI_FILTER_BOOL) {
            gfx.fillRoundRect(cx, ry, cw, PARAM_ROW_H, 4, UI_COLOR_BLACK);
            gfx.drawRoundRect(cx, ry, cw, PARAM_ROW_H, 4, UI_COLOR_GRAY);
            gfx.setTextColor(UI_COLOR_CYAN, UI_COLOR_BLACK);
            gfx.setCursor(cx + (cw - n * CHAR_W) / 2, ty);
            print_fit(text, (cw - 4) / CHAR_W);
            continue;
        }
        static const char* const SIGNS[2] = { "-", "+" };
        for (int k = 0; k < 2; k++) {
            int bx = k == 0 ? cx : cx + cw - PARAM_BTN_W;
            gfx.fillRoundRect(bx, ry, PARAM_BTN_W, PARAM_ROW_H, 4, UI_COLOR_BLACK);
            gfx.drawRoundRect(bx, ry, PARAM_BTN_W, PARAM_ROW_H, 4, UI_COLOR_GRAY);
            gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
            gfx.setCursor(bx + (PARAM_BTN_W - CHAR_W) / 2, ty);
            gfx.print(SIGNS[k]);
        }
        gfx.setTextColor(UI_COLOR_CYAN, UI_COLOR_DARKGRAY);
        gfx.setCursor(cx + (cw - n * CHAR_W) / 2, ty);
        print_fit(text, (cw - 2 * PARAM_BTN_W) / CHAR_W);
    }

    struct { int code; const char* label; } buttons[4] = {
        { PARAM_PREV, "<" }, { PARAM_NEXT, ">" }, { PARAM_RESET, "Reset" }, { PARAM_DONE, "Done" },
    };
    for (const auto& b : buttons) {
        if ((b.code == PARAM_PREV || b.code == PARAM_NEXT) && pages <= 1) continue;
        int bx, by;
        param_button(px, py, pw, ph, b.code, &bx, &by);
        gfx.fillRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_BLACK);
        gfx.drawRoundRect(bx, by, PICK_BTN_W, PICK_BTN_H, 4, UI_COLOR_WHITE);
        int n = (int)strlen(b.label);
        gfx.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
        gfx.setCursor(bx + (PICK_BTN_W - n * CHAR_W) / 2, by + (PICK_BTN_H - CHAR_H) / 2);
        print_fit(b.label, (PICK_BTN_W - 2) / CHAR_W);
    }
}

int ScreenMidiDevices::paramHit(int x, int y) const
{
    if (!m_scratch) return -1;
    const ui_filter_def_t& d = m_scratch->def;
    int px, py, pw, ph;
    params_rect(&px, &py, &pw, &ph);
    int rows = paramRows();
    int pages = (d.nparams + rows - 1) / rows;
    static const int CODES[4] = { PARAM_PREV, PARAM_NEXT, PARAM_RESET, PARAM_DONE };
    for (int code : CODES) {
        if ((code == PARAM_PREV || code == PARAM_NEXT) && pages <= 1) continue;
        int bx, by;
        param_button(px, py, pw, ph, code, &bx, &by);
        if (x >= bx && x < bx + PICK_BTN_W && y >= by && y < by + PICK_BTN_H) return code;
    }
    for (int r = 0; r < rows; r++) {
        int idx = m_paramPage * rows + r;
        if (idx >= d.nparams) break;
        int lx, cx, cw, ry;
        param_row(px, py, pw, r, &lx, &cx, &cw, &ry);
        if (y < ry || y >= ry + PARAM_ROW_H || x < cx || x >= cx + cw) continue;
        const ui_filter_param_t& p = d.params[idx];
        if (p.kind == UI_FILTER_ENUM || p.kind == UI_FILTER_BOOL) return (idx << 2) | PARAM_CYCLE;
        if (x < cx + PARAM_BTN_W) return (idx << 2) | PARAM_MINUS;
        if (x >= cx + cw - PARAM_BTN_W) return (idx << 2) | PARAM_PLUS;
        return -1;
    }
    return -1;
}

void ScreenMidiDevices::applyParamHit(int code)
{
    if (!loadChain() || m_paramStage < 0 || m_paramStage >= m_scratch->chain.len) return;
    ui_filter_stage_t& st = m_scratch->chain.stages[m_paramStage];
    const ui_filter_def_t& d = m_scratch->def;
    const ui_mididev_t& a = m_grid.devs[m_overlayFrom];
    const ui_mididev_t& b = m_grid.devs[m_overlayTo];

    switch (code) {
    case PARAM_PREV:
        if (m_paramPage > 0) m_paramPage--;
        return;
    case PARAM_NEXT:
        m_paramPage++;
        return;
    case PARAM_DONE:
        m_overlay = OVERLAY_CHANNEL;
        return;
    case PARAM_RESET:
        // Back to the values the filter was registered with
        for (int i = 0; i < d.nparams; i++) {
            snprintf(st.pnames[i], sizeof(st.pnames[i]), "%s", d.params[i].name);
            st.values[i] = d.params[i].def;
        }
        st.nvalues = d.nparams;
        saveChain();
        return;
    default:
        break;
    }

    int idx = code >> 2;
    if (idx < 0 || idx >= d.nparams) return;
    const ui_filter_param_t& p = d.params[idx];
    int v = stage_value(st, p);
    switch (code & 3) {
    case PARAM_MINUS: v -= p.step; break;
    case PARAM_PLUS:  v += p.step; break;
    default:
        if (p.kind == UI_FILTER_BOOL) v = v ? 0 : 1;
        else v = (v >= p.max) ? p.min : v + 1;   // An enum steps round
        break;
    }
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    if (a.bit && b.bit) {
        ui_filter_set_value(a.bit, b.bit, m_paramStage, p.name, (int16_t)v);
    } else {
        int k = 0;
        while (k < st.nvalues && strcmp(st.pnames[k], p.name) != 0) k++;
        if (k == st.nvalues && k < UI_FILTER_PARAM_MAX) {
            snprintf(st.pnames[k], sizeof(st.pnames[k]), "%s", p.name);
            st.nvalues++;
        }
        if (k < st.nvalues) st.values[k] = (int16_t)v;
        saveChain();
    }
}

void ScreenMidiDevices::tapParams(int x, int y)
{
    int code = paramHit(x, y);
    if (code < 0) {
        // Outside the page: back to the Filters tab
        int px, py, pw, ph;
        params_rect(&px, &py, &pw, &ph);
        if (x >= px && x < px + pw && y >= py && y < py + ph) return;
        m_overlay = OVERLAY_CHANNEL;
        m_needsRedraw = true;
        return;
    }
    applyParamHit(code);
    m_needsRedraw = true;
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
