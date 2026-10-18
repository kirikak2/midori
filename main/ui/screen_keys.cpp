#include "sdkconfig.h"
#include "screen_keys.h"
#include "ui_keys.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

// Global instance
static ScreenKeys s_screenKeys;

ScreenKeys& getScreenKeys()
{
    return s_screenKeys;
}

static constexpr int GLYPH_W = 6;
static constexpr int GLYPH_H = 8;

#if defined(UI_LAYOUT_LARGE)
static constexpr int LABEL_TEXT_SIZE = 2;
static constexpr int KEY_GAP = 2;          // Black between white keys
#else
static constexpr int LABEL_TEXT_SIZE = 1;
static constexpr int KEY_GAP = 1;
#endif

// Velocity across a key's length with velocity_mode :position: the top edge
// plays this softly, the bottom edge at 127.
static constexpr int VELOCITY_TOP = 30;

static void noteName(uint8_t note, char* buf, int len)
{
    static const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    note &= 0x7F;
    snprintf(buf, len, "%s%d", NAMES[note % 12], (int)(note / 12) - 1);
}

// Dark text on light keys, light text on dark ones
static uint16_t labelColorOn(uint16_t fill)
{
    int r = (fill >> 11) & 0x1F;
    int g = (fill >> 5) & 0x3F;
    int b = fill & 0x1F;
    int luma = r * 2 * 299 + g * 587 + b * 2 * 114;   // Out of ~63000
    return luma > 32000 ? UI_COLOR_DARKGRAY : UI_COLOR_WHITE;
}

ScreenKeys::ScreenKeys()
    : m_isActive(false)
    , m_gen(0)
{
    memset(&m_geom, 0, sizeof(m_geom));
    resetArea();
}

void ScreenKeys::setArea(int x, int y, int w, int h)
{
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;
}

void ScreenKeys::resetArea()
{
    setArea(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT);
}

// --- Geometry ----------------------------------------------------------------

void ScreenKeys::computeGeom()
{
    Geom& g = m_geom;
    g.x = m_areaX;
    g.y = m_areaY;
    g.w = m_areaW;
    g.h = m_areaH;
    g.low = ui_keys::low();
    g.top = ui_keys::top();
    g.whites = 0;
    for (int n = g.low; n <= g.top; n++) {
        if (!ui_keys::is_black(n)) g.whites++;
    }
    if (g.whites < 1) g.whites = 1;

    int whiteW = g.w / g.whites;
    g.blackW = whiteW * 58 / 100;
    if (g.blackW < 3) g.blackW = 3;
    g.blackH = g.h * 62 / 100;
}

int ScreenKeys::whiteIndex(int note) const
{
    int i = 0;
    for (int n = m_geom.low; n < note; n++) {
        if (!ui_keys::is_black(n)) i++;
    }
    return i;
}

// Whole pixels per key, the remainder spread across the keyboard, so the
// white keys fill the area exactly.
int ScreenKeys::whiteX(int index) const
{
    return m_geom.x + index * m_geom.w / m_geom.whites;
}

void ScreenKeys::keyRect(int note, int& x, int& y, int& w, int& h) const
{
    int i = whiteIndex(note);
    if (ui_keys::is_black(note)) {
        // Centred on the boundary between the white keys either side
        x = whiteX(i) - m_geom.blackW / 2;
        y = m_geom.y;
        w = m_geom.blackW;
        h = m_geom.blackH;
    } else {
        x = whiteX(i);
        y = m_geom.y;
        w = whiteX(i + 1) - x;
        h = m_geom.h;
    }
}

int ScreenKeys::hitTest(int x, int y, int& velocity) const
{
    const Geom& g = m_geom;
    velocity = ui_keys::velocity();
    if (x < g.x || x >= g.x + g.w || y < g.y || y >= g.y + g.h) return -1;

    int note = -1;
    int keyTop = g.y, keyH = g.h;

    // Black keys sit on top of the white ones, so they win where they are
    if (y < g.y + g.blackH) {
        for (int n = g.low + 1; n < g.top; n++) {
            if (!ui_keys::is_black(n)) continue;
            int kx, ky, kw, kh;
            keyRect(n, kx, ky, kw, kh);
            if (x >= kx && x < kx + kw) {
                note = n;
                keyH = g.blackH;
                break;
            }
        }
    }
    if (note < 0) {
        int index = (x - g.x) * g.whites / g.w;
        if (index >= g.whites) index = g.whites - 1;
        // whiteX rounds, so the division can land one key off at an edge
        while (index > 0 && x < whiteX(index)) index--;
        while (index + 1 < g.whites && x >= whiteX(index + 1)) index++;
        int i = 0;
        for (int n = g.low; n <= g.top; n++) {
            if (ui_keys::is_black(n)) continue;
            if (i == index) { note = n; break; }
            i++;
        }
    }

    if (note >= 0 && ui_keys::velocity_by_position() && keyH > 1) {
        int rel = (y - keyTop) * 1000 / (keyH - 1);
        if (rel > 1000) rel = 1000;
        velocity = VELOCITY_TOP + (127 - VELOCITY_TOP) * rel / 1000;
    }
    return note;
}

// --- Drawing -----------------------------------------------------------------

