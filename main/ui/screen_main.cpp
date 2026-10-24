// Tempo screen (docs/TEMPO.md): the BPM, the external clock source, Sync and
// TAP tempo, laid out inside its area so a layout can show it in a cell.
//
//   [ 120 BPM ]            <- tap: TAP tempo (also the nav bar's TAP)
//                             swipe up / right: faster, down / left: slower
//   Ext: 120.0 BPM
//   [USB-H][USB-D][DIN][BLE]
//   [ > Play ][ Sync ]     <- Play / Stop sends MIDI Start / Stop
//
// Fine changes are a swipe on the BPM (1 BPM a step); the BPM can also be set
// from Ruby (UI::Tempo.bpm, a knob, a pad). The clock goes out through routes
// from the tempo device.

#include "sdkconfig.h"
#include "screen_main.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include "esp_timer.h"

// Global instance
static ScreenMain s_screenMain;

ScreenMain& getScreenMain()
{
    return s_screenMain;
}

// Upper bounds for the parts; the area may make them smaller
#if defined(UI_LAYOUT_LARGE)
static constexpr int BPM_TEXT_MAX = 12;
static constexpr int SOURCE_BTN_W_MAX = 120;
static constexpr int SOURCE_BTN_H_MAX = 56;
static constexpr int SYNC_BTN_W_MAX = 260;
static constexpr int SYNC_BTN_H_MAX = 64;
static constexpr int GAP = 12;
static constexpr int SWIPE_START_PX = 16;   // Further than this from the press: a swipe
static constexpr int SWIPE_STEP_PX = 12;    // 1 BPM
#else
static constexpr int BPM_TEXT_MAX = 6;
static constexpr int SOURCE_BTN_W_MAX = 48;
static constexpr int SOURCE_BTN_H_MAX = 24;
static constexpr int SYNC_BTN_W_MAX = 110;
static constexpr int SYNC_BTN_H_MAX = 30;
static constexpr int GAP = 4;
static constexpr int SWIPE_START_PX = 8;
static constexpr int SWIPE_STEP_PX = 6;
#endif

// Which external clock sources this board's USB mode has
#if CONFIG_USB_MIDI_HOST_ENABLED
static constexpr bool CLOCK_FROM_USB_HOST = true;
#else
static constexpr bool CLOCK_FROM_USB_HOST = false;
#endif
#if CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE
static constexpr bool CLOCK_FROM_USB_DEVICE = true;
#else
static constexpr bool CLOCK_FROM_USB_DEVICE = false;
#endif

// External BPM text redraws at most this often (it changes on every clock)
static constexpr int64_t EXT_REDRAW_US = 100000;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

ScreenMain::ScreenMain()
    : m_areaX(0), m_areaY(UI_CONTENT_Y), m_areaW(UI_SCREEN_WIDTH), m_areaH(UI_CONTENT_HEIGHT)
    , m_isActive(false)
    , m_needsRedraw(false)
    , m_bpmRect{0, 0, 0, 0}
    , m_bpmTextSize(1)
    , m_extY(0)
    , m_extTextSize(1)
    , m_buttonTextSize(1)
    , m_sourceCount(0)
    , m_lastBpm(-1)
    , m_lastExt10(-1)
    , m_lastFlags(0)
    , m_lastExtDrawUs(0)
    , m_swipeTouch(-1)
    , m_swiping(false)
    , m_swipeVertical(true)
    , m_swipeX(0)
    , m_swipeY(0)
    , m_swipeBpm(0)
{
    static const struct { int button; bool available; } order[SOURCE_SLOTS] = {
        { BTN_SOURCE_USB,        CLOCK_FROM_USB_HOST },
        { BTN_SOURCE_USB_DEVICE, CLOCK_FROM_USB_DEVICE },
        { BTN_SOURCE_DIN,        true },
        { BTN_SOURCE_BLE,        true },
    };
    for (int i = 0; i < SOURCE_SLOTS; i++) {
        if (order[i].available) m_sourceButtons[m_sourceCount++] = order[i].button;
    }
    layoutView();
}

void ScreenMain::setArea(int x, int y, int w, int h)
{
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;
    layoutView();
}

void ScreenMain::resetArea()
{
    setArea(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT);
}

