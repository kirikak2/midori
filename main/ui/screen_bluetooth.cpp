// Bluetooth screen: BLE-MIDI device / host (screen_bluetooth.h, docs/BLE_MIDI.md)

#include "sdkconfig.h"
#include "screen_bluetooth.h"
#include "ui_strip.h"
#include "ble_midi_device.h"
#include "ble_midi_host.h"
#include "ble_midi_settings.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <cstdio>

static ScreenBluetooth s_screenBluetooth;

ScreenBluetooth& getScreenBluetooth()
{
    return s_screenBluetooth;
}

#if defined(UI_LAYOUT_LARGE)
static constexpr int TS = 2;
#else
static constexpr int TS = 1;
#endif
static constexpr int MARGIN = 6 * TS;
static constexpr int BTN_H = 22 * TS;
static constexpr int STATUS_H = 14 * TS;
static constexpr int ROW_H = 22 * TS;
static constexpr int FORGET_W = 22 * TS;
static constexpr int GLYPH_W = 6 * TS;
static constexpr int GLYPH_H = 8 * TS;

static constexpr int64_t POLL_US = 300000;
static constexpr int SCAN_MS = 8000;
static constexpr int MAX_ROWS = BLE_MIDI_HOST_MAX_KNOWN + BLE_MIDI_HOST_MAX_RESULTS;
static constexpr int8_t NO_RSSI = 127;

struct ScreenBluetooth::Row {
    uint8_t addr[6];
    uint8_t type;
    char    name[24];
    int8_t  rssi;
    bool    known;
    int     slot;       // -1: in no slot
    int     status;     // ble_midi_host_slot_status_t of the slot
};

// Scratch for the C API's copies (heap, with the rows)
static ble_midi_peer_t* s_peers = nullptr;

ScreenBluetooth::ScreenBluetooth()
    : m_isActive(false)
    , m_needsRedraw(false)
    , m_lastPollUs(0)
    , m_lastHash(0)
    , m_rows(nullptr)
    , m_rowCount(0)
    , m_scroll(0)
    , m_devOn(false), m_hostOn(false), m_scanning(false)
    , m_devState(0)
    , m_touchId(-1)
    , m_touchX(0), m_touchY(0), m_dragAccum(0)
    , m_dragged(false)
{
    m_devPeer[0] = '\0';
}

bool ScreenBluetooth::ensureRows()
{
    if (m_rows && s_peers) return true;
    if (!m_rows) m_rows = (Row*)heap_caps_calloc(MAX_ROWS, sizeof(Row), MALLOC_CAP_8BIT);
    if (!s_peers) s_peers = (ble_midi_peer_t*)heap_caps_calloc(MAX_ROWS, sizeof(ble_midi_peer_t), MALLOC_CAP_8BIT);
    return m_rows && s_peers;
}

// --- Model ----------------------------------------------------------------

static int find_row(ScreenBluetooth::Row* rows, int n, const uint8_t addr[6])
{
    for (int i = 0; i < n; i++) {
        if (memcmp(rows[i].addr, addr, 6) == 0) return i;
    }
    return -1;
}

void ScreenBluetooth::snapshot()
{
    m_devOn = BLE_MIDI_DEVICE_is_enabled();
    m_hostOn = BLE_MIDI_HOST_is_enabled();
    m_scanning = BLE_MIDI_HOST_scanning();
    ble_midi_device_stats_t ds;
    BLE_MIDI_DEVICE_stats(&ds);
    m_devState = (int)ds.state;
    if (ds.state == BLE_MIDI_DEVICE_LINKED || ds.state == BLE_MIDI_DEVICE_CONNECTED) {
        BLE_MIDI_HOST_format_addr(ds.peer, m_devPeer);
    } else {
        m_devPeer[0] = '\0';
    }

    if (!ensureRows()) {
        m_rowCount = 0;
        return;
    }
    int n = 0;
    auto add = [&](const ble_midi_peer_t& p) -> Row* {
        int i = find_row(m_rows, n, p.addr);
        if (i < 0) {
            if (n >= MAX_ROWS) return nullptr;
            i = n++;
            Row& r = m_rows[i];
            memset(&r, 0, sizeof(r));
            memcpy(r.addr, p.addr, 6);
            r.type = p.addr_type;
            r.rssi = NO_RSSI;
            r.slot = -1;
        }
        Row& r = m_rows[i];
        if (p.name[0]) {
            strncpy(r.name, p.name, sizeof(r.name) - 1);
            r.name[sizeof(r.name) - 1] = '\0';
        }
        if (p.known) r.known = true;
        return &r;
    };

    // The slots first (connected on top), then remembered, then found
    for (int s = 0; s < BLE_MIDI_HOST_MAX_DEVICES; s++) {
        int st = BLE_MIDI_HOST_dev_status(s);
        if (st == BLE_MIDI_HOST_SLOT_FREE) continue;
        if (BLE_MIDI_HOST_dev_info(s, &s_peers[0]) != 0) continue;
        Row* r = add(s_peers[0]);
        if (r) { r->slot = s; r->status = st; }
    }
    int k = BLE_MIDI_HOST_known(s_peers, BLE_MIDI_HOST_MAX_KNOWN);
    for (int i = k - 1; i >= 0; i--) add(s_peers[i]);     // most recent first
    int f = BLE_MIDI_HOST_scan_results(s_peers, BLE_MIDI_HOST_MAX_RESULTS);
    for (int i = 0; i < f; i++) {
        Row* r = add(s_peers[i]);
        if (r) r->rssi = s_peers[i].rssi;
    }
    m_rowCount = n;
    int maxScroll = m_rowCount - visibleRows();
    if (maxScroll < 0) maxScroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
}

