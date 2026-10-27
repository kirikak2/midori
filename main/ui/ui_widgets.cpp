#include "ui_widgets.h"
#include <cstring>
#include <cstdio>
#include <cmath>

// --- Pads -------------------------------------------------------------------

void ui_pad_paint(LovyanGFX* g, int ox, int oy, int x, int y, int w, int h,
                  const pad_config_t* pad, const char* label, int textSize)
{
    x -= ox;
    y -= oy;

    // Determine colors
    uint16_t bgColor;
    uint16_t textColor = UI_COLOR_WHITE;
    uint16_t borderColor = UI_COLOR_WHITE;
    bool assigned = pad && pad->assigned;

    if (!assigned) {
        // Unassigned pad - gray
        bgColor = UI_COLOR_DARKGRAY;
        textColor = UI_COLOR_GRAY;
    } else {
        bgColor = pad->color;

        // Brighten while pressed (trigger / momentary) or toggled on
        if (pad->state) {
            bgColor = ui_lighten_color(bgColor);
            borderColor = UI_COLOR_YELLOW;
        }
    }

    // Draw pad background
    g->fillRoundRect(x, y, w, h, 6, bgColor);
    g->drawRoundRect(x, y, w, h, 6, borderColor);

    // Draw label
    g->setTextSize(textSize);
    g->setTextColor(textColor, bgColor);

    int textW = (int)strlen(label) * UI_GLYPH_W * textSize;
    int textH = UI_GLYPH_H * textSize;
    g->setCursor(x + (w - textW) / 2, y + (h - textH) / 2);
    g->print(label);

    if (!assigned) return;

    // Show type indicator in corner for assigned pads
    const char* typeStr = nullptr;
    switch (pad->type) {
        case PAD_TYPE_TRIGGER:   typeStr = "T"; break;
        case PAD_TYPE_MOMENTARY: typeStr = "M"; break;
        case PAD_TYPE_TOGGLE:    typeStr = pad->state ? "ON" : "OFF"; break;
    }
    if (typeStr) {
        g->setTextColor(UI_COLOR_GRAY, bgColor);
        g->setCursor(x + 4 * textSize, y + 4 * textSize);
        g->print(typeStr);
    }
}

int ui_pad_text_size_for(int w, int h, const char* label)
{
    int len = (int)strlen(label);
    if (len < 1) len = 1;
    int size = 3;
    // The label fits across with a margin, and is at most a third of the
    // height so the corner indicator still has room above it.
    while (size > 1 && (len * UI_GLYPH_W * size > w - 8 || UI_GLYPH_H * size * 3 > h)) {
        size--;
    }
    return size;
}

// --- Knobs ------------------------------------------------------------------

// Arcs thinner than this are not worth a draw call, and fillArc would round
// them away to nothing anyway.
static constexpr float MIN_ARC_DEG = 0.05f;

// Widest value the number field reserves. The field has to stay inside the
// ring's inner circle, which is what caps it at five.
static constexpr int VALUE_MAX_CHARS = 5;

// Below this fraction of the ring radius the finger's offset stops being
// divided by its true distance.
static constexpr float DRAG_MIN_RADIUS = 0.25f;

KnobGeom ui_knob_geom_for(int x, int y, int w, int h)
{
    KnobGeom k;
    k.x = x;
    k.y = y;
    k.w = w;
    k.h = h;

    // Text by cell height: the Knobs screen's 180 / 150 / 86px cells use
    // 3 / 2 / 1.
    k.textSize = (h >= 170) ? 3 : (h >= 110) ? 2 : 1;
    int labelH = UI_GLYPH_H * k.textSize;
    k.labelY = h - labelH - 2 * k.textSize;

    // The ring takes the space above the label, centred in it
    int ringSpace = k.labelY - 4;
    int r = ringSpace / 2;
    if (r > w / 2 - 4) r = w / 2 - 4;
    if (r < 6) r = 6;
    k.rOut = r;
    k.rIn = (r * 77) / 100;   // The Knobs screen's inner/outer ratio
    k.cx = x + w / 2;
    k.cy = y + 2 + ringSpace / 2;

    // The value is printed inside the ring; drop a size until five
    // characters fit across the inner circle.
    while (k.textSize > 1 &&
           VALUE_MAX_CHARS * UI_GLYPH_W * k.textSize > k.rIn * 2) {
        k.textSize--;
    }
    return k;
}