// Rows from the top: BPM (about 40%), external BPM, source buttons, Sync. A
// hidden source button keeps an empty rectangle, so no tap hits it.
void ScreenMain::layoutView()
{
    int ax = m_areaX, ay = m_areaY, aw = m_areaW, ah = m_areaH;
    int pad = clampi(ah / 30, 2, GAP);

    int bpmH = ah * 40 / 100;
    int extH = ah * 12 / 100;
    int srcH = ah * 20 / 100;
    int syncH = ah - bpmH - extH - srcH - 2 * pad;

    // "120" at size s is 18s wide, " BPM" at s/3 another ~24s/3: ~26s in all
    m_bpmTextSize = clampi(bpmH / 8, 1, BPM_TEXT_MAX);
    m_bpmTextSize = clampi((aw - 2 * pad) / 26, 1, m_bpmTextSize);
    m_bpmRect = { ax + pad, ay + pad, aw - 2 * pad, bpmH };

    m_extTextSize = (extH >= 24) ? 2 : 1;
    m_extY = ay + pad + bpmH + (extH - 8 * m_extTextSize) / 2;

    for (int i = 0; i < BTN_COUNT; i++) m_buttons[i] = { {0, 0, 0, 0}, "" };

    int n = m_sourceCount > 0 ? m_sourceCount : 1;
    int btnW = clampi((aw - (n + 1) * pad) / n, 1, SOURCE_BTN_W_MAX);
    int btnH = clampi(srcH - pad, 1, SOURCE_BTN_H_MAX);
    m_buttonTextSize = (btnH >= 36 && btnW >= 6 * 2 * 5 + 8) ? 2 : 1;
    int srcY = ay + pad + bpmH + extH + (srcH - btnH) / 2;
    int startX = ax + (aw - (m_sourceCount * btnW + (m_sourceCount - 1) * pad)) / 2;
    for (int i = 0; i < m_sourceCount; i++) {
        Button& b = m_buttons[m_sourceButtons[i]];
        b.r = { startX + (btnW + pad) * i, srcY, btnW, btnH };
        b.label = sourceLabel(m_sourceButtons[i]);
    }

    // Play / Stop and Sync side by side
    int bw = clampi((aw - 3 * pad) / 2, 1, SYNC_BTN_W_MAX);
    int sh = clampi(syncH - pad, 1, SYNC_BTN_H_MAX);
    int syncY = ay + pad + bpmH + extH + srcH + (syncH - sh) / 2;
    int rowX = ax + (aw - (2 * bw + pad)) / 2;
    m_buttons[BTN_PLAY] = { { rowX, syncY, bw, sh }, "Play" };
    m_buttons[BTN_SYNC] = { { rowX + bw + pad, syncY, bw, sh }, "Sync" };
}

void ScreenMain::enter()
{
    m_isActive = true;
    m_lastBpm = -1;
    m_swipeTouch = -1;
    m_swiping = false;
}

void ScreenMain::leave()
{
    m_isActive = false;
    m_swipeTouch = -1;
    m_swiping = false;
}

// What decides colours and labels, besides the numbers
uint32_t ScreenMain::stateFlags() const
{
    UIManager& ui = UIManager::getInstance();
    uint32_t f = 0;
    if (ui.getSyncMode()) f |= 1;
    if (ui_tempo_running()) f |= 2;
    if (ui.getExternalBpm() > 0) f |= 4;
    if (m_swiping) f |= 8;
    f |= (uint32_t)ui.getExternalBpmSourceSelection() << 4;
    for (int i = 0; i < m_sourceCount; i++) {
        if (ui.getExternalBpmBySource(sourceInterface(m_sourceButtons[i])) > 0) f |= 1u << (8 + i);
    }
    return f;
}

void ScreenMain::update()
{
    if (!m_isActive) return;

    UIManager& ui = UIManager::getInstance();
    int64_t now = esp_timer_get_time();
    int bpm = (int)ui.getBpm();
    int ext10 = (int)(ui.getExternalBpm() * 10);
    uint32_t flags = stateFlags();

    bool changed = m_needsRedraw || bpm != m_lastBpm || flags != m_lastFlags;
    if (!changed && ext10 != m_lastExt10 && now - m_lastExtDrawUs >= EXT_REDRAW_US) {
        changed = true;
    }
    if (!changed) return;

    m_needsRedraw = false;
    m_lastExtDrawUs = now;
    paint();
}

void ScreenMain::draw()
{
    // Called once per band of a compose: paint the same picture every time
    paint();
}

void ScreenMain::paint()
{
    UIManager& ui = UIManager::getInstance();
    m_lastBpm = (int)ui.getBpm();
    m_lastExt10 = (int)(ui.getExternalBpm() * 10);
    m_lastFlags = stateFlags();

    auto draw = [this](LovyanGFX* g, int ox, int oy) { render(g, ox, oy); };
    if (!ui_strip::paint(m_areaX, m_areaY, m_areaW, m_areaH, UI_COLOR_BLACK, draw)) {
        ui_gfx().fillRect(m_areaX, m_areaY, m_areaW, m_areaH, UI_COLOR_BLACK);
        render(&ui_gfx(), 0, 0);
    }
}

