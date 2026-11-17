// Timer screen (docs/TIMER.md): a countdown that MIDI Start starts and that
// ends with MIDI Stop, laid out inside its area so a layout can show it in a
// cell.
//
//        01:30                  <- the time left: white waiting, green counting,
//                                  orange the last 10 s, red at 0, grey unarmed
//   Set 01:30  Armed
//   [-1m][-10s][-1s][+1s][+10s][+1m]  <- the time set (not while counting)
//   [ > Play ][ Reset ][ Arm ]  <- Play / Stop is the tempo's (MIDI Start / Stop)
//
// The countdown is ui_timer.cpp; nothing here keeps time.

#include "sdkconfig.h"
#include "screen_timer.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>

static ScreenTimer s_screenTimer;

ScreenTimer& getScreenTimer()
{
    return s_screenTimer;
}

#if defined(UI_LAYOUT_LARGE)
static constexpr int TIME_TEXT_MAX = 24;
static constexpr int ADJ_BTN_W_MAX = 160;
static constexpr int ADJ_BTN_H_MAX = 64;
static constexpr int CTRL_BTN_W_MAX = 240;
static constexpr int CTRL_BTN_H_MAX = 72;
static constexpr int GAP = 12;
#else
static constexpr int TIME_TEXT_MAX = 9;
static constexpr int ADJ_BTN_W_MAX = 70;
static constexpr int ADJ_BTN_H_MAX = 30;
static constexpr int CTRL_BTN_W_MAX = 100;
static constexpr int CTRL_BTN_H_MAX = 32;
static constexpr int GAP = 4;
#endif

// The last seconds are shown in orange
static constexpr int WARN_SECONDS = 10;

static constexpr int ADJ_COUNT = 6;
static const int ADJ_STEP[ADJ_COUNT] = { -60, -10, -1, 1, 10, 60 };
static const char* const ADJ_LABEL[ADJ_COUNT] = { "-1m", "-10s", "-1s", "+1s", "+10s", "+1m" };

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void format_time(char* buf, int len, int seconds)
{
    // 0..99:59; the bounds also tell the compiler "mm:ss" fits in 6 bytes
    if (seconds < 0) seconds = 0;
    if (seconds > UI_TIMER_MAX_SECONDS) seconds = UI_TIMER_MAX_SECONDS;
    snprintf(buf, len, "%02d:%02d", (seconds / 60) % 100, seconds % 60);
}

ScreenTimer::ScreenTimer()
    : m_areaX(0), m_areaY(UI_CONTENT_Y), m_areaW(UI_SCREEN_WIDTH), m_areaH(UI_CONTENT_HEIGHT)
    , m_isActive(false)
    , m_needsRedraw(false)
    , m_timeRect{0, 0, 0, 0}
    , m_timeTextSize(1)
    , m_infoY(0)
    , m_infoTextSize(1)
    , m_buttonTextSize(1)
    , m_lastSerial(0)
    , m_lastPlaying(false)
    , m_pressed(-1)
    , m_pressedTouch(-1)
{
    layoutView();
}

void ScreenTimer::setArea(int x, int y, int w, int h)
{
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;
    layoutView();
}

void ScreenTimer::resetArea()
{
    setArea(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT);
}

// Rows from the top: the time left (about 42%), the info line, the buttons
// for the time set, then Play / Reset / Arm
void ScreenTimer::layoutView()
{
    int ax = m_areaX, ay = m_areaY, aw = m_areaW, ah = m_areaH;
    int pad = clampi(ah / 30, 2, GAP);

    int timeH = ah * 42 / 100;
    int infoH = ah * 12 / 100;
    int adjH = ah * 22 / 100;
    int ctrlH = ah - timeH - infoH - adjH - 2 * pad;

    // "00:00" at size s is 30s wide and 8s tall
    m_timeTextSize = clampi(timeH / 8, 1, TIME_TEXT_MAX);
    m_timeTextSize = clampi((aw - 2 * pad) / 30, 1, m_timeTextSize);
    m_timeRect = { ax + pad, ay + pad, aw - 2 * pad, timeH };

    m_infoTextSize = (infoH >= 24) ? 2 : 1;
    m_infoY = ay + pad + timeH + (infoH - 8 * m_infoTextSize) / 2;

    // -1m / -10s / -1s / +1s / +10s / +1m
    int aw4 = clampi((aw - (ADJ_COUNT + 1) * pad) / ADJ_COUNT, 1, ADJ_BTN_W_MAX);
    int ah4 = clampi(adjH - pad, 1, ADJ_BTN_H_MAX);
    int adjY = ay + pad + timeH + infoH + (adjH - ah4) / 2;
    int adjX = ax + (aw - (ADJ_COUNT * aw4 + (ADJ_COUNT - 1) * pad)) / 2;
    for (int i = 0; i < ADJ_COUNT; i++) {
        m_buttons[BTN_MINUS_MIN + i] = { adjX + (aw4 + pad) * i, adjY, aw4, ah4 };
    }

    // Play / Reset / Arm
    int cw = clampi((aw - 4 * pad) / 3, 1, CTRL_BTN_W_MAX);
    int ch = clampi(ctrlH - pad, 1, CTRL_BTN_H_MAX);
    int ctrlY = ay + pad + timeH + infoH + adjH + (ctrlH - ch) / 2;
    int ctrlX = ax + (aw - (3 * cw + 2 * pad)) / 2;
    m_buttons[BTN_PLAY]  = { ctrlX, ctrlY, cw, ch };
    m_buttons[BTN_RESET] = { ctrlX + cw + pad, ctrlY, cw, ch };
    m_buttons[BTN_ARM]   = { ctrlX + 2 * (cw + pad), ctrlY, cw, ch };

    // Text size 2 where the narrowest label ("Armed", "+10s") fits
    int minW = aw4 < cw ? aw4 : cw;
    int minH = ah4 < ch ? ah4 : ch;
    m_buttonTextSize = (minH >= 36 && minW >= 6 * 2 * 5 + 8) ? 2 : 1;
}