uint32_t ScreenBluetooth::hash() const
{
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t len) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < len; i++) { h ^= b[i]; h *= 16777619u; }
    };
    mix(&m_devOn, sizeof(m_devOn));
    mix(&m_hostOn, sizeof(m_hostOn));
    mix(&m_scanning, sizeof(m_scanning));
    mix(&m_devState, sizeof(m_devState));
    mix(m_devPeer, sizeof(m_devPeer));
    mix(&m_scroll, sizeof(m_scroll));
    mix(&m_rowCount, sizeof(m_rowCount));
    for (int i = 0; m_rows && i < m_rowCount; i++) {
        const Row& r = m_rows[i];
        mix(r.addr, 6);
        mix(r.name, sizeof(r.name));
        int8_t rssi = r.rssi == NO_RSSI ? NO_RSSI : (int8_t)(r.rssi / 4);   // less churn
        mix(&rssi, 1);
        mix(&r.known, sizeof(r.known));
        mix(&r.slot, sizeof(r.slot));
        mix(&r.status, sizeof(r.status));
    }
    return h;
}

// --- Geometry ---------------------------------------------------------------

int ScreenBluetooth::listTop() const
{
    return UI_CONTENT_Y + MARGIN + BTN_H + MARGIN + STATUS_H;
}

int ScreenBluetooth::visibleRows() const
{
    int h = UI_CONTENT_Y + UI_CONTENT_HEIGHT - listTop() - MARGIN / 2;
    int n = h / ROW_H;
    return n > 0 ? n : 1;
}

static void button_rect(int i, int& x, int& y, int& w, int& h)
{
    w = (UI_SCREEN_WIDTH - 3 * MARGIN) / 2;
    h = BTN_H;
    x = MARGIN + i * (w + MARGIN);
    y = UI_CONTENT_Y + MARGIN;
}

// --- Screen -----------------------------------------------------------------

void ScreenBluetooth::enter()
{
    m_isActive = true;
    m_scroll = 0;
    m_touchId = -1;
    snapshot();
    m_lastHash = hash();
    m_lastPollUs = esp_timer_get_time();
}

void ScreenBluetooth::leave()
{
    m_isActive = false;
    m_touchId = -1;
}

void ScreenBluetooth::update()
{
    if (!m_isActive) return;
    int64_t now = esp_timer_get_time();
    if (now - m_lastPollUs >= POLL_US || m_needsRedraw) {
        m_lastPollUs = now;
        snapshot();
        uint32_t h = hash();
        if (h != m_lastHash || m_needsRedraw) {
            m_lastHash = h;
            m_needsRedraw = false;
            ui_strip::repaint_content([&]() { draw(); });
        }
    }
}

static void draw_text(int x, int y, const char* s, uint16_t fg, uint16_t bg, int maxChars)
{
    char buf[48];
    int n = (int)strlen(s);
    if (maxChars > (int)sizeof(buf) - 1) maxChars = (int)sizeof(buf) - 1;
    if (n > maxChars) n = maxChars;
    memcpy(buf, s, (size_t)n);
    buf[n] = '\0';
    ui_gfx().setTextColor(fg, bg);
    ui_gfx().setCursor(x, y);
    ui_gfx().print(buf);
}

