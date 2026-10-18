#include "sdkconfig.h"
#include "screen_home.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "esp_timer.h"

// Global instance
static ScreenHome s_screenHome;

ScreenHome& getScreenHome()
{
    return s_screenHome;
}

#if defined(UI_LAYOUT_LARGE)
static constexpr int COLS = 5;
static constexpr int GAP = 16;
static constexpr int MAX_TEXT_SIZE = 3;
#else
static constexpr int COLS = 4;
static constexpr int GAP = 6;
static constexpr int MAX_TEXT_SIZE = 2;
#endif
static constexpr int ROWS = 3;

static constexpr int GLYPH_W = 6;
static constexpr int GLYPH_H = 8;

// Tile states ("120 BPM", "Bank A") are polled this often while Home shows
static constexpr int64_t POLL_INTERVAL_US = 500000;

// Tiles are sized for the longest fixed name ("MIDI Info"), so every tile of
// the same board uses the same text size; only a long layout title shrinks.
static constexpr int NAME_FIT_CHARS = 9;

// Group colours: performance surfaces / tempo & connection / system
static const uint16_t GROUP_BG[] = { 0x01E0, 0x0010, UI_COLOR_DARKGRAY };

static const char* screenName(ui_screen_index_t screen)
{
    switch (screen) {
        case UI_SCREEN_MAIN:      return "Tempo";
        case UI_SCREEN_PADS:      return "Pads";
        case UI_SCREEN_MIDI_INFO: return "MIDI Info";
        case UI_SCREEN_LOGS:      return "Logs";
        case UI_SCREEN_SCRIPTS:   return "Scripts";
        case UI_SCREEN_SETTINGS:  return "Settings";
        case UI_SCREEN_TOMBOLA:   return "Tombola";
        case UI_SCREEN_KNOBS:     return "Knobs";
        case UI_SCREEN_XYPAD:     return "XYPad";
        case UI_SCREEN_KEYS:      return "Keys";
        default:                  return "";
    }
}

ScreenHome::ScreenHome()
    : m_isActive(false)
    , m_itemCount(0)
    , m_lastPollUs(0)
    , m_scrollRow(0)
    , m_touchId(-1)
    , m_startX(0), m_startY(0), m_lastY(0)
    , m_dragAccum(0)
    , m_scrolled(false)
    , m_pressedTile(-1)
{
    memset(m_status, 0, sizeof(m_status));
}

// --- Geometry --------------------------------------------------------------

int ScreenHome::columns() const { return COLS; }
int ScreenHome::visibleRows() const { return ROWS; }

int ScreenHome::totalRows() const
{
    return (m_itemCount + COLS - 1) / COLS;
}

void ScreenHome::tileRect(int slot, int& x, int& y, int& w, int& h) const
{
    // Distributed like the layout DSL's tracks: whole pixels, the remainder
    // handed out from the left/top so the grid fills the area exactly.
    int availW = UI_SCREEN_WIDTH - GAP * 2 - GAP * (COLS - 1);
    int availH = UI_CONTENT_HEIGHT - GAP * 2 - GAP * (ROWS - 1);
    int col = slot % COLS;
    int row = slot / COLS;

    int baseW = availW / COLS, extraW = availW % COLS;
    int baseH = availH / ROWS, extraH = availH % ROWS;

    x = GAP;
    for (int c = 0; c < col; c++) x += baseW + (c < extraW ? 1 : 0) + GAP;
    w = baseW + (col < extraW ? 1 : 0);

    y = UI_CONTENT_Y + GAP;
    for (int r = 0; r < row; r++) y += baseH + (r < extraH ? 1 : 0) + GAP;
    h = baseH + (row < extraH ? 1 : 0);
}

int ScreenHome::hitTest(int x, int y) const
{
    for (int slot = 0; slot < COLS * ROWS; slot++) {
        int item = m_scrollRow * COLS + slot;
        if (item >= m_itemCount) break;
        int tx, ty, tw, th;
        tileRect(slot, tx, ty, tw, th);
        if (x >= tx && x < tx + tw && y >= ty && y < ty + th) return item;
    }
    return -1;
}

// --- Content ---------------------------------------------------------------

void ScreenHome::refreshItems(MenuItem* items, int& count)
{
    count = UIManager::getInstance().buildMenu(items, MENU_MAX_ITEMS);
}

