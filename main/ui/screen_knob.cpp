#include "sdkconfig.h"
#include "screen_knob.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "esp_log.h"

// Global instance
static ScreenKnobs s_screenKnobs;

ScreenKnobs& getScreenKnobs()
{
    return s_screenKnobs;
}

// The gauge sweeps 270 degrees with the gap at the bottom, the way a panel knob
// is drawn. LovyanGFX measures arcs from 3 o'clock going clockwise (screen y
// grows downward), so 135 is the lower left and 405 the lower right. Angles
// past 360 are fine: fillEllipseArc takes them modulo 360 and handles the
// wrap-around span itself.
static constexpr float ANGLE_START = 135.0f;
static constexpr float ANGLE_SWEEP = 270.0f;
static constexpr float ANGLE_END   = ANGLE_START + ANGLE_SWEEP;
static constexpr float SWEEP_RAD   = ANGLE_SWEEP * 3.14159265f / 180.0f;

// Below this fraction of the ring radius the finger's offset stops being
// divided by its true distance. Turning goes as 1/r^2, so without a floor a
// finger a few pixels from the centre would slam the knob end to end.
static constexpr float DRAG_MIN_RADIUS = 0.25f;

// Arcs thinner than this are not worth a draw call, and fillArc would round
// them away to nothing anyway.
static constexpr float MIN_ARC_DEG = 0.05f;

// Widest value the number field reserves. The erase rectangle has to stay
// inside the ring's inner circle, which is what caps it at five.
static constexpr int VALUE_MAX_CHARS = 5;

#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
static constexpr int BANK_TEXT_SIZE = 4;
#else
static constexpr int BANK_TEXT_SIZE = 2;
#endif

// Default font cell, in pixels per text size unit
static constexpr int GLYPH_W = 6;
static constexpr int GLYPH_H = 8;

ScreenKnobs::ScreenKnobs()
    : m_isActive(false)
    , m_drawnBank(0)
    , m_drawnBanksInUse(0)
{
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchToKnob[i] = -1;
        m_lastX[i] = 0;
        m_lastY[i] = 0;
        m_accum[i] = 0.0f;
        m_moved[i] = false;
    }
    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        m_drawn[i] = 0.0f;
        m_hasDrawn[i] = false;
    }
    strcpy(m_title, "Knobs");
}

// --- Geometry --------------------------------------------------------------

void ScreenKnobs::cellRect(int index, int& x, int& y, int& w, int& h)
{
    int col = index % UI_KNOB_COLS;
    int row = index / UI_KNOB_COLS;

    w = UI_KNOB_CELL_W;
    h = UI_KNOB_CELL_H;
    x = UI_KNOB_GRID_X + col * (UI_KNOB_CELL_W + UI_KNOB_GAP_X);
    y = UI_KNOB_GRID_Y + row * (UI_KNOB_CELL_H + UI_KNOB_GAP_Y);
}

void ScreenKnobs::ringCenter(int index, int& cx, int& cy)
{
    int x, y, w, h;
    cellRect(index, x, y, w, h);
    cx = x + w / 2;
    cy = y + UI_KNOB_RING_CY;
}

void ScreenKnobs::bankRect(int bank, int& x, int& y, int& w, int& h)
{
    x = UI_KNOB_BANK_X;
    w = UI_KNOB_BANK_W;
    h = UI_KNOB_BANK_H;
    y = UI_CONTENT_Y + bank * (UI_KNOB_BANK_H + UI_KNOB_BANK_GAP);
}

int ScreenKnobs::hitTestBank(int x, int y)
{
    for (int b = 0; b < UI_KNOB_BANKS; b++) {
        int bx, by, bw, bh;
        bankRect(b, bx, by, bw, bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) return b;
    }
    return -1;
}

int ScreenKnobs::hitTestKnob(int x, int y)
{
    // The whole cell, not just the ring: aiming at a 7px band of rim with a
    // fingertip is not a thing anyone should have to do.
    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        int kx, ky, kw, kh;
        cellRect(i, kx, ky, kw, kh);
        if (x >= kx && x < kx + kw && y >= ky && y < ky + kh) return i;
    }
    return -1;
}

float ScreenKnobs::valueAngle(const knob_config_t* k, float value)
{
    float span = k->max - k->min;
    if (span == 0.0f) return ANGLE_START;
    float t = (value - k->min) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return ANGLE_START + ANGLE_SWEEP * t;
}

float ScreenKnobs::originAngle(const knob_config_t* k)
{
    return (k->origin == KNOB_ORIGIN_CENTER) ? (ANGLE_START + ANGLE_SWEEP * 0.5f)
                                             : ANGLE_START;
}