void ScreenMain::render(LovyanGFX* g, int ox, int oy)
{
    UIManager& ui = UIManager::getInstance();
    float bpm = ui.getBpm();
    float ext = ui.getExternalBpm();
    bool sync = ui.getSyncMode();
    midi_interface_t selected = ui.getExternalBpmSourceSelection();
    g->setTextWrap(false, false);

    // ----- BPM (green while the clock runs between Start and Stop) -----
    char num[8];
    snprintf(num, sizeof(num), "%d", (int)(bpm + 0.5f));
    int s = m_bpmTextSize;
    int ls = s / 3 > 0 ? s / 3 : 1;
    int numW = (int)strlen(num) * 6 * s;
    int totalW = numW + 6 * ls + 3 * 6 * ls;
    int bx = m_bpmRect.x + (m_bpmRect.w - totalW) / 2 - ox;
    int by = m_bpmRect.y + (m_bpmRect.h - 8 * s) / 2 - oy;
    uint16_t bpmColor = m_swiping ? UI_COLOR_YELLOW
                      : ui_tempo_running() ? UI_COLOR_GREEN : (sync ? UI_COLOR_CYAN : UI_COLOR_WHITE);
    g->setTextSize(s);
    g->setTextColor(bpmColor, UI_COLOR_BLACK);
    g->setCursor(bx, by);
    g->print(num);
    g->setTextSize(ls);
    g->setCursor(bx + numW + 6 * ls, by + 8 * s - 8 * ls);
    g->print("BPM");

    // ----- External BPM -----
    char extStr[24];
    if (ext > 0) snprintf(extStr, sizeof(extStr), "Ext: %.1f BPM", ext);
    else snprintf(extStr, sizeof(extStr), "Ext: --- BPM");
    int es = m_extTextSize;
    g->setTextSize(es);
    g->setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    g->setCursor(m_areaX + (m_areaW - (int)strlen(extStr) * 6 * es) / 2 - ox, m_extY - oy);
    g->print(extStr);

    // ----- Source buttons -----
    int ts = m_buttonTextSize;
    for (int i = 0; i < m_sourceCount; i++) {
        const Button& b = m_buttons[m_sourceButtons[i]];
        midi_interface_t src = sourceInterface(m_sourceButtons[i]);
        bool isSelected = (selected == src);
        bool hasClock = ui.getExternalBpmBySource(src) > 0;
        uint16_t bg = isSelected ? UI_COLOR_BLUE : (hasClock ? UI_COLOR_DARKGRAY : UI_COLOR_BLACK);
        uint16_t fg = hasClock ? UI_COLOR_WHITE : UI_COLOR_GRAY;
        g->fillRoundRect(b.r.x - ox, b.r.y - oy, b.r.w, b.r.h, 3, bg);
        g->drawRoundRect(b.r.x - ox, b.r.y - oy, b.r.w, b.r.h, 3, UI_COLOR_WHITE);
        g->setTextSize(ts);
        g->setTextColor(fg, bg);
        g->setCursor(b.r.x + (b.r.w - (int)strlen(b.label) * 6 * ts) / 2 - ox,
                     b.r.y + (b.r.h - 8 * ts) / 2 - oy);
        g->print(b.label);
    }

    renderPlay(g, ox, oy);

    // ----- Sync -----
    const Button& sb = m_buttons[BTN_SYNC];
    uint16_t bg, fg = UI_COLOR_WHITE;
    if (ext <= 0) {
        bg = UI_COLOR_DARKGRAY;   // No external clock to follow
        fg = UI_COLOR_GRAY;
    } else {
        bg = sync ? UI_COLOR_GREEN : UI_COLOR_NAVY;
    }
    const char* label = sync ? "SYNC" : "Sync";
    g->fillRoundRect(sb.r.x - ox, sb.r.y - oy, sb.r.w, sb.r.h, 4, bg);
    g->drawRoundRect(sb.r.x - ox, sb.r.y - oy, sb.r.w, sb.r.h, 4, UI_COLOR_WHITE);
    g->setTextSize(ts);
    g->setTextColor(fg, bg);
    g->setCursor(sb.r.x + (sb.r.w - (int)strlen(label) * 6 * ts) / 2 - ox,
                 sb.r.y + (sb.r.h - 8 * ts) / 2 - oy);
    g->print(label);
}