void ScreenHome::tileLabel(const MenuItem& item, char* buf, int len) const
{
    if (item.screen == UI_SCREEN_LAYOUT) {
        ui_layout_title(item.arg, buf, len);
    } else {
        snprintf(buf, len, "%s", screenName(item.screen));
    }
}

void ScreenHome::tileStatus(const MenuItem& item, char* buf, int len) const
{
    buf[0] = '\0';
    if (item.screen == UI_SCREEN_LAYOUT) return;
    // Ask the screen itself; it knows what is worth a glance from the menu
    Screen* screen = UIManager::getInstance().getScreen(item.screen);
    if (screen) screen->getTileStatus(buf, len);
}

// --- Drawing ---------------------------------------------------------------

void ScreenHome::drawTile(int item, bool pressed)
{
    int slot = item - m_scrollRow * COLS;
    if (slot < 0 || slot >= COLS * ROWS || item >= m_itemCount) return;

    int x, y, w, h;
    tileRect(slot, x, y, w, h);

    const MenuItem& it = m_items[item];
    uint16_t bg = GROUP_BG[it.group < 3 ? it.group : 2];
    if (pressed) bg = ui_lighten_color(bg);

    char label[24];
    tileLabel(it, label, sizeof(label));
    const char* status = m_status[item];

    auto render = [&](LovyanGFX* g, int ox, int oy) {
        int lx = x - ox, ly = y - oy;
        g->fillRoundRect(lx, ly, w, h, 6, bg);
        g->drawRoundRect(lx, ly, w, h, 6, pressed ? UI_COLOR_YELLOW : UI_COLOR_GRAY);

        // Name: the size that fits the longest fixed name, smaller only for a
        // layout title that would not fit at it, truncated past that.
        int size = (w - 8) / (NAME_FIT_CHARS * GLYPH_W);
        if (size > MAX_TEXT_SIZE) size = MAX_TEXT_SIZE;
        if (size < 1) size = 1;
        int len = (int)strlen(label);
        while (size > 1 && len * GLYPH_W * size > w - 8) size--;
        int maxChars = (w - 8) / (GLYPH_W * size);
        if (maxChars > 0 && len > maxChars) { label[maxChars] = '\0'; len = maxChars; }

        int statusSize = size > 1 ? size - 1 : 1;
        bool hasStatus = status[0] != '\0';
        int nameH = GLYPH_H * size;
        int statusH = hasStatus ? GLYPH_H * statusSize + 4 : 0;
        int top = ly + (h - nameH - statusH) / 2;

        g->setTextSize(size);
        g->setTextColor(UI_COLOR_WHITE, bg);
        g->setCursor(lx + (w - len * GLYPH_W * size) / 2, top);
        g->print(label);

        if (hasStatus) {
            char st[STATUS_LEN];
            snprintf(st, sizeof(st), "%s", status);
            int slen = (int)strlen(st);
            int smax = (w - 8) / (GLYPH_W * statusSize);
            if (smax > 0 && slen > smax) { st[smax] = '\0'; slen = smax; }
            g->setTextSize(statusSize);
            g->setTextColor(UI_COLOR_YELLOW, bg);
            g->setCursor(lx + (w - slen * GLYPH_W * statusSize) / 2, top + nameH + 4);
            g->print(st);
        }
    };

    if (!ui_strip::paint(x, y, w, h, UI_COLOR_BLACK, render)) {
        ui_gfx().fillRect(x, y, w, h, UI_COLOR_BLACK);
        render(&ui_gfx(), 0, 0);
    }
}

void ScreenHome::drawAll()
{
    // Scroll indicator: a thin bar on the right edge when not everything fits
    int rows = totalRows();
    if (rows > ROWS) {
        int barH = UI_CONTENT_HEIGHT * ROWS / rows;
        int barY = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - barH) * m_scrollRow / (rows - ROWS);
        ui_gfx().fillRect(UI_SCREEN_WIDTH - 3, barY, 3, barH, UI_COLOR_GRAY);
    }
    for (int slot = 0; slot < COLS * ROWS; slot++) {
        int item = m_scrollRow * COLS + slot;
        if (item >= m_itemCount) break;
        drawTile(item, item == m_pressedTile);
    }
}