void ScreenKnobs::paintSpan(int cx, int cy, float from, float to, uint16_t color)
{
    if (to - from < MIN_ARC_DEG) return;
    M5.Lcd.fillArc(cx, cy, UI_KNOB_R_IN, UI_KNOB_R_OUT, from, to, color);
}

// --- Drawing ---------------------------------------------------------------

void ScreenKnobs::drawValueText(int index, const knob_config_t* k)
{
    int cx, cy;
    ringCenter(index, cx, cy);

    char buf[16];
    if (k->step >= 1.0f) {
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(k->value));
    } else {
        snprintf(buf, sizeof(buf), "%.1f", k->value);
    }
    buf[VALUE_MAX_CHARS] = '\0';

    int th = GLYPH_H * UI_KNOB_TEXT_SIZE;
    int fieldW = VALUE_MAX_CHARS * GLYPH_W * UI_KNOB_TEXT_SIZE;
    int textW = (int)strlen(buf) * GLYPH_W * UI_KNOB_TEXT_SIZE;

    // Clear the whole field rather than relying on the text's own background:
    // the string is centred, so a shorter one would leave the old ends behind.
    M5.Lcd.fillRect(cx - fieldW / 2, cy - th / 2, fieldW, th, UI_COLOR_BLACK);
    M5.Lcd.setTextSize(UI_KNOB_TEXT_SIZE);
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_BLACK);
    M5.Lcd.setCursor(cx - textW / 2, cy - th / 2);
    M5.Lcd.print(buf);
}

void ScreenKnobs::drawKnob(int index, bool grabbed)
{
    if (index < 0 || index >= UI_KNOB_COUNT) return;

    int x, y, w, h;
    cellRect(index, x, y, w, h);
    int cx, cy;
    ringCenter(index, cx, cy);

    const knob_config_t* k = ui_knob_get_config(ui_knob_get_bank(), index);
    bool assigned = (k && k->assigned);

    M5.Lcd.fillRect(x, y, w, h, UI_COLOR_BLACK);

    if (!assigned) {
        paintSpan(cx, cy, ANGLE_START, ANGLE_END, UI_COLOR_DARKGRAY);
    } else {
        float aOrigin = originAngle(k);
        float aValue = valueAngle(k, k->value);
        float lo = (aOrigin < aValue) ? aOrigin : aValue;
        float hi = (aOrigin < aValue) ? aValue : aOrigin;

        paintSpan(cx, cy, ANGLE_START, lo, UI_COLOR_DARKGRAY);
        paintSpan(cx, cy, lo, hi, k->color);
        paintSpan(cx, cy, hi, ANGLE_END, UI_COLOR_DARKGRAY);

        drawValueText(index, k);
    }

    // Label, clipped to the cell
    char label[24];
    if (assigned && k->label[0] != '\0') {
        snprintf(label, sizeof(label), "%s", k->label);
    } else {
        snprintf(label, sizeof(label), "Knob %d", index + 1);
    }
    int maxChars = w / (GLYPH_W * UI_KNOB_TEXT_SIZE);
    if (maxChars > 0 && (int)strlen(label) > maxChars) label[maxChars] = '\0';

    int labelW = (int)strlen(label) * GLYPH_W * UI_KNOB_TEXT_SIZE;
    M5.Lcd.setTextSize(UI_KNOB_TEXT_SIZE);
    M5.Lcd.setTextColor(assigned ? UI_COLOR_WHITE : UI_COLOR_GRAY, UI_COLOR_BLACK);
    M5.Lcd.setCursor(x + (w - labelW) / 2, y + UI_KNOB_LABEL_Y);
    M5.Lcd.print(label);

    if (grabbed) drawGrabRing(index, true);

    m_drawn[index] = assigned ? k->value : 0.0f;
    m_hasDrawn[index] = true;
}

// Grabbing a knob only adds a ring outside the gauge, on background that is
// nothing but black -- so it can be put on and taken off on its own. Repainting
// the whole cell for this would throw away and redraw the gauge on every touch,
// which is both wasteful and visible as a flash.
void ScreenKnobs::drawGrabRing(int index, bool on)
{
    int cx, cy;
    ringCenter(index, cx, cy);
    M5.Lcd.drawCircle(cx, cy, UI_KNOB_R_OUT + 2,
                      on ? (uint16_t)UI_COLOR_YELLOW : (uint16_t)UI_COLOR_BLACK);
}

