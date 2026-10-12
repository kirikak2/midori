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