void ScreenHome::poll(bool force)
{
    MenuItem items[MENU_MAX_ITEMS];
    int count;
    refreshItems(items, count);

    bool sameItems = (count == m_itemCount);
    for (int i = 0; sameItems && i < count; i++) {
        sameItems = items[i].screen == m_items[i].screen && items[i].arg == m_items[i].arg;
    }

    if (!sameItems || force) {
        memcpy(m_items, items, sizeof(MenuItem) * count);
        m_itemCount = count;
        int maxScroll = totalRows() - ROWS;
        if (maxScroll < 0) maxScroll = 0;
        if (m_scrollRow > maxScroll) m_scrollRow = maxScroll;
        for (int i = 0; i < count; i++) tileStatus(m_items[i], m_status[i], STATUS_LEN);
        if (!force) {
            m_pressedTile = -1;
            ui_strip::repaint_content([&]() { drawAll(); });
        }
        return;
    }

    for (int i = 0; i < count; i++) {
        char st[STATUS_LEN];
        tileStatus(m_items[i], st, STATUS_LEN);
        if (strcmp(st, m_status[i]) != 0) {
            memcpy(m_status[i], st, STATUS_LEN);
            drawTile(i, i == m_pressedTile);
        }
    }
}

// --- Screen lifecycle ------------------------------------------------------

void ScreenHome::enter()
{
    m_isActive = true;
    m_touchId = -1;
    m_pressedTile = -1;
    // draw() follows straight away and paints from this snapshot
    poll(true);
    m_lastPollUs = esp_timer_get_time();
}

void ScreenHome::leave()
{
    m_isActive = false;
    m_touchId = -1;
    m_pressedTile = -1;
}

void ScreenHome::update()
{
    if (!m_isActive) return;
    int64_t now = esp_timer_get_time();
    if (now - m_lastPollUs < POLL_INTERVAL_US) return;
    m_lastPollUs = now;
    poll(false);
}

void ScreenHome::draw()
{
    drawAll();
}

// --- Touch -----------------------------------------------------------------

void ScreenHome::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive) return;

    if (pressed) {
        if (m_touchId >= 0) return;
        m_touchId = touchId;
        m_startX = x;
        m_startY = y;
        m_lastY = y;
        m_dragAccum = 0;
        m_scrolled = false;
        m_pressedTile = hitTest(x, y);
        if (m_pressedTile >= 0) drawTile(m_pressedTile, true);
        return;
    }

    if (touchId != m_touchId) return;
    m_touchId = -1;

    int tile = m_pressedTile;
    m_pressedTile = -1;
    if (tile < 0) return;

    if (m_scrolled || hitTest(x, y) != tile) {
        drawTile(tile, false);
        return;
    }

    // Leaves this screen; nothing below may touch its state any more
    const MenuItem item = m_items[tile];
    UIManager::getInstance().setScreen(item.screen, item.arg, UI_SCREEN_SOURCE_TOUCH);
}

void ScreenHome::onTouchMove(int touchId, int x, int y)
{
    (void)x;
    if (!m_isActive || touchId != m_touchId) return;

    int dy = y - m_lastY;
    m_lastY = y;

    int slop = UI_CONTENT_HEIGHT / 20;
    if (!m_scrolled && abs(y - m_startY) + abs(x - m_startX) > slop) {
        m_scrolled = true;
        if (m_pressedTile >= 0) {
            int t = m_pressedTile;
            m_pressedTile = -1;
            drawTile(t, false);
        }
    }
    if (!m_scrolled || totalRows() <= ROWS) return;

    // One row per row-height dragged; dragging up shows later rows
    int tx, ty, tw, rowH;
    tileRect(0, tx, ty, tw, rowH);
    rowH += GAP;
    m_dragAccum -= dy;
    int maxScroll = totalRows() - ROWS;
    int row = m_scrollRow;
    while (m_dragAccum >= rowH && row < maxScroll) { row++; m_dragAccum -= rowH; }
    while (m_dragAccum <= -rowH && row > 0)        { row--; m_dragAccum += rowH; }
    if (row != m_scrollRow) {
        m_scrollRow = row;
        ui_strip::repaint_content([&]() { drawAll(); });
    }
}

const char* ScreenHome::getTitle()
{
    return "Home";
}

const char* ScreenHome::getNavCenterLabel()
{
    // Nothing in the middle on Home
    return "";
}