float ui_knob_value_angle(const knob_config_t* k, float value)
{
    float span = k->max - k->min;
    if (span == 0.0f) return UI_KNOB_ANGLE_START;
    float t = (value - k->min) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return UI_KNOB_ANGLE_START + UI_KNOB_ANGLE_SWEEP * t;
}

float ui_knob_origin_angle(const knob_config_t* k)
{
    return (k->origin == KNOB_ORIGIN_CENTER)
        ? (UI_KNOB_ANGLE_START + UI_KNOB_ANGLE_SWEEP * 0.5f)
        : UI_KNOB_ANGLE_START;
}

static void paintSpan(LovyanGFX* g, int cx, int cy, const KnobGeom& geom,
                      float from, float to, uint16_t color)
{
    if (to - from < MIN_ARC_DEG) return;
    g->fillArc(cx, cy, geom.rIn, geom.rOut, from, to, color);
}

void ui_knob_paint(LovyanGFX* g, int ox, int oy, const KnobGeom& geom,
                   const knob_config_t* k, const char* label, bool grabbed)
{
    int x = geom.x - ox;
    int y = geom.y - oy;
    int cx = geom.cx - ox;
    int cy = geom.cy - oy;
    const float angleEnd = UI_KNOB_ANGLE_START + UI_KNOB_ANGLE_SWEEP;
    bool assigned = (k && k->assigned);

    g->fillRect(x, y, geom.w, geom.h, UI_COLOR_BLACK);

    if (!assigned) {
        paintSpan(g, cx, cy, geom, UI_KNOB_ANGLE_START, angleEnd, UI_COLOR_DARKGRAY);
    } else {
        float aOrigin = ui_knob_origin_angle(k);
        float aValue = ui_knob_value_angle(k, k->value);
        float lo = (aOrigin < aValue) ? aOrigin : aValue;
        float hi = (aOrigin < aValue) ? aValue : aOrigin;

        paintSpan(g, cx, cy, geom, UI_KNOB_ANGLE_START, lo, UI_COLOR_DARKGRAY);
        paintSpan(g, cx, cy, geom, lo, hi, k->color);
        paintSpan(g, cx, cy, geom, hi, angleEnd, UI_COLOR_DARKGRAY);

        // Value, centred in the ring
        char buf[16];
        if (k->step >= 1.0f) {
            snprintf(buf, sizeof(buf), "%d", (int)lroundf(k->value));
        } else {
            snprintf(buf, sizeof(buf), "%.1f", k->value);
        }
        buf[VALUE_MAX_CHARS] = '\0';

        int th = UI_GLYPH_H * geom.textSize;
        int fieldW = VALUE_MAX_CHARS * UI_GLYPH_W * geom.textSize;
        int textW = (int)strlen(buf) * UI_GLYPH_W * geom.textSize;
        g->fillRect(cx - fieldW / 2, cy - th / 2, fieldW, th, UI_COLOR_BLACK);
        g->setTextSize(geom.textSize);
        g->setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
        g->setCursor(cx - textW / 2, cy - th / 2);
        g->print(buf);
    }

    // Label, clipped to the cell
    char text[24];
    snprintf(text, sizeof(text), "%s", label);
    int maxChars = geom.w / (UI_GLYPH_W * geom.textSize);
    if (maxChars > 0 && (int)strlen(text) > maxChars) text[maxChars] = '\0';

    int labelW = (int)strlen(text) * UI_GLYPH_W * geom.textSize;
    g->setTextSize(geom.textSize);
    g->setTextColor(assigned ? UI_COLOR_WHITE : UI_COLOR_GRAY, UI_COLOR_BLACK);
    g->setCursor(x + (geom.w - labelW) / 2, y + geom.labelY);
    g->print(text);

    if (grabbed) {
        g->drawCircle(cx, cy, geom.rOut + 2, UI_COLOR_YELLOW);
    }
}

