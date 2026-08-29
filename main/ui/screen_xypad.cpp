#include "sdkconfig.h"
#include "screen_xypad.h"
#include "ui_ppa.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "SCREEN_XYPAD";

// Global instance
static ScreenXYPad s_screenXYPad;

ScreenXYPad& getScreenXYPad()
{
    return s_screenXYPad;
}

#if defined(UI_LAYOUT_LARGE)
static constexpr int DOT_RADIUS = 16;
static constexpr int LABEL_TEXT_SIZE = 3;
static constexpr int64_t DRAW_INTERVAL_US = 33000;  // ~30fps, same pacing as Tombola
static constexpr int RIPPLE_MAX_RADIUS = 160;
#else
static constexpr int DOT_RADIUS = 8;
static constexpr int LABEL_TEXT_SIZE = 1;
static constexpr int64_t DRAW_INTERVAL_US = 50000;  // ~20fps
static constexpr int RIPPLE_MAX_RADIUS = 80;
#endif

static constexpr int GLYPH_W = 6;
static constexpr int GLYPH_H = 8;
static constexpr int LABEL_MAX_CHARS = 8;   // "X:-100" worst case plus slack

static constexpr int64_t RIPPLE_DURATION_US = 500000;    // 0.5s expanding ring
static constexpr int64_t TRAIL_DURATION_US = 2000000;    // 2s afterglow
static constexpr int64_t TRAIL_MIN_INTERVAL_US = 50000;  // Sample at most every 50ms

// One color per slot, borrowed from the same palette Tombola's balls use.
static const uint16_t SLOT_COLORS[UI_XYPAD_MAX_TOUCHES] = {
    UI_COLOR_RED, UI_COLOR_CYAN, UI_COLOR_YELLOW, UI_COLOR_GREEN, UI_COLOR_MAGENTA,
};

// Linearly blends an RGB565 color toward black (t=0 unchanged, t=1 black).
// Used for both the ripple ring and the trail's afterglow -- proportional
// scaling reads more like a fade than repeated ui_darken_color() steps, which
// subtract a fixed amount each time.
static uint16_t fadeColor(uint16_t color, float t)
{
    if (t <= 0.0f) return color;
    if (t >= 1.0f) return UI_COLOR_BLACK;
    float k = 1.0f - t;
    uint16_t r = (uint16_t)(((color >> 11) & 0x1F) * k);
    uint16_t g = (uint16_t)(((color >> 5) & 0x3F) * k);
    uint16_t b = (uint16_t)((color & 0x1F) * k);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

ScreenXYPad::ScreenXYPad()
    : m_isActive(false)
    , m_sprite(nullptr)
    , m_lastDrawUs(0)
    , m_hadContent(false)
{
    memset(m_touchKnown, 0, sizeof(m_touchKnown));
    memset(m_touchX, 0, sizeof(m_touchX));
    memset(m_touchY, 0, sizeof(m_touchY));
    memset(m_trail, 0, sizeof(m_trail));
    memset(m_trailHead, 0, sizeof(m_trailHead));
    memset(m_trailCount, 0, sizeof(m_trailCount));
    memset(m_lastTrailPushUs, 0, sizeof(m_lastTrailPushUs));
    memset(m_ripples, 0, sizeof(m_ripples));
    memset(m_drawnFallback, 0, sizeof(m_drawnFallback));
}

void ScreenXYPad::contentRect(int& x, int& y, int& w, int& h)
{
    x = 0;
    y = UI_CONTENT_Y;
    w = UI_SCREEN_WIDTH;
    h = UI_CONTENT_HEIGHT;
}

uint16_t ScreenXYPad::slotColor(int index)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return UI_COLOR_WHITE;
    return SLOT_COLORS[index];
}

void ScreenXYPad::noteName(uint8_t note, char* buf, int bufSize)
{
    static const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    int octave = (int)(note / 12) - 1;
    snprintf(buf, bufSize, "%s%d", NAMES[note % 12], octave);
}

// --- Sprite lifecycle --------------------------------------------------

void ScreenXYPad::allocateSprite()
{
    if (m_sprite) return;

    int cx, cy, cw, ch;
    contentRect(cx, cy, cw, ch);

    m_sprite = new LGFX_Sprite(&M5.Lcd);
    m_sprite->setColorDepth(16);
    m_sprite->setPsram(false);
    void* result = m_sprite->createSprite(cw, ch);
    if (!result) {
        ESP_LOGW(TAG, "Content-area sprite alloc in internal RAM failed (%dx%d), trying PSRAM",
                 cw, ch);
        m_sprite->setPsram(true);
        result = m_sprite->createSprite(cw, ch);
    }
    if (!result) {
        // Not fatal: paintFrame() falls back to plain, non-animated direct
        // draw when m_sprite is null.
        ESP_LOGE(TAG, "Failed to create XYPad sprite, falling back to direct draw");
        delete m_sprite;
        m_sprite = nullptr;
    } else {
        ESP_LOGI(TAG, "XYPad sprite created: %dx%d", cw, ch);
    }
}