void ScreenKnobs::drawKnobValue(int index)
{
    if (index < 0 || index >= UI_KNOB_COUNT) return;

    const knob_config_t* k = ui_knob_get_config(ui_knob_get_bank(), index);
    if (!k || !k->assigned) {
        drawKnob(index, isGrabbed(index));
        return;
    }
    if (!m_hasDrawn[index]) {
        drawKnob(index, isGrabbed(index));
        return;
    }

    int cx, cy;
    ringCenter(index, cx, cy);

    // Only the wedge between the old and new values changes. Both bands start
    // at the origin angle, so their difference is a single arc: colour it in
    // when the value moved away from the origin, wipe it back to the track
    // colour when it moved toward it, and do both when it crossed over.
    float aOrigin = originAngle(k);
    float aOld = valueAngle(k, m_drawn[index]);
    float aNew = valueAngle(k, k->value);

    if ((aOld - aOrigin) * (aNew - aOrigin) < 0.0f) {
        float oldLo = (aOld < aOrigin) ? aOld : aOrigin;
        float oldHi = (aOld < aOrigin) ? aOrigin : aOld;
        float newLo = (aNew < aOrigin) ? aNew : aOrigin;
        float newHi = (aNew < aOrigin) ? aOrigin : aNew;
        paintSpan(cx, cy, oldLo, oldHi, UI_COLOR_DARKGRAY);
        paintSpan(cx, cy, newLo, newHi, k->color);
    } else {
        float lo = (aOld < aNew) ? aOld : aNew;
        float hi = (aOld < aNew) ? aNew : aOld;
        bool grew = fabsf(aNew - aOrigin) > fabsf(aOld - aOrigin);
        paintSpan(cx, cy, lo, hi, grew ? k->color : (uint16_t)UI_COLOR_DARKGRAY);
    }

    drawValueText(index, k);
    m_drawn[index] = k->value;
}

void ScreenKnobs::drawBankStrip()
{
    uint8_t active = ui_knob_get_bank();

    for (int b = 0; b < UI_KNOB_BANKS; b++) {
        int x, y, w, h;
        bankRect(b, x, y, w, h);

        bool inUse = ui_knob_bank_in_use(b);
        uint16_t bg = (b == active) ? UI_COLOR_WHITE : UI_COLOR_BLACK;
        uint16_t fg;
        if (b == active)   fg = UI_COLOR_BLACK;
        else if (inUse)    fg = UI_COLOR_WHITE;
        else               fg = UI_COLOR_DARKGRAY;

        M5.Lcd.fillRoundRect(x, y, w, h, 4, bg);
        M5.Lcd.drawRoundRect(x, y, w, h, 4, UI_COLOR_GRAY);

        char letter[2] = { (char)('A' + b), '\0' };
        M5.Lcd.setTextSize(BANK_TEXT_SIZE);
        M5.Lcd.setTextColor(fg, bg);
        M5.Lcd.setCursor(x + (w - GLYPH_W * BANK_TEXT_SIZE) / 2,
                         y + (h - GLYPH_H * BANK_TEXT_SIZE) / 2);
        M5.Lcd.print(letter);
    }

    m_drawnBanksInUse = banksInUseMask();
}

uint8_t ScreenKnobs::banksInUseMask()
{
    uint8_t mask = 0;
    for (int b = 0; b < UI_KNOB_BANKS; b++) {
        if (ui_knob_bank_in_use(b)) mask |= (1u << b);
    }
    return mask;
}

bool ScreenKnobs::isGrabbed(int index)
{
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (m_touchToKnob[i] == index) return true;
    }
    return false;
}

void ScreenKnobs::drawAll()
{
    m_drawnBank = ui_knob_get_bank();
    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        m_hasDrawn[i] = false;
        drawKnob(i, isGrabbed(i));
    }
    drawBankStrip();
    // Everything was just painted, so any pending per-knob requests are stale.
    ui_knob_take_dirty();
    ui_knob_take_repaint();
}

// --- Screen lifecycle ------------------------------------------------------

void ScreenKnobs::enter()
{
    m_isActive = true;
    releaseAllTouches();
    ui_clear_content_area();
    drawAll();
}

void ScreenKnobs::leave()
{
    m_isActive = false;
    releaseAllTouches();
}

void ScreenKnobs::releaseAllTouches()
{
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchToKnob[i] = -1;
        m_moved[i] = false;
    }
}

void ScreenKnobs::update()
{
    if (!m_isActive) return;

    // A script can switch banks from the PicoRuby task; the repaint has to
    // happen here, where the LCD may be touched.
    if (m_drawnBank != ui_knob_get_bank()) {
        ui_clear_content_area();
        drawAll();
        return;
    }

    uint32_t repaint = ui_knob_take_repaint();
    uint32_t dirty = ui_knob_take_dirty() & ~repaint;

    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        if (repaint & (1u << i)) drawKnob(i, isGrabbed(i));
    }
    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        if (dirty & (1u << i)) drawKnobValue(i);
    }

    // Assigning a knob in a bank that is not on screen changes nothing in the
    // grid but does light up its letter in the strip.
    if (m_drawnBanksInUse != banksInUseMask()) {
        drawBankStrip();
    }
}