float ui_knob_drag_angle(const KnobGeom& geom, int x, int y, int dx, int dy)
{
    float px = (float)(x - geom.cx);
    float py = (float)(y - geom.cy);
    float r2 = px * px + py * py;
    float minR = geom.rOut * DRAG_MIN_RADIUS;
    float minR2 = minR * minR;
    if (r2 < minR2) r2 = minR2;
    return (px * (float)dy - py * (float)dx) / r2;
}

// --- Envelopes --------------------------------------------------------------

// The sustain segment's share of a full A / D / R segment
static constexpr float ENV_HOLD_UNITS = 0.5f;
static constexpr float ENV_PLOT_UNITS = 3.0f + ENV_HOLD_UNITS;
static constexpr int ENV_MARGIN = 4;

EnvGeom ui_env_geom_for(int x, int y, int w, int h)
{
    EnvGeom e;
    e.x = x;
    e.y = y;
    e.w = w;
    e.h = h;
    e.textSize = (h >= 240 && w >= 320) ? 2 : 1;

    int side = (w < h) ? w : h;
    e.handleR = side / 24;
    if (e.handleR < 4) e.handleR = 4;
    if (e.handleR > 12) e.handleR = 12;

    int textH = UI_GLYPH_H * e.textSize;
    e.labelY = ENV_MARGIN;
    int top = e.labelY + textH + 2;

    // The values row only when the plot keeps a usable height without it
    e.valuesY = h - ENV_MARGIN - textH;
    int bottom = e.valuesY - 3;
    if (bottom - top - 2 * e.handleR < 32) {
        e.valuesY = -1;
        bottom = h - ENV_MARGIN;
    }

    e.px = x + ENV_MARGIN + e.handleR;
    e.pw = w - 2 * (ENV_MARGIN + e.handleR);
    e.py = y + top + e.handleR;
    e.ph = (bottom - top) - 2 * e.handleR;
    if (e.pw < 1) e.pw = 1;
    if (e.ph < 1) e.ph = 1;
    e.unit = (float)e.pw / ENV_PLOT_UNITS;
    e.hold = e.unit * ENV_HOLD_UNITS;
    return e;
}

float ui_env_time_to_norm(float ms, float maxTime)
{
    if (ms <= 0.0f || maxTime <= 0.0f) return 0.0f;
    float n = logf(1.0f + ms) / logf(1.0f + maxTime);
    return (n > 1.0f) ? 1.0f : n;
}

float ui_env_norm_to_time(float n, float maxTime)
{
    if (n <= 0.0f) return 0.0f;
    if (n >= 1.0f) return maxTime;
    return expf(n * logf(1.0f + maxTime)) - 1.0f;
}

void ui_env_points(const EnvGeom& geom, const envelope_config_t* e,
                   float xs[5], float ys[5])
{
    float base = (float)(geom.py + geom.ph);
    float sus = base - e->adsr[UI_ENV_SUSTAIN] * (float)geom.ph;

    xs[0] = (float)geom.px;
    ys[0] = base;
    xs[1] = xs[0] + geom.unit * ui_env_time_to_norm(e->adsr[UI_ENV_ATTACK], e->max_time);
    ys[1] = (float)geom.py;
    xs[2] = xs[1] + geom.unit * ui_env_time_to_norm(e->adsr[UI_ENV_DECAY], e->max_time);
    ys[2] = sus;
    xs[3] = xs[2] + geom.hold;
    ys[3] = sus;
    xs[4] = xs[3] + geom.unit * ui_env_time_to_norm(e->adsr[UI_ENV_RELEASE], e->max_time);
    ys[4] = base;
}