// --- Animation state -----------------------------------------------------

void ScreenXYPad::pushTrailPoint(int index, int x, int y, int64_t now)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return;
    if (now - m_lastTrailPushUs[index] < TRAIL_MIN_INTERVAL_US) return;
    m_lastTrailPushUs[index] = now;

    int head = m_trailHead[index];
    m_trail[index][head].x = x;
    m_trail[index][head].y = y;
    m_trail[index][head].atUs = now;
    m_trailHead[index] = (head + 1) % TRAIL_MAX_POINTS;
    if (m_trailCount[index] < TRAIL_MAX_POINTS) m_trailCount[index]++;
}

void ScreenXYPad::spawnRipple(int x, int y, uint16_t color, int64_t now)
{
    int victim = -1;
    for (int i = 0; i < RIPPLE_MAX; i++) {
        if (!m_ripples[i].active) { victim = i; break; }
    }
    if (victim < 0) {
        // Pool full: steal the oldest ring instead of dropping the new tap.
        int64_t oldest = now + 1;
        for (int i = 0; i < RIPPLE_MAX; i++) {
            if (m_ripples[i].startUs < oldest) { oldest = m_ripples[i].startUs; victim = i; }
        }
    }
    m_ripples[victim].active = true;
    m_ripples[victim].x = x;
    m_ripples[victim].y = y;
    m_ripples[victim].startUs = now;
    m_ripples[victim].color = color;
}

void ScreenXYPad::pruneTrails(int64_t now)
{
    for (int s = 0; s < UI_XYPAD_MAX_TOUCHES; s++) {
        while (m_trailCount[s] > 0) {
            int oldestIdx = (m_trailHead[s] - m_trailCount[s] + TRAIL_MAX_POINTS * 4) % TRAIL_MAX_POINTS;
            if (now - m_trail[s][oldestIdx].atUs < TRAIL_DURATION_US) break;
            m_trailCount[s]--;
        }
    }
    for (int i = 0; i < RIPPLE_MAX; i++) {
        if (m_ripples[i].active && now - m_ripples[i].startUs >= RIPPLE_DURATION_US) {
            m_ripples[i].active = false;
        }
    }
}

bool ScreenXYPad::hasAnythingToDraw()
{
    for (int s = 0; s < UI_XYPAD_MAX_TOUCHES; s++) {
        const xypad_slot_t* slot = ui_xypad_get_slot((uint8_t)s);
        if (slot && (slot->active || slot->latched)) return true;
        if (m_trailCount[s] > 0) return true;
    }
    for (int i = 0; i < RIPPLE_MAX; i++) {
        if (m_ripples[i].active) return true;
    }
    return false;
}

// --- Drawing ---------------------------------------------------------------