void ScreenKeys::render(LovyanGFX* g, int ox, int oy)
{
    const Geom& geo = m_geom;
    bool labels = ui_keys::labels();
    char label[8];

    // White keys first, then the black keys over them
    int i = 0;
    for (int n = geo.low; n <= geo.top; n++) {
        if (ui_keys::is_black(n)) continue;
        int x = whiteX(i);
        int w = whiteX(i + 1) - x;
        i++;
        uint16_t fill = ui_keys::key_fill(n);
        int gap = (w > KEY_GAP * 4) ? KEY_GAP : 0;
        g->fillRect(x - ox, geo.y - oy, w - gap, geo.h, fill);

        if (labels && n % 12 == 0) {
            noteName((uint8_t)n, label, sizeof(label));
            int size = LABEL_TEXT_SIZE;
            int tw = (int)strlen(label) * GLYPH_W * size;
            if (tw > w - gap - 2 && size > 1) {
                size = 1;
                tw = (int)strlen(label) * GLYPH_W;
            }
            int th = GLYPH_H * size;
            // Only where it fits below the black keys
            if (tw <= w - gap - 2 && geo.h - geo.blackH >= th + 4) {
                g->setTextSize(size);
                g->setTextColor(labelColorOn(fill), fill);
                g->setCursor(x + (w - gap - tw) / 2 - ox, geo.y + geo.h - th - 4 - oy);
                g->print(label);
            }
        }
    }

    for (int n = geo.low + 1; n < geo.top; n++) {
        if (!ui_keys::is_black(n)) continue;
        int x, y, w, h;
        keyRect(n, x, y, w, h);
        g->fillRect(x - ox, y - oy, w, h, ui_keys::key_fill(n));
        g->drawRect(x - ox, y - oy, w, h, UI_COLOR_DARKGRAY);
    }
}

void ScreenKeys::paintRect(int x, int y, int w, int h)
{
    // Keep inside the keyboard (a black key at the edge of the range pokes
    // half a key past it)
    if (x < m_geom.x) { w -= m_geom.x - x; x = m_geom.x; }
    if (x + w > m_geom.x + m_geom.w) w = m_geom.x + m_geom.w - x;
    if (w <= 0 || h <= 0) return;

    if (!ui_strip::paint(x, y, w, h, UI_COLOR_BLACK, [&](LovyanGFX* g, int ox, int oy) {
            render(g, ox, oy);
        })) {
        // No strip: straight to the panel, clipped to the same rectangle
        LovyanGFX& g = ui_gfx();
        g.setClipRect(x, y, w, h);
        g.fillRect(x, y, w, h, UI_COLOR_BLACK);
        render(&g, 0, 0);
        g.clearClipRect();
    }
}

// Repaints the dirty keys of the range. Keys next to each other are merged
// into one rectangle, so a chord costs a few paints rather than one per key.
void ScreenKeys::paintDirty(const uint32_t dirty[4])
{
    int spanX = 0, spanEnd = -1;
    for (int n = m_geom.low; n <= m_geom.top; n++) {
        if (!(dirty[n >> 5] & (1u << (n & 31)))) continue;
        int x, y, w, h;
        keyRect(n, x, y, w, h);
        // A white key's own column; the black keys over it come along in render()
        if (spanEnd >= spanX && x <= spanEnd) {
            if (x + w > spanEnd) spanEnd = x + w;
            if (x < spanX) spanX = x;
            continue;
        }
        if (spanEnd > spanX) paintRect(spanX, m_geom.y, spanEnd - spanX, m_geom.h);
        spanX = x;
        spanEnd = x + w;
    }
    if (spanEnd > spanX) paintRect(spanX, m_geom.y, spanEnd - spanX, m_geom.h);
}

// --- Screen lifecycle --------------------------------------------------------

void ScreenKeys::enter()
{
    m_isActive = true;
    m_gen = ui_keys::generation();
    computeGeom();
    // UIManager (or the layout) paints everything right after this
    uint32_t dirty[4];
    bool full;
    ui_keys::take_dirty(dirty, full);
}

void ScreenKeys::leave()
{
    // Fingers cannot reach a keyboard that is not showing
    ui_keys::release_all();
    m_isActive = false;
}

void ScreenKeys::update()
{
    if (!m_isActive) return;

    uint32_t gen = ui_keys::generation();
    if (gen != m_gen) {
        // A script reset the keyboard: whatever the fingers hold goes off
        // where it was sent
        m_gen = gen;
        ui_keys::release_all();
    }

    uint32_t dirty[4];
    bool full;
    ui_keys::take_dirty(dirty, full);
    if (full || m_geom.low != ui_keys::low() || m_geom.top != ui_keys::top()) {
        computeGeom();
        paintRect(m_geom.x, m_geom.y, m_geom.w, m_geom.h);
        return;
    }
    if (dirty[0] | dirty[1] | dirty[2] | dirty[3]) paintDirty(dirty);
}

void ScreenKeys::draw()
{
    computeGeom();
    paintRect(m_geom.x, m_geom.y, m_geom.w, m_geom.h);
}

void ScreenKeys::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive) return;
    if (pressed) {
        int velocity;
        int note = hitTest(x, y, velocity);
        ui_keys::touch_down(touchId, note, velocity);
    } else {
        ui_keys::touch_up(touchId);
    }
}

void ScreenKeys::onTouchMove(int touchId, int x, int y)
{
    if (!m_isActive) return;
    int velocity;
    int note = hitTest(x, y, velocity);
    ui_keys::touch_move(touchId, note, velocity);
}

const char* ScreenKeys::getTitle()
{
    return "Keys";
}

void ScreenKeys::getTileStatus(char* buf, int len)
{
    char lo[8], hi[8];
    noteName((uint8_t)ui_keys::low(), lo, sizeof(lo));
    noteName((uint8_t)ui_keys::top(), hi, sizeof(hi));
    snprintf(buf, len, "%s-%s", lo, hi);
}