void ScreenTimer::enter()
{
    m_isActive = true;
    m_pressed = -1;
    m_pressedTouch = -1;
}

void ScreenTimer::leave()
{
    m_isActive = false;
    m_pressed = -1;
    m_pressedTouch = -1;
}

bool ScreenTimer::adjustable() const
{
    return ui_timer_state() != UI_TIMER_RUNNING;
}

void ScreenTimer::update()
{
    if (!m_isActive) return;
    uint32_t serial = ui_timer_serial();
    bool playing = ui_tempo_running();
    if (!m_needsRedraw && serial == m_lastSerial && playing == m_lastPlaying) return;
    m_needsRedraw = false;
    paint();
}

void ScreenTimer::draw()
{
    // Called once per band of a compose: paint the same picture every time
    paint();
}

void ScreenTimer::paint()
{
    m_lastSerial = ui_timer_serial();
    m_lastPlaying = ui_tempo_running();

    auto draw = [this](LovyanGFX* g, int ox, int oy) { render(g, ox, oy); };
    if (!ui_strip::paint(m_areaX, m_areaY, m_areaW, m_areaH, UI_COLOR_BLACK, draw)) {
        ui_gfx().fillRect(m_areaX, m_areaY, m_areaW, m_areaH, UI_COLOR_BLACK);
        render(&ui_gfx(), 0, 0);
    }
}

void ScreenTimer::renderButton(LovyanGFX* g, int ox, int oy, int id, const char* label,
                               uint16_t bg, uint16_t fg)
{
    const Rect& r = m_buttons[id];
    if (m_pressed == id) bg = ui_lighten_color(bg);
    int ts = m_buttonTextSize;
    g->fillRoundRect(r.x - ox, r.y - oy, r.w, r.h, 4, bg);
    g->drawRoundRect(r.x - ox, r.y - oy, r.w, r.h, 4, UI_COLOR_WHITE);
    g->setTextSize(ts);
    g->setTextColor(fg, bg);
    g->setCursor(r.x + (r.w - (int)strlen(label) * 6 * ts) / 2 - ox,
                 r.y + (r.h - 8 * ts) / 2 - oy);
    g->print(label);
}