static const char* device_state_text(int st)
{
    switch (st) {
        case BLE_MIDI_DEVICE_OFF:         return "off";
        case BLE_MIDI_DEVICE_STARTING:    return "starting";
        case BLE_MIDI_DEVICE_ADVERTISING: return "advertising";
        case BLE_MIDI_DEVICE_LINKED:      return "linked";
        case BLE_MIDI_DEVICE_CONNECTED:   return "connected";
        default:                          return "error";
    }
}

void ScreenBluetooth::draw()
{
    ui_gfx().setTextSize(TS);

    // Role toggles
    for (int i = 0; i < 2; i++) {
        int x, y, w, h;
        button_rect(i, x, y, w, h);
        bool on = i == 0 ? m_devOn : m_hostOn;
        uint16_t bg = on ? UI_COLOR_DARKGREEN : UI_COLOR_DARKGRAY;
        ui_gfx().fillRoundRect(x, y, w, h, 4 * TS, bg);
        char label[24];
        snprintf(label, sizeof(label), "%s: %s", i == 0 ? "Device" : "Host", on ? "ON" : "OFF");
        int tw = (int)strlen(label) * GLYPH_W;
        draw_text(x + (w - tw) / 2, y + (h - GLYPH_H) / 2, label, UI_COLOR_WHITE, bg, 23);
    }

    // Device role: name and state
    char line[64];
    int sy = UI_CONTENT_Y + MARGIN + BTN_H + (MARGIN + STATUS_H - GLYPH_H) / 2;
    if (m_devPeer[0]) {
        snprintf(line, sizeof(line), "\"%s\" %s %s", BLE_MIDI_DEVICE_name(), device_state_text(m_devState), m_devPeer);
    } else {
        snprintf(line, sizeof(line), "\"%s\" %s%s", BLE_MIDI_DEVICE_name(), device_state_text(m_devState),
                 m_scanning ? "  (scanning...)" : "");
    }
    draw_text(MARGIN, sy, line, UI_COLOR_GRAY, UI_COLOR_BLACK, (UI_SCREEN_WIDTH - 2 * MARGIN) / GLYPH_W);

    // Peripherals
    int top = listTop();
    int rows = visibleRows();
    int listW = UI_SCREEN_WIDTH - 2 * MARGIN;
    if (m_rowCount == 0) {
        const char* msg = m_hostOn || m_scanning ? "No BLE-MIDI devices. Tap Scan." : "Host is off. Tap Scan to look.";
        draw_text(MARGIN + 4 * TS, top + (ROW_H - GLYPH_H) / 2, msg, UI_COLOR_GRAY, UI_COLOR_BLACK, listW / GLYPH_W);
        return;
    }
    for (int v = 0; v < rows; v++) {
        int i = m_scroll + v;
        if (i >= m_rowCount) break;
        const Row& r = m_rows[i];
        int y = top + v * ROW_H;
        bool connected = r.slot >= 0 && r.status == BLE_MIDI_HOST_SLOT_CONNECTED;
        bool busy = r.slot >= 0 && !connected;
        uint16_t bg = connected ? UI_COLOR_NAVY : UI_COLOR_BLACK;
        ui_gfx().fillRect(MARGIN, y + 1, listW, ROW_H - 2, bg);
        ui_gfx().drawRect(MARGIN, y + 1, listW, ROW_H - 2, UI_COLOR_DARKGRAY);

        int ty = y + (ROW_H - GLYPH_H) / 2;
        char name[26];
        if (r.name[0]) snprintf(name, sizeof(name), "%s%s", r.known ? "*" : " ", r.name);
        else snprintf(name, sizeof(name), "%s%02X:%02X:%02X", r.known ? "*" : " ", r.addr[3], r.addr[4], r.addr[5]);
        int nameChars = (listW - FORGET_W) / GLYPH_W - 18;
        if (nameChars < 8) nameChars = 8;
        draw_text(MARGIN + 3 * TS, ty, name, UI_COLOR_WHITE, bg, nameChars);

        char right[24];
        const char* st = connected ? "Connected" : busy ? "Connecting" : "";
        if (r.rssi != NO_RSSI) snprintf(right, sizeof(right), "%4d %s", (int)r.rssi, st);
        else snprintf(right, sizeof(right), "%s", st);
        int rw = (int)strlen(right) * GLYPH_W;
        int rx = MARGIN + listW - FORGET_W - 3 * TS - rw;
        draw_text(rx, ty, right, connected ? UI_COLOR_CYAN : busy ? UI_COLOR_YELLOW : UI_COLOR_GRAY, bg, 16);

        if (r.known) {
            int fx = MARGIN + listW - FORGET_W;
            ui_gfx().fillRect(fx, y + 1, FORGET_W, ROW_H - 2, UI_COLOR_DARKGRAY);
            draw_text(fx + (FORGET_W - GLYPH_W) / 2, ty, "x", UI_COLOR_WHITE, UI_COLOR_DARKGRAY, 1);
        }
    }
    // Scroll hint
    if (m_rowCount > rows) {
        char more[32];
        snprintf(more, sizeof(more), "%d/%d", m_scroll + rows < m_rowCount ? m_scroll + rows : m_rowCount, m_rowCount);
        int mw = (int)strlen(more) * GLYPH_W;
        draw_text(UI_SCREEN_WIDTH - MARGIN - mw, sy, more, UI_COLOR_GRAY, UI_COLOR_BLACK, 15);
    }
}