void ScreenXYPad::renderFrame(LovyanGFX* g, int ox, int oy, int contentW, int64_t now)
{
    // Ripples: an expanding two-pixel-thick ring per touchdown, fading to
    // black as it grows.
    for (int i = 0; i < RIPPLE_MAX; i++) {
        Ripple& r = m_ripples[i];
        if (!r.active) continue;
        int64_t age = now - r.startUs;
        if (age >= RIPPLE_DURATION_US) continue;
        float t = (float)age / (float)RIPPLE_DURATION_US;
        int radius = (int)(t * RIPPLE_MAX_RADIUS);
        if (radius < 1) radius = 1;
        uint16_t color = fadeColor(r.color, t);
        g->drawCircle(r.x - ox, r.y - oy, radius, color);
        if (radius > 1) g->drawCircle(r.x - ox, r.y - oy, radius - 1, color);
    }

    // Trail: shrinking, fading dots behind a moving (or just-released) touch.
    for (int s = 0; s < UI_XYPAD_MAX_TOUCHES; s++) {
        int count = m_trailCount[s];
        int head = m_trailHead[s];
        uint16_t base = slotColor(s);
        for (int k = 0; k < count; k++) {
            int idx = (head - count + k + TRAIL_MAX_POINTS * 4) % TRAIL_MAX_POINTS;
            const TrailPoint& p = m_trail[s][idx];
            int64_t age = now - p.atUs;
            if (age >= TRAIL_DURATION_US) continue;
            float t = (float)age / (float)TRAIL_DURATION_US;
            int radius = (int)((1.0f - t) * (DOT_RADIUS * 0.6f)) + 1;
            g->fillCircle(p.x - ox, p.y - oy, radius, fadeColor(base, t));
        }
    }

    // Current dot + label, on top of everything else.
    for (int s = 0; s < UI_XYPAD_MAX_TOUCHES; s++) {
        const xypad_slot_t* slot = ui_xypad_get_slot((uint8_t)s);
        if (!slot || (!slot->active && !slot->latched) || !m_touchKnown[s]) continue;

        int x = m_touchX[s] - ox;
        int y = m_touchY[s] - oy;
        uint16_t color = slotColor(s);

        if (slot->latched) {
            g->drawCircle(x, y, DOT_RADIUS, color);
        } else {
            g->fillCircle(x, y, DOT_RADIUS, color);
        }

        char label[LABEL_MAX_CHARS + 1];
        if (slot->touch_x_mode == XYPAD_XMODE_CC) {
            snprintf(label, sizeof(label), "X:%d", (int)slot->x);
        } else {
            noteName(slot->note, label, sizeof(label));
        }

        // Flip the label to the dot's left when it would otherwise run past
        // the right edge, instead of letting it print off the sprite/panel
        // and (depending on the GFX driver's wrap behaviour) reappear at the
        // left edge -- what looked like a stuck afterimage there before.
        int textW = (int)strlen(label) * GLYPH_W * LABEL_TEXT_SIZE;
        int labelX = x + DOT_RADIUS + 4;
        if (labelX + textW > contentW) {
            labelX = x - DOT_RADIUS - 4 - textW;
        }
        if (labelX < 0) labelX = 0;
        int labelY = y - (GLYPH_H * LABEL_TEXT_SIZE) / 2;

        g->setTextSize(LABEL_TEXT_SIZE);
        g->setTextColor(color, UI_COLOR_BLACK);
        g->setCursor(labelX, labelY);
        g->print(label);
    }
}

void ScreenXYPad::paintFrame(int64_t now)
{
    int cx, cy, cw, ch;
    contentRect(cx, cy, cw, ch);

    if (m_sprite) {
        m_sprite->fillScreen(UI_COLOR_BLACK);
        renderFrame(m_sprite, cx, cy, cw, now);
        // Tab5 can hand the blit to the P4's pixel accelerator; everywhere
        // else (and if it is unavailable) this falls back to pushSprite's
        // per-line memcpy. UIManager::update() calls M5.Lcd.display() once
        // after every screen's work for this tick, so this does not need to.
        if (!ui_ppa::blit(m_sprite, cx, cy)) {
            m_sprite->pushSprite(cx, cy);
        }
        return;
    }

    // Fallback: sprite unavailable, so no ripple/trail -- just the dot and
    // label, incrementally erased and redrawn like the very first version of
    // this screen.
    for (int s = 0; s < UI_XYPAD_MAX_TOUCHES; s++) {
        const xypad_slot_t* slot = ui_xypad_get_slot((uint8_t)s);
        bool show = slot && (slot->active || slot->latched) && m_touchKnown[s];
        if (show) {
            fallbackRefresh(s, m_touchX[s], m_touchY[s]);
        } else {
            fallbackEraseSlot(s);
        }
    }
}

void ScreenXYPad::fallbackEraseSlot(int index)
{
    DrawnSlot& d = m_drawnFallback[index];
    if (!d.shown) return;
    M5.Lcd.fillRect(d.eraseX, d.eraseY, d.eraseW, d.eraseH, UI_COLOR_BLACK);
    d.shown = false;
}