void ScreenTimer::render(LovyanGFX* g, int ox, int oy)
{
    int state = ui_timer_state();
    int remaining = ui_timer_remaining();
    int seconds = ui_timer_seconds();
    bool armed = ui_timer_armed();
    bool playing = ui_tempo_running();
    g->setTextWrap(false, false);

    // ----- The time left -----
    char num[8];
    format_time(num, sizeof(num), remaining);
    uint16_t color;
    switch (state) {
        case UI_TIMER_RUNNING:
            color = remaining <= WARN_SECONDS ? UI_COLOR_ORANGE : UI_COLOR_GREEN;
            break;
        case UI_TIMER_FINISHED: color = UI_COLOR_RED; break;
        case UI_TIMER_STOPPED:  color = UI_COLOR_CYAN; break;
        default:                color = armed ? UI_COLOR_WHITE : UI_COLOR_GRAY; break;
    }
    int s = m_timeTextSize;
    g->setTextSize(s);
    g->setTextColor(color, UI_COLOR_BLACK);
    g->setCursor(m_timeRect.x + (m_timeRect.w - (int)strlen(num) * 6 * s) / 2 - ox,
                 m_timeRect.y + (m_timeRect.h - 8 * s) / 2 - oy);
    g->print(num);

    // ----- Info: the time set and what the timer is doing -----
    const char* what;
    switch (state) {
        case UI_TIMER_RUNNING:  what = "Counting"; break;
        case UI_TIMER_FINISHED: what = "Time up"; break;
        case UI_TIMER_STOPPED:  what = "Stopped"; break;
        default:                what = armed ? "Armed: Play starts" : "Not armed"; break;
    }
    char set[8];
    format_time(set, sizeof(set), seconds);
    char info[48];
    snprintf(info, sizeof(info), "Set %s  %s", set, what);
    int is = m_infoTextSize;
    g->setTextSize(is);
    g->setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    g->setCursor(m_areaX + (m_areaW - (int)strlen(info) * 6 * is) / 2 - ox, m_infoY - oy);
    g->print(info);

    // ----- The time set -----
    bool adj = adjustable();
    for (int i = 0; i < ADJ_COUNT; i++) {
        renderButton(g, ox, oy, BTN_MINUS_MIN + i, ADJ_LABEL[i],
                     adj ? UI_COLOR_NAVY : UI_COLOR_BLACK,
                     adj ? UI_COLOR_WHITE : UI_COLOR_DARKGRAY);
    }

    // ----- Play / Stop (a triangle / a square before the label) -----
    {
        const Rect& r = m_buttons[BTN_PLAY];
        uint16_t bg = playing ? UI_COLOR_GREEN : UI_COLOR_NAVY;
        if (m_pressed == BTN_PLAY) bg = ui_lighten_color(bg);
        uint16_t fg = playing ? UI_COLOR_BLACK : UI_COLOR_WHITE;
        const char* label = playing ? "Stop" : "Play";
        int ts = m_buttonTextSize;
        int x = r.x - ox, y = r.y - oy;
        g->fillRoundRect(x, y, r.w, r.h, 4, bg);
        g->drawRoundRect(x, y, r.w, r.h, 4, UI_COLOR_WHITE);
        int icon = 8 * ts;
        int textW = (int)strlen(label) * 6 * ts;
        int gap = 3 * ts;
        int cx = x + (r.w - (icon + gap + textW)) / 2;
        int cy = y + (r.h - icon) / 2;
        if (playing) {
            g->fillRect(cx, cy, icon, icon, fg);
        } else {
            g->fillTriangle(cx, cy, cx, cy + icon - 1, cx + icon - 1, cy + icon / 2, fg);
        }
        g->setTextSize(ts);
        g->setTextColor(fg, bg);
        g->setCursor(cx + icon + gap, y + (r.h - 8 * ts) / 2);
        g->print(label);
    }

    renderButton(g, ox, oy, BTN_RESET, "Reset", UI_COLOR_DARKGRAY, UI_COLOR_WHITE);
    renderButton(g, ox, oy, BTN_ARM, armed ? "Armed" : "Arm",
                 armed ? UI_COLOR_DARKGREEN : UI_COLOR_NAVY, UI_COLOR_WHITE);
}

void ScreenTimer::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!pressed) {
        if (touchId == m_pressedTouch) {
            m_pressedTouch = -1;
            m_pressed = -1;
            m_needsRedraw = true;
        }
        return;
    }
    for (int i = 0; i < BTN_COUNT; i++) {
        if (m_buttons[i].w > 0 && m_buttons[i].contains(x, y)) {
            if (m_pressedTouch < 0) {
                m_pressedTouch = touchId;
                m_pressed = i;
            }
            handleButtonPress(i);
            m_needsRedraw = true;
            return;
        }
    }
}

void ScreenTimer::onTouchMove(int touchId, int x, int y)
{
    (void)touchId; (void)x; (void)y;
}

void ScreenTimer::handleButtonPress(int buttonId)
{
    switch (buttonId) {
        case BTN_MINUS_MIN:
        case BTN_MINUS_10:
        case BTN_MINUS_1:
        case BTN_PLUS_1:
        case BTN_PLUS_10:
        case BTN_PLUS_MIN:
            if (adjustable()) {
                ui_timer_set_seconds(ui_timer_seconds() + ADJ_STEP[buttonId - BTN_MINUS_MIN]);
            }
            break;
        case BTN_PLAY:
            if (ui_tempo_running()) {
                ui_tempo_set_running(false);
            } else {
                // Play here means "run the timer": arm it if it was not
                if (!ui_timer_armed()) ui_timer_set_armed(true, false);
                ui_tempo_set_running(true);
            }
            break;
        case BTN_RESET:
            ui_timer_reset();
            break;
        case BTN_ARM:
            ui_timer_set_armed(!ui_timer_armed(), false);
            break;
        default:
            break;
    }
}

void ScreenTimer::onNavCenter()
{
    ui_timer_reset();
}

const char* ScreenTimer::getTitle()
{
    return "Timer";
}

const char* ScreenTimer::getNavCenterLabel()
{
    return "Reset";
}

void ScreenTimer::getTileStatus(char* buf, int len)
{
    char t[8];
    format_time(t, sizeof(t), ui_timer_remaining());
    int state = ui_timer_state();
    snprintf(buf, len, "%s%s", t,
             state == UI_TIMER_RUNNING ? " >" : (ui_timer_armed() ? "" : " off"));
}