int ui_env_nearest_handle(const EnvGeom& geom, const envelope_config_t* e,
                          int x, int y, uint8_t excludeMask)
{
    float xs[5], ys[5];
    ui_env_points(geom, e, xs, ys);
    int best = -1;
    float bestD = 0.0f;
    for (int h = 0; h < 4; h++) {
        if (excludeMask & (1u << h)) continue;
        float dx = xs[h + 1] - (float)x;
        float dy = ys[h + 1] - (float)y;
        float d = dx * dx + dy * dy;
        // Ties go to the later handle: with no attack, no decay and full
        // sustain, A and D coincide, and only D can pull them apart
        // (lowering the sustain). A is reachable again once they part.
        if (best < 0 || d <= bestD) {
            best = h;
            bestD = d;
        }
    }
    return best;
}

static float segment_time(const EnvGeom& geom, const envelope_config_t* e,
                          float from, float to)
{
    return ui_env_norm_to_time((to - from) / geom.unit, e->max_time);
}

bool ui_env_drag(const EnvGeom& geom, envelope_config_t* e, int h, float hx, float hy)
{
    float xs[5], ys[5];
    ui_env_points(geom, e, xs, ys);

    float next[4];
    memcpy(next, e->adsr, sizeof(next));
    float level = (float)(geom.py + geom.ph) - hy;
    level /= (float)geom.ph;

    switch (h) {
        case 0:
            next[UI_ENV_ATTACK] = segment_time(geom, e, xs[0], hx);
            break;
        case 1:
            next[UI_ENV_DECAY] = segment_time(geom, e, xs[1], hx);
            next[UI_ENV_SUSTAIN] = level;
            break;
        case 2:
            next[UI_ENV_SUSTAIN] = level;
            break;
        case 3:
            next[UI_ENV_RELEASE] = segment_time(geom, e, xs[3], hx);
            break;
        default:
            return false;
    }

    bool changed = false;
    for (int i = 0; i < 4; i++) {
        float q = ui_env_quantize(e, i, next[i]);
        if (q != e->adsr[i]) {
            e->adsr[i] = q;
            changed = true;
        }
    }
    return changed;
}

// A quarter of the colour's brightness, for the area under the curve
static uint16_t env_fill_color(uint16_t c)
{
    uint16_t r = ((c >> 11) & 0x1F) / 4;
    uint16_t g = ((c >> 5) & 0x3F) / 4;
    uint16_t b = (c & 0x1F) / 4;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// A line r pixels either side of (x0, y0)-(x1, y1), with round ends. Not
// drawWideLine: LovyanGFX's wedge lines replace the clip rect with their own
// box (clamped to the whole sprite only), clear it when done, and blend the
// edge pixels with no clipping at all. On ui_strip's band canvas, whose rows
// outside the band are not memory of its own, that writes over the heap.
// Triangles and circles keep to the clip rect.
static void thick_line(LovyanGFX* g, int x0, int y0, int x1, int y1, int r, uint16_t color)
{
    if (r <= 0) {
        g->drawLine(x0, y0, x1, y1, color);
        return;
    }
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len >= 0.5f) {
        int nx = (int)lroundf(-dy / len * (float)r);
        int ny = (int)lroundf(dx / len * (float)r);
        g->fillTriangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, color);
        g->fillTriangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, color);
    }
    g->fillCircle(x0, y0, r, color);
    g->fillCircle(x1, y1, r, color);
}

static void format_env_value(char* buf, int len, int stage, float v)
{
    static const char names[4] = { 'A', 'D', 'S', 'R' };
    if (stage == UI_ENV_SUSTAIN) {
        snprintf(buf, len, "%c %d%%", names[stage], (int)lroundf(v * 100.0f));
    } else if (v < 1000.0f) {
        snprintf(buf, len, "%c %dms", names[stage], (int)lroundf(v));
    } else {
        snprintf(buf, len, "%c %.1fs", names[stage], v / 1000.0f);
    }
}