void ScreenBluetooth::tap(int x, int y)
{
    for (int i = 0; i < 2; i++) {
        int bx, by, bw, bh;
        button_rect(i, bx, by, bw, bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (i == 0) ble_midi_settings_set_device(!m_devOn);
            else ble_midi_settings_set_host(!m_hostOn);
            m_needsRedraw = true;
            return;
        }
    }
    int top = listTop();
    if (y < top) return;
    int v = (y - top) / ROW_H;
    if (v >= visibleRows()) return;
    int i = m_scroll + v;
    if (i >= m_rowCount) return;
    const Row& r = m_rows[i];
    int listW = UI_SCREEN_WIDTH - 2 * MARGIN;
    if (r.known && x >= MARGIN + listW - FORGET_W) {
        BLE_MIDI_HOST_forget(r.addr);
    } else if (r.slot >= 0) {
        BLE_MIDI_HOST_disconnect(r.slot);
    } else {
        BLE_MIDI_HOST_connect(r.addr, r.type);
    }
    m_needsRedraw = true;
}

void ScreenBluetooth::onTouch(int touchId, int x, int y, bool pressed)
{
    if (pressed) {
        if (m_touchId >= 0) return;
        m_touchId = touchId;
        m_touchX = x;
        m_touchY = y;
        m_dragAccum = 0;
        m_dragged = false;
        return;
    }
    if (touchId != m_touchId) return;
    m_touchId = -1;
    if (!m_dragged) tap(m_touchX, m_touchY);
}

void ScreenBluetooth::onTouchMove(int touchId, int x, int y)
{
    (void)x;
    if (touchId != m_touchId) return;
    int dy = y - m_touchY;
    m_touchY = y;
    m_dragAccum += dy;
    if (!m_dragged && (m_dragAccum > ROW_H / 2 || m_dragAccum < -ROW_H / 2)) m_dragged = true;
    if (!m_dragged) return;
    int maxScroll = m_rowCount - visibleRows();
    if (maxScroll < 0) maxScroll = 0;
    while (m_dragAccum <= -ROW_H && m_scroll < maxScroll) { m_scroll++; m_dragAccum += ROW_H; m_needsRedraw = true; }
    while (m_dragAccum >= ROW_H && m_scroll > 0) { m_scroll--; m_dragAccum -= ROW_H; m_needsRedraw = true; }
    if (m_scroll == 0 && m_dragAccum > ROW_H) m_dragAccum = ROW_H;
    if (m_scroll == maxScroll && m_dragAccum < -ROW_H) m_dragAccum = -ROW_H;
}

void ScreenBluetooth::onNavCenter()
{
    if (BLE_MIDI_HOST_scanning()) BLE_MIDI_HOST_scan_stop();
    else BLE_MIDI_HOST_scan_start(SCAN_MS);
    m_needsRedraw = true;
}

const char* ScreenBluetooth::getTitle()
{
    return "Bluetooth";
}

const char* ScreenBluetooth::getNavCenterLabel()
{
    return "Scan";
}

void ScreenBluetooth::getTileStatus(char* buf, int len)
{
    if (len <= 0) return;
    int n = BLE_MIDI_DEVICE_is_connected() ? 1 : 0;
    for (int s = 0; s < BLE_MIDI_HOST_MAX_DEVICES; s++) {
        if (BLE_MIDI_HOST_dev_connected(s)) n++;
    }
    if (n > 0) snprintf(buf, (size_t)len, "%d linked", n);
    else if (BLE_MIDI_DEVICE_is_enabled() || BLE_MIDI_HOST_is_enabled()) snprintf(buf, (size_t)len, "On");
    else snprintf(buf, (size_t)len, "Off");
}