void ScreenXYPad::fallbackPaintSlot(int index, int x, int y)
{
    const xypad_slot_t* slot = ui_xypad_get_slot((uint8_t)index);
    if (!slot) return;

    DrawnSlot& d = m_drawnFallback[index];
    uint16_t color = slotColor(index);

    char label[LABEL_MAX_CHARS + 1];
    if (slot->touch_x_mode == XYPAD_XMODE_CC) {
        snprintf(label, sizeof(label), "X:%d", (int)slot->x);
    } else {
        noteName(slot->note, label, sizeof(label));
    }
    int textW = (int)strlen(label) * GLYPH_W * LABEL_TEXT_SIZE;

    int cx, cy, cw, ch;
    contentRect(cx, cy, cw, ch);
    int labelX = x + DOT_RADIUS + 4;
    if (labelX + textW > cx + cw) {
        labelX = x - DOT_RADIUS - 4 - textW;
    }
    if (labelX < cx) labelX = cx;
    int labelY = y - (GLYPH_H * LABEL_TEXT_SIZE) / 2;

    if (slot->latched) {
        M5.Lcd.drawCircle(x, y, DOT_RADIUS, color);
    } else {
        M5.Lcd.fillCircle(x, y, DOT_RADIUS, color);
    }
    M5.Lcd.setTextSize(LABEL_TEXT_SIZE);
    M5.Lcd.setTextColor(color, UI_COLOR_BLACK);
    M5.Lcd.setCursor(labelX, labelY);
    M5.Lcd.print(label);

    // Bounding box covers the dot and wherever the label ended up, whichever
    // side it is on.
    int eraseLeft = (labelX < x - DOT_RADIUS) ? labelX : (x - DOT_RADIUS);
    int eraseRight = (labelX + textW > x + DOT_RADIUS) ? (labelX + textW) : (x + DOT_RADIUS);
    d.shown = true;
    d.x = x;
    d.y = y;
    d.eraseX = eraseLeft - 1;
    d.eraseY = y - DOT_RADIUS - 1;
    d.eraseW = (eraseRight - eraseLeft) + 2;
    d.eraseH = DOT_RADIUS * 2 + 2;
}

void ScreenXYPad::fallbackRefresh(int index, int x, int y)
{
    fallbackEraseSlot(index);
    fallbackPaintSlot(index, x, y);
}

// --- Screen lifecycle --------------------------------------------------

void ScreenXYPad::enter()
{
    m_isActive = true;
    allocateSprite();
    ui_clear_content_area();
    memset(m_drawnFallback, 0, sizeof(m_drawnFallback));
    // Force one paint even if nothing is animating, e.g. a slot latched via
    // Hold from a previous visit should still show its dot immediately.
    m_hadContent = true;
    paintFrame(esp_timer_get_time());
}

void ScreenXYPad::leave()
{
    m_isActive = false;
    // Sprite kept -- re-entering this screen is common and the allocation is
    // the expensive part (same reasoning as Tombola). Trails/ripples simply
    // stop being painted; touches cannot arrive here while another screen is
    // active, so there is nothing left to animate anyway.
}

void ScreenXYPad::update()
{
    if (!m_isActive) return;

    int64_t now = esp_timer_get_time();
    if (now - m_lastDrawUs < DRAW_INTERVAL_US) return;

    pruneTrails(now);
    bool anything = hasAnythingToDraw();
    // Keep painting through the frame everything finishes fading on (so the
    // screen actually clears), then go idle for free once nothing changes.
    if (!anything && !m_hadContent) return;

    m_lastDrawUs = now;
    m_hadContent = anything;
    paintFrame(now);
}

void ScreenXYPad::draw()
{
    ui_clear_content_area();
    memset(m_drawnFallback, 0, sizeof(m_drawnFallback));
    m_hadContent = true;
    paintFrame(esp_timer_get_time());
}

void ScreenXYPad::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive) return;

    int cx, cy, cw, ch;
    contentRect(cx, cy, cw, ch);

    if (pressed) {
        int idx = ui_xypad_touch_down(touchId, x, y, cx, cy, cw, ch);
        if (idx < 0) return;

        m_touchKnown[idx] = true;
        m_touchX[idx] = x;
        m_touchY[idx] = y;

        int64_t now = esp_timer_get_time();
        pushTrailPoint(idx, x, y, now);
        spawnRipple(x, y, slotColor(idx), now);

        // Paint immediately rather than waiting for the next throttled tick,
        // so the ripple starts the instant the finger lands.
        m_lastDrawUs = now;
        m_hadContent = true;
        paintFrame(now);
    } else {
        // Position/trail/ripple are left as they are -- the fade continues
        // via update(), which is also what will erase the dot once neither
        // active nor latched is true anymore.
        ui_xypad_touch_up(touchId);
    }
}

void ScreenXYPad::onTouchMove(int touchId, int x, int y)
{
    if (!m_isActive) return;

    int cx, cy, cw, ch;
    contentRect(cx, cy, cw, ch);

    int idx = ui_xypad_touch_move(touchId, x, y, cx, cy, cw, ch);
    if (idx < 0) return;

    m_touchKnown[idx] = true;
    m_touchX[idx] = x;
    m_touchY[idx] = y;
    pushTrailPoint(idx, x, y, esp_timer_get_time());
    // No immediate paint here: update()'s throttle absorbs the steady stream
    // of move events, which is what keeps a fast drag from flooding repaints.
}

const char* ScreenXYPad::getTitle()
{
    return "XYPad";
}