void ui_env_paint(LovyanGFX* g, int ox, int oy, const EnvGeom& geom,
                  const envelope_config_t* e, uint8_t grabbedMask)
{
    int x = geom.x - ox;
    int y = geom.y - oy;
    int ts = geom.textSize;

    g->fillRect(x, y, geom.w, geom.h, UI_COLOR_BLACK);
    g->drawRoundRect(x, y, geom.w, geom.h, 6, UI_COLOR_DARKGRAY);

    // Level 1.0 and 0 guides
    int px = geom.px - ox;
    int top = geom.py - oy;
    int base = geom.py + geom.ph - oy;
    g->drawFastHLine(px, top, geom.pw, UI_COLOR_DARKGRAY);
    g->drawFastHLine(px, base, geom.pw, UI_COLOR_DARKGRAY);

    float fx[5], fy[5];
    ui_env_points(geom, e, fx, fy);
    int xs[5], ys[5];
    for (int i = 0; i < 5; i++) {
        xs[i] = (int)lroundf(fx[i]) - ox;
        ys[i] = (int)lroundf(fy[i]) - oy;
    }

    // Area under the curve, a quad per segment split in two triangles
    uint16_t fill = env_fill_color(e->color);
    for (int i = 0; i < 4; i++) {
        if (xs[i + 1] <= xs[i]) continue;
        g->fillTriangle(xs[i], ys[i], xs[i + 1], ys[i + 1], xs[i + 1], base, fill);
        g->fillTriangle(xs[i], ys[i], xs[i], base, xs[i + 1], base, fill);
    }
    for (int i = 0; i < 4; i++) {
        thick_line(g, xs[i], ys[i], xs[i + 1], ys[i + 1], ts, e->color);
    }

    // Handles, grabbed ones larger with a ring
    for (int h = 0; h < 4; h++) {
        int hx = xs[h + 1];
        int hy = ys[h + 1];
        if (grabbedMask & (1u << h)) {
            int r = geom.handleR * 3 / 2;
            g->fillCircle(hx, hy, r, e->color);
            g->drawCircle(hx, hy, r + 2, UI_COLOR_WHITE);
        } else {
            g->fillCircle(hx, hy, geom.handleR, e->color);
            g->drawCircle(hx, hy, geom.handleR, UI_COLOR_BLACK);
        }
    }

    g->setTextSize(ts);

    // Label, clipped to the cell
    char text[24];
    snprintf(text, sizeof(text), "%s", e->label);
    int maxChars = (geom.w - 2 * ENV_MARGIN) / (UI_GLYPH_W * ts);
    if (maxChars >= 0 && (int)strlen(text) > maxChars) text[maxChars] = '\0';
    g->setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    g->setCursor(x + ENV_MARGIN + 2, y + geom.labelY);
    g->print(text);

    if (geom.valuesY < 0) return;

    // Values row, the stages a grabbed handle moves in white
    static const uint8_t stageHandles[4] = {
        1u << 0,                 // A
        1u << 1,                 // D
        (1u << 1) | (1u << 2),   // S
        1u << 3,                 // R
    };
    int colW = (geom.w - 2 * ENV_MARGIN) / 4;
    int colChars = colW / (UI_GLYPH_W * ts);
    for (int i = 0; i < 4; i++) {
        format_env_value(text, sizeof(text), i, e->adsr[i]);
        if (colChars >= 0 && (int)strlen(text) > colChars) text[colChars] = '\0';
        bool hot = (grabbedMask & stageHandles[i]) != 0;
        g->setTextColor(hot ? UI_COLOR_WHITE : UI_COLOR_GRAY, UI_COLOR_BLACK);
        g->setCursor(x + ENV_MARGIN + 2 + colW * i, y + geom.valuesY);
        g->print(text);
    }
}