void ScreenKnobs::draw()
{
    drawAll();
}

// --- Touch -----------------------------------------------------------------

void ScreenKnobs::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;

    if (pressed) {
        int bank = hitTestBank(x, y);
        if (bank >= 0) {
            if (bank != ui_knob_get_bank()) {
                // Whatever the other fingers were turning belongs to the bank
                // that is going away; do not let them keep turning a knob that
                // has been replaced under them.
                releaseAllTouches();
                ui_knob_set_bank((uint8_t)bank);
            }
            return;
        }

        int index = hitTestKnob(x, y);
        if (index < 0) return;

        const knob_config_t* k = ui_knob_get_config(ui_knob_get_bank(), index);
        if (!k || !k->assigned) return;

        // One finger per knob. A second one would add its angular velocity to
        // the first's and make the value lurch.
        for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
            if (i != touchId && m_touchToKnob[i] == index) return;
        }

        m_touchToKnob[touchId] = index;
        m_lastX[touchId] = x;
        m_lastY[touchId] = y;
        m_accum[touchId] = k->value;
        m_moved[touchId] = false;
        drawGrabRing(index, true);
        return;
    }

    int index = m_touchToKnob[touchId];
    m_touchToKnob[touchId] = -1;
    if (index < 0) return;

    if (!isGrabbed(index)) {
        drawGrabRing(index, false);
    }

    // The value that was left behind has to reach the script even if it drops
    // every intermediate one, or the synth stays on a value the knob is not
    // showing. A touch that never moved the value has nothing to announce.
    if (m_moved[touchId]) {
        const knob_config_t* k = ui_knob_get_config(ui_knob_get_bank(), index);
        if (k && k->assigned && k->notify) {
            ui_knob_event_post(ui_knob_get_bank(), (uint8_t)index, k->value, true);
        }
    }
    m_moved[touchId] = false;
}

void ScreenKnobs::onTouchMove(int touchId, int x, int y)
{
    if (!m_isActive || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;

    int index = m_touchToKnob[touchId];
    if (index < 0) return;

    int dx = x - m_lastX[touchId];
    int dy = y - m_lastY[touchId];
    m_lastX[touchId] = x;
    m_lastY[touchId] = y;
    if (dx == 0 && dy == 0) return;

    uint8_t bank = ui_knob_get_bank();
    const knob_config_t* k = ui_knob_get_config(bank, index);
    if (!k || !k->assigned) return;

    int cx, cy;
    ringCenter(index, cx, cy);

    // The knob turns by exactly the angle the finger sweeps around its centre,
    // so the end of the gauge stays under the fingertip. This is
    // d(atan2(py,px)) written out -- the cross product of the offset with the
    // movement, over the distance squared -- which is why it holds at any
    // distance and along any path rather than approximately.
    //
    // Grabbing further out therefore covers the same angle in more pixels,
    // which is the fine-adjustment end of the gesture.
    float px = (float)(x - cx);
    float py = (float)(y - cy);
    float r2 = px * px + py * py;
    float minR = UI_KNOB_R_OUT * DRAG_MIN_RADIUS;
    float minR2 = minR * minR;
    if (r2 < minR2) r2 = minR2;

    float dtheta = (px * (float)dy - py * (float)dx) / r2;

    // Accumulated unquantized, then handed over to be snapped. Adding the
    // quantized value back each time would throw away every movement smaller
    // than one step, and a slow drag is made of nothing else.
    float span = k->max - k->min;
    m_accum[touchId] += span * dtheta / SWEEP_RAD * k->sensitivity;
    if (m_accum[touchId] < k->min) m_accum[touchId] = k->min;
    if (m_accum[touchId] > k->max) m_accum[touchId] = k->max;

    if (ui_knob_set_value(bank, (uint8_t)index, m_accum[touchId], true)) {
        m_moved[touchId] = true;
    }
}

void ScreenKnobs::onNavCenter()
{
    // Re-announce every knob so the script can push the current values out
    // again -- for a synth that was plugged in after the patch was set up.
    ui_knob_notify_all(ui_knob_get_bank());
}

const char* ScreenKnobs::getTitle()
{
    snprintf(m_title, sizeof(m_title), "Knobs %c", 'A' + ui_knob_get_bank());
    return m_title;
}

const char* ScreenKnobs::getNavCenterLabel()
{
    return "Send";
}