// Play (a triangle) while stopped, Stop (a square) while running
void ScreenMain::renderPlay(LovyanGFX* g, int ox, int oy)
{
    const Button& b = m_buttons[BTN_PLAY];
    bool running = ui_tempo_running();
    uint16_t bg = running ? UI_COLOR_GREEN : UI_COLOR_NAVY;
    uint16_t fg = running ? UI_COLOR_BLACK : UI_COLOR_WHITE;
    const char* label = running ? "Stop" : "Play";
    int ts = m_buttonTextSize;

    int x = b.r.x - ox, y = b.r.y - oy;
    g->fillRoundRect(x, y, b.r.w, b.r.h, 4, bg);
    g->drawRoundRect(x, y, b.r.w, b.r.h, 4, UI_COLOR_WHITE);

    int icon = 8 * ts;                       // As tall as the text
    int textW = (int)strlen(label) * 6 * ts;
    int gap = 3 * ts;
    int cx = x + (b.r.w - (icon + gap + textW)) / 2;
    int cy = y + (b.r.h - icon) / 2;
    if (running) {
        g->fillRect(cx, cy, icon, icon, fg);
    } else {
        g->fillTriangle(cx, cy, cx, cy + icon - 1, cx + icon - 1, cy + icon / 2, fg);
    }
    g->setTextSize(ts);
    g->setTextColor(fg, bg);
    g->setCursor(cx + icon + gap, y + (b.r.h - 8 * ts) / 2);
    g->print(label);
}

const char* ScreenMain::sourceLabel(int button)
{
    switch (button) {
        case BTN_SOURCE_USB:        return "USB-H";
        case BTN_SOURCE_USB_DEVICE: return "USB-D";
        case BTN_SOURCE_DIN:        return "DIN";
        default:                    return "BLE";
    }
}

midi_interface_t ScreenMain::sourceInterface(int button)
{
    switch (button) {
        case BTN_SOURCE_USB:        return MIDI_INTERFACE_USB;
        case BTN_SOURCE_USB_DEVICE: return MIDI_INTERFACE_USB_DEVICE;
        case BTN_SOURCE_DIN:        return MIDI_INTERFACE_DIN;
        default:                    return MIDI_INTERFACE_BLE;
    }
}

void ScreenMain::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!pressed) {
        if (touchId == m_swipeTouch) {
            m_swipeTouch = -1;
            if (m_swiping) {
                m_swiping = false;
                m_needsRedraw = true;   // Back from the swipe colour
            }
        }
        return;
    }
    if (m_bpmRect.contains(x, y)) {
        // The tap counts on the press, where its timing is. Should the touch
        // turn into a swipe, onTouchMove takes the tap back.
        if (m_swipeTouch < 0) {
            m_swipeTouch = touchId;
            m_swiping = false;
            m_swipeX = x;
            m_swipeY = y;
            m_swipeBpm = UIManager::getInstance().getInternalBpm();
        }
        ui_tempo_tap();
        return;
    }
    for (int i = 0; i < BTN_COUNT; i++) {
        if (m_buttons[i].r.w > 0 && m_buttons[i].r.contains(x, y)) {
            handleButtonPress(i);
            return;
        }
    }
}

// A swipe from the BPM: up or right is faster, 1 BPM every SWIPE_STEP_PX
// along the axis it started on, counted from the BPM before the press
void ScreenMain::onTouchMove(int touchId, int x, int y)
{
    if (touchId != m_swipeTouch) return;
    UIManager& ui = UIManager::getInstance();
    if (ui.getSyncMode()) return;   // The tempo is the external clock's

    int dx = x - m_swipeX;
    int dy = m_swipeY - y;   // Up is positive
    if (!m_swiping) {
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (adx < SWIPE_START_PX && ady < SWIPE_START_PX) return;
        m_swiping = true;
        m_swipeVertical = ady >= adx;
        // Not a tap after all: forget it, and the tempo it may have set
        ui_tempo_tap_reset();
        m_swipeBpm = (float)(int)(m_swipeBpm + 0.5f);
        m_needsRedraw = true;
    }

    int steps = (m_swipeVertical ? dy : dx) / SWIPE_STEP_PX;
    ui.setBpm(m_swipeBpm + (float)steps);
}

void ScreenMain::handleButtonPress(int buttonId)
{
    UIManager& ui = UIManager::getInstance();
    if (buttonId == BTN_SYNC) {
        // Only with an external clock to follow
        if (ui.getExternalBpm() > 0) ui.setSyncMode(!ui.getSyncMode());
    } else if (buttonId == BTN_PLAY) {
        ui_tempo_set_running(!ui_tempo_running());
    } else {
        ui.setExternalBpmSourceSelection(sourceInterface(buttonId));
    }
    m_needsRedraw = true;
}

void ScreenMain::onNavCenter()
{
    ui_tempo_tap();
}

const char* ScreenMain::getTitle()
{
    // Named for what it does now that Home is the starting point. The class,
    // UI_SCREEN_MAIN and Ruby's :main keep the old name for compatibility.
    return "Tempo";
}

void ScreenMain::getTileStatus(char* buf, int len)
{
    snprintf(buf, len, "%.0f BPM%s", (double)UIManager::getInstance().getBpm(),
             ui_tempo_running() ? " >" : "");
}

const char* ScreenMain::getNavCenterLabel()
{
    return "TAP";
}
