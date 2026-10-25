#include "sdkconfig.h"
#include "screen_sequencer.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>
#include "esp_timer.h"
#include "esp_heap_caps.h"

// Global instance
static ScreenSequencer s_screenSequencer;

ScreenSequencer& getScreenSequencer()
{
    return s_screenSequencer;
}

static constexpr int GLYPH_W = 6;
static constexpr int GLYPH_H = 8;
static constexpr int64_t LONG_PRESS_US = 500000;
static constexpr int64_t BLINK_US = 250000;
static constexpr int MOVE_SLOP = 12;          // Pixels a finger may wander and still tap
static constexpr int MAX_WINDOW = SEQ_MAX_BARS * 16;   // Widest line, in units

static constexpr uint16_t COL_BG        = UI_COLOR_BLACK;
static constexpr uint16_t COL_WHITE     = UI_COLOR_WHITE;
static constexpr uint16_t COL_RED       = UI_COLOR_RED;
static constexpr uint16_t COL_PURPLE    = UI_COLOR_PURPLE;
static constexpr uint16_t COL_YELLOW    = UI_COLOR_YELLOW;
static constexpr uint16_t COL_NAME_BG   = 0x18C3;
static constexpr uint16_t COL_NAME_SEL  = 0x3186;
static constexpr uint16_t COL_CELL_A    = 0x2124;
static constexpr uint16_t COL_CELL_B    = 0x18C3;
static constexpr uint16_t COL_BTN       = 0x2945;
static constexpr uint16_t COL_SLOT_USED = 0x3A6B;
static constexpr uint16_t COL_SLOT_PLAY = 0x05E0;
static constexpr uint16_t COL_SLOT_NEXT = 0xC600;
static constexpr uint16_t COL_ON        = 0x0451;
static constexpr uint16_t COL_MUTED     = 0x632C;
static constexpr uint16_t COL_TEXT_DIM  = 0xAD55;

static const uint16_t LINE_COLORS[8] = {
    UI_COLOR_CYAN, UI_COLOR_ORANGE, UI_COLOR_MAGENTA, UI_COLOR_GREEN,
    COL_YELLOW, 0x5D1F, 0xFB2C, 0xA81F,
};

static const char* RATE_NAMES[SEQ_RATE_COUNT] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
static const int STEP_CHOICES[] = { 4, 8, 16, 32, 64 };
static const int BAR_CHOICES[] = { 1, 2, 4, 8 };

static void noteName(int note, char* buf, int len)
{
    static const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    note &= 0x7F;
    snprintf(buf, len, "%s%d", NAMES[note % 12], note / 12 - 1);
}

static int64_t nowUs()
{
    return esp_timer_get_time();
}

// Next value of a cycle of choices
static int nextChoice(const int* choices, int n, int current)
{
    for (int i = 0; i < n; i++) {
        if (choices[i] == current) return choices[(i + 1) % n];
    }
    return choices[0];
}

// Text clipped to `w` pixels at text size `ts`
static void text(LovyanGFX* g, int ox, int oy, int x, int y, int w, const char* s,
                 uint16_t fg, int ts)
{
    int maxChars = w / (GLYPH_W * ts);
    if (maxChars <= 0) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", s);
    if ((int)strlen(buf) > maxChars) buf[maxChars] = '\0';
    g->setTextSize(ts);
    g->setTextColor(fg);
    g->setCursor(x - ox, y - oy);
    g->print(buf);
}

ScreenSequencer::ScreenSequencer()
    : m_isActive(false)
    , m_view(nullptr)
    , m_haveView(false)
    , m_rev(0)
    , m_window(0)
    , m_viewSteps(16)
    , m_follow(true)
    , m_selected(0)
    , m_scroll(0)
    , m_sheetLine(-1)
    , m_confirmDelete(false)
    , m_lastPlay(-1), m_lastNext(-1), m_lastEdit(-1)
    , m_lastRunning(false), m_lastChain(false)
    , m_lastUsed(0)
    , m_fileSheet(false)
    , m_fileSel(-1)
    , m_fileScroll(0)
    , m_fileConfirm(-1)
    , m_fileCount(0)
    , m_filesSerial(0)
    , m_files(nullptr)
    , m_fileRowH(24)
    , m_fileRows(1)
    , m_patMenu(-1)
    , m_patConfirm(-1)
    , m_patClip(-1)
    , m_chainSheet(false)
    , m_chainSel(-1)
    , m_chainScroll(0)
    , m_chainConfirm(false)
    , m_chainN(0)
    , m_lineClipPat(-1)
    , m_lineClipLine(-1)
    , m_confirmPaste(false)
    , m_dirty(false)
{
    memset(m_patBtn, 0, sizeof(m_patBtn));
    memset(m_chainBtn, 0, sizeof(m_chainBtn));
    memset(&m_chainList, 0, sizeof(m_chainList));
    m_status[0] = '\0';
    memset(m_fileBtn, 0, sizeof(m_fileBtn));
    memset(&m_fileList, 0, sizeof(m_fileList));
#if !defined(UI_LAYOUT_LARGE)
    m_viewSteps = 8;
#endif
    memset(&m_g, 0, sizeof(m_g));
    memset(m_btn, 0, sizeof(m_btn));
    memset(m_sheetBtn, 0, sizeof(m_sheetBtn));
    memset(m_touch, 0, sizeof(m_touch));
    m_clip = { 0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT };
    for (int i = 0; i < SEQ_MAX_LINES; i++) m_playCol[i] = -1;
    resetArea();
}

void ScreenSequencer::setArea(int x, int y, int w, int h)
{
    m_areaX = x;
    m_areaY = y;
    m_areaW = w;
    m_areaH = h;
}

void ScreenSequencer::resetArea()
{
    setArea(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT);
}

// --- Model -------------------------------------------------------------------

bool ScreenSequencer::refresh(bool notes)
{
    if (!m_view) {
        m_view = (ui_seq::View*)heap_caps_calloc(1, sizeof(ui_seq::View),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!m_view) m_view = (ui_seq::View*)calloc(1, sizeof(ui_seq::View));
        if (!m_view) return false;
        notes = true;
    }
    if (!m_haveView) notes = true;
    if (!ui_seq::snapshot(m_view, m_window, m_viewSteps, notes)) return false;
    m_haveView = true;
    if (m_selected >= m_view->lineCount) m_selected = m_view->lineCount - 1;
    if (m_sheetLine >= m_view->lineCount) m_sheetLine = -1;
    return true;
}

int ScreenSequencer::unitTicks(int line) const
{
    return m_view->lines[line].mode == SEQ_MODE_GRID
               ? seq_rate_ticks(m_view->lines[line].rate) : ui_seq::RT_UNIT_TICKS;
}

int ScreenSequencer::playColOf(int line) const
{
    if (!m_haveView || line >= m_view->lineCount) return -1;
    const ui_seq::LineView& lv = m_view->lines[line];
    if (lv.pos < 0 || lv.cols == 0) return -1;
    int u = lv.pos / unitTicks(line);
    int rel = (u - lv.s0 + lv.cols) % lv.cols;
    return rel < lv.shown ? rel : -1;
}

// --- Geometry ----------------------------------------------------------------

void ScreenSequencer::computeGeom()
{
    Geom& g = m_g;
    bool wide = m_areaW >= 700;
    int gap = wide ? 4 : 2;
    g.textSize = wide ? 2 : 1;
    g.headerRowH = wide ? 44 : 22;
    g.headerH = g.headerRowH * 2;
    g.linesY = m_areaY + g.headerH;
    g.linesH = m_areaH - g.headerH;
    if (g.linesH < 1) g.linesH = 1;
    g.nameW = wide ? 200 : (m_areaW >= 480 ? 110 : 58);
    g.cellX = m_areaX + g.nameW + gap;
    g.cellW = m_areaX + m_areaW - g.cellX;
    if (g.cellW < 1) g.cellW = 1;

    int lines = m_haveView ? m_view->lineCount : SEQ_DEFAULT_LINES;
    g.total = lines + (lines < SEQ_MAX_LINES ? 1 : 0);
    int minRow = wide ? 56 : 28;
    int maxRow = wide ? 80 : 40;
    g.rowH = g.linesH / g.total;
    if (g.rowH > maxRow) g.rowH = maxRow;
    if (g.rowH < minRow) g.rowH = minRow;
    if (g.rowH > g.linesH) g.rowH = g.linesH;
    g.rows = g.linesH / g.rowH;
    if (g.rows < 1) g.rows = 1;

    // Header buttons
    memset(m_btn, 0, sizeof(m_btn));
    int y1 = m_areaY, y2 = m_areaY + g.headerRowH, bh = g.headerRowH - gap;
    int right = m_areaX + m_areaW;
    int chainW = wide ? 110 : 52;
    m_btn[BTN_CHAIN] = { right - chainW, y1, chainW, bh };
    int x = m_areaX;
    if (wide) {
        int slotW = (m_areaW - chainW - 260) / SEQ_MAX_PATTERNS;
        if (slotW > 56) slotW = 56;
        for (int i = 0; i < SEQ_MAX_PATTERNS; i++) {
            m_btn[BTN_SLOT0 + i] = { x, y1, slotW - gap, bh };
            x += slotW;
        }
    } else {
        m_btn[BTN_PAT_PREV] = { x, y1, 24, bh };
        x += 24 + gap;
        m_btn[BTN_PAT] = { x, y1, 40, bh };
        x += 40 + gap;
        m_btn[BTN_PAT_NEXT] = { x, y1, 24, bh };
        x += 24 + gap;
    }
    m_btn[BTN_CHORD] = { x + gap, y1, right - chainW - gap - (x + gap), bh };

    x = m_areaX;
    int arrowW = wide ? 56 : 24;
    m_btn[BTN_WIN_PREV] = { x, y2, arrowW, bh };
    x += arrowW + gap;
    int winW = wide ? 200 : 76;
    m_btn[BTN_WIN] = { x, y2, winW, bh };
    x += winW + gap;
    m_btn[BTN_WIN_NEXT] = { x, y2, arrowW, bh };
    x += arrowW + gap * 3;
    int viewW = wide ? 80 : 30;
    m_btn[BTN_VIEW] = { x, y2, viewW, bh };
    x += viewW + gap;
    int followW = wide ? 120 : 44;
    m_btn[BTN_FOLLOW] = { x, y2, followW, bh };
    int playW = wide ? 160 : 56;
    m_btn[BTN_PLAY] = { right - playW, y2, playW, bh };
    int fileW = wide ? 200 : 50;
    m_btn[BTN_FILE] = { right - playW - gap - fileW, y2, fileW, bh };

    // File sheet: the songs on the left, the buttons down the right
    int listW = m_areaW * 3 / 5;
    m_fileList = { m_areaX, g.linesY, listW, m_areaY + m_areaH - g.linesY };
    m_fileRowH = wide ? 48 : 24;
    m_fileRows = m_fileList.h / m_fileRowH;
    if (m_fileRows < 1) m_fileRows = 1;
    int fbx = m_areaX + listW + gap * 2;
    int fbh = (m_areaY + m_areaH - g.linesY) / FB_COUNT;
    for (int i = 0; i < FB_COUNT; i++) {
        m_fileBtn[i] = { fbx, g.linesY + i * fbh + gap, right - fbx, fbh - gap * 2 };
    }

    // Pattern menu: a title row, then 3 x 2 buttons
    int py = g.linesY + g.headerRowH;
    int ph = (m_areaY + m_areaH - py) / 2;
    int pw = m_areaW / 3;
    for (int i = 0; i < PM_COUNT; i++) {
        int r = i / 3, c = i % 3;
        m_patBtn[i] = { m_areaX + c * pw + gap, py + r * ph + gap, pw - gap * 2, ph - gap * 2 };
    }

    // Chain sheet: the entries on the left, 2 x 6 buttons on the right
    int chainListW = m_areaW / 2;
    m_chainList = { m_areaX, g.linesY + g.headerRowH, chainListW,
                    m_areaY + m_areaH - g.linesY - g.headerRowH };
    static const int8_t CHAIN_GRID[6][2] = {
        { CB_ADD, CB_DEL }, { CB_PAT_DN, CB_PAT_UP }, { CB_REP_DN, CB_REP_UP },
        { CB_UP, CB_DOWN }, { CB_CLEAR, CB_ON }, { CB_CLOSE, -1 },
    };
    int cbx = m_areaX + chainListW + gap * 2;
    int cbw = (right - cbx) / 2;
    int cbh = (m_areaY + m_areaH - g.linesY) / 6;
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 2; c++) {
            int id = CHAIN_GRID[r][c];
            if (id < 0) continue;
            m_chainBtn[id] = { cbx + c * cbw, g.linesY + r * cbh + gap, cbw - gap * 2, cbh - gap * 2 };
        }
    }

    // Settings sheet: a title row, then 4 x 4 buttons
    memset(m_sheetBtn, 0, sizeof(m_sheetBtn));
    int sy = g.linesY + g.headerRowH;
    int sh = (m_areaY + m_areaH - sy) / 4;
    int sw = m_areaW / 4;
    static const int8_t GRID[4][4] = {
        { SB_MODE, SB_CH_DN, SB_CH, SB_CH_UP },
        { SB_STEPS, SB_RATE, SB_BARS, SB_MUTE },
        { SB_GATE_DN, SB_GATE, SB_GATE_UP, SB_CLEAR },
        { SB_DELETE, SB_COPY, SB_PASTE, SB_CLOSE },
    };
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int id = GRID[r][c];
            if (id < 0) continue;
            m_sheetBtn[id] = { m_areaX + c * sw + gap, sy + r * sh + gap, sw - gap * 2, sh - gap * 2 };
        }
    }
}

void ScreenSequencer::clampScroll()
{
    int maxScroll = m_g.total - m_g.rows;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
    if (m_scroll < 0) m_scroll = 0;
}

int ScreenSequencer::rowY(int row) const
{
    return m_g.linesY + (row - m_scroll) * m_g.rowH;
}

int ScreenSequencer::lineAtRow(int y) const
{
    if (y < m_g.linesY) return -1;
    int r = (y - m_g.linesY) / m_g.rowH;
    if (r >= m_g.rows) return -1;
    int row = r + m_scroll;
    return row < m_g.total ? row : -1;
}

void ScreenSequencer::cellRect(int row, int col, int shown, Rect& r) const
{
    if (shown < 1) shown = 1;
    int x0 = m_g.cellX + col * m_g.cellW / shown;
    int x1 = m_g.cellX + (col + 1) * m_g.cellW / shown;
    r = { x0, rowY(row), x1 - x0, m_g.rowH - 1 };
}

int ScreenSequencer::colAt(int x, int shown) const
{
    if (shown < 1) return 0;
    int c = (x - m_g.cellX) * shown / m_g.cellW;
    if (c < 0) c = 0;
    if (c >= shown) c = shown - 1;
    return c;
}

// The square at the right of a line's name that mutes it
static void muteRect(int nameX, int nameW, int y, int rowH, int& x, int& my, int& s)
{
    s = rowH - 12;
    if (s > 40) s = 40;
    if (s < 12) s = 12;
    x = nameX + nameW - s - 4;
    my = y + (rowH - s) / 2;
}

// --- Drawing -----------------------------------------------------------------

void ScreenSequencer::button(LovyanGFX* g, int ox, int oy, const Rect& r, const char* label,
                             uint16_t bg, uint16_t fg, bool outline)
{
    if (r.w <= 0) return;
    g->fillRoundRect(r.x - ox, r.y - oy, r.w, r.h, 4, bg);
    if (outline) g->drawRoundRect(r.x - ox, r.y - oy, r.w, r.h, 4, COL_WHITE);
    int ts = m_g.textSize;
    int tw = (int)strlen(label) * GLYPH_W * ts;
    if (tw > r.w - 4 && ts > 1) {
        ts = 1;
        tw = (int)strlen(label) * GLYPH_W;
    }
    int tx = r.x + (r.w - tw) / 2;
    if (tx < r.x + 2) tx = r.x + 2;
    text(g, ox, oy, tx, r.y + (r.h - GLYPH_H * ts) / 2, r.w - 4, label, fg, ts);
}

void ScreenSequencer::renderHeader(LovyanGFX* g, int ox, int oy)
{
    const ui_seq::View& v = *m_view;
    char buf[48];
    bool blinkOn = (nowUs() / BLINK_US) % 2 == 0;

    for (int i = 0; i < SEQ_MAX_PATTERNS; i++) {
        const Rect& r = m_btn[BTN_SLOT0 + i];
        if (r.w <= 0) continue;
        uint16_t bg = COL_BTN;
        if (v.used & (1u << i)) bg = COL_SLOT_USED;
        if (i == v.play) bg = v.running ? COL_SLOT_PLAY : 0x03A0;
        if (i == v.next && blinkOn) bg = COL_SLOT_NEXT;
        snprintf(buf, sizeof(buf), "%d", i + 1);
        button(g, ox, oy, r, buf, bg, COL_WHITE, i == v.edit);
    }
    if (m_btn[BTN_PAT].w > 0) {
        button(g, ox, oy, m_btn[BTN_PAT_PREV], "<", COL_BTN, COL_WHITE);
        snprintf(buf, sizeof(buf), "P%d%s", v.shown + 1, v.edit >= 0 ? "*" : "");
        uint16_t bg = (v.next >= 0 && blinkOn) ? COL_SLOT_NEXT : (v.running ? COL_SLOT_PLAY : COL_SLOT_USED);
        button(g, ox, oy, m_btn[BTN_PAT], buf, bg, COL_WHITE, v.edit >= 0);
        button(g, ox, oy, m_btn[BTN_PAT_NEXT], ">", COL_BTN, COL_WHITE);
    }
    button(g, ox, oy, m_btn[BTN_CHAIN], "Chain", v.chainOn ? COL_PURPLE : COL_BTN,
           v.chainLen ? COL_WHITE : COL_TEXT_DIM);

    // The chord the grid places
    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < v.chordN && pos < (int)sizeof(buf) - 6; i++) {
        char nn[6];
        noteName(v.chord[i], nn, sizeof(nn));
        pos += snprintf(buf + pos, sizeof(buf) - pos, i ? " %s" : "%s", nn);
    }
    const Rect& cr = m_btn[BTN_CHORD];
    if (cr.w > 0) {
        g->fillRoundRect(cr.x - ox, cr.y - oy, cr.w, cr.h, 4, 0x1082);
        text(g, ox, oy, cr.x + 6, cr.y + (cr.h - GLYPH_H * m_g.textSize) / 2, cr.w - 12, buf,
             COL_YELLOW, m_g.textSize);
    }

    // The steps on screen, as the selected line counts them
    button(g, ox, oy, m_btn[BTN_WIN_PREV], "<", COL_BTN, COL_WHITE);
    button(g, ox, oy, m_btn[BTN_WIN_NEXT], ">", COL_BTN, COL_WHITE);
    buf[0] = '\0';
    if (m_selected >= 0 && m_selected < v.lineCount) {
        const ui_seq::LineView& lv = v.lines[m_selected];
        int last = (lv.s0 + lv.shown - 1) % (lv.cols ? lv.cols : 1);
        snprintf(buf, sizeof(buf), "%d-%d/%d", lv.s0 + 1, last + 1, lv.cols);
    }
    button(g, ox, oy, m_btn[BTN_WIN], buf, 0x1082, COL_WHITE);
    snprintf(buf, sizeof(buf), "%d", m_viewSteps);
    button(g, ox, oy, m_btn[BTN_VIEW], buf, COL_BTN, COL_WHITE);
    button(g, ox, oy, m_btn[BTN_FOLLOW], m_g.textSize > 1 ? "Follow" : "Fol",
           m_follow ? COL_ON : COL_BTN, COL_WHITE);
    button(g, ox, oy, m_btn[BTN_PLAY], v.running ? "Stop" : "Play",
           v.running ? COL_SLOT_PLAY : COL_SLOT_USED, COL_WHITE);
    // The song's name (* while unsaved), or a fresh message from saving
    if (m_status[0]) {
        snprintf(buf, sizeof(buf), "%s", m_status);
    } else {
        snprintf(buf, sizeof(buf), "%s%s", v.name[0] ? v.name : "File", m_dirty ? "*" : "");
    }
    button(g, ox, oy, m_btn[BTN_FILE], buf, m_fileSheet ? COL_ON : COL_BTN,
           m_status[0] ? COL_YELLOW : COL_WHITE);
}

void ScreenSequencer::renderLine(LovyanGFX* g, int ox, int oy, int line, int y)
{
    const ui_seq::LineView& lv = m_view->lines[line];
    int rowH = m_g.rowH;
    int ts = m_g.textSize;
    bool grid = lv.mode == SEQ_MODE_GRID;
    uint16_t color = lv.mute ? COL_MUTED : LINE_COLORS[line % 8];
    char buf[40];

    // Name column
    g->fillRect(m_areaX - ox, y - oy, m_g.nameW, rowH - 1, line == m_selected ? COL_NAME_SEL : COL_NAME_BG);
    g->fillRect(m_areaX - ox, y - oy, 4, rowH - 1, color);
    int mx, my, ms;
    muteRect(m_areaX, m_g.nameW, y, rowH, mx, my, ms);
    int textW = mx - m_areaX - 10;
    if (lv.name[0]) {
        snprintf(buf, sizeof(buf), "%d %s", line + 1, lv.name);
    } else {
        snprintf(buf, sizeof(buf), "%d", line + 1);
    }
    int lineH = GLYPH_H * ts;
    int ty = y + (rowH - lineH * 2 - 4) / 2;
    text(g, ox, oy, m_areaX + 8, ty, textW, buf, COL_WHITE, ts);
    if (grid) {
        snprintf(buf, sizeof(buf), ts > 1 ? "ch%d G %dx%s" : "c%d G%d", lv.channel + 1, lv.steps,
                 RATE_NAMES[lv.rate % SEQ_RATE_COUNT]);
    } else {
        snprintf(buf, sizeof(buf), ts > 1 ? "ch%d R %dbar" : "c%d R%d", lv.channel + 1, lv.bars);
    }
    text(g, ox, oy, m_areaX + 8, ty + lineH + 4, textW, buf, COL_TEXT_DIM, ts);
    g->fillRect(mx - ox, my - oy, ms, ms, lv.mute ? COL_RED : 0x2945);
    g->drawRect(mx - ox, my - oy, ms, ms, lv.mute ? COL_RED : COL_TEXT_DIM);
    text(g, ox, oy, mx + (ms - GLYPH_W) / 2, my + (ms - GLYPH_H) / 2, ms, "M", COL_WHITE, 1);

    // Cells
    int row = line;
    int shown = lv.shown ? lv.shown : 1;
    uint32_t unit = grid ? 1 : ui_seq::RT_UNIT_TICKS;
    uint32_t period = grid ? lv.steps : lv.length;
    int innerY = y + 3, innerH = rowH - 7;
    int range = lv.hi >= lv.lo ? lv.hi - lv.lo + 1 : 1;
    int band = innerH / range;
    bool collapse = band < 3;
    int playCol = playColOf(line);

    int clipX1 = m_clip.x + m_clip.w;
    for (int c = 0; c < shown; c++) {
        Rect cr;
        cellRect(row, c, shown, cr);
        if (cr.x + cr.w <= m_clip.x || cr.x >= clipX1) continue;
        uint32_t u = (lv.s0 + c) % (lv.cols ? lv.cols : 1);
        g->fillRect(cr.x - ox, cr.y - oy, cr.w - 1, cr.h, (u / 4) % 2 ? COL_CELL_B : COL_CELL_A);
    }

    // Notes, column by column (a note across the loop end comes back at its start)
    for (int c = 0; c < shown && period > 0; c++) {
        Rect cr;
        cellRect(row, c, shown, cr);
        if (cr.x + cr.w <= m_clip.x || cr.x >= clipX1) continue;
        uint32_t u = (lv.s0 + c) % lv.cols;
        uint32_t c0 = u * unit;
        int starts = 0, lowest = 128;
        for (int i = 0; i < lv.n; i++) {
            const seq_note_t& nt = lv.notes[i];
            uint32_t a, b;
            uint32_t d = (c0 + period - nt.pos) % period;
            bool startsHere = false;
            if (d < nt.len) {
                a = 0;
                b = nt.len - d < unit ? nt.len - d : unit;
                startsHere = d == 0;
            } else {
                uint32_t d2 = (nt.pos + period - c0) % period;
                if (d2 >= unit) continue;
                a = d2;
                b = d2 + nt.len < unit ? d2 + nt.len : unit;
                startsHere = true;
            }
            int px0 = cr.x + (int)(a * cr.w / unit);
            int px1 = cr.x + (int)(b * cr.w / unit);
            if (px1 <= px0) px1 = px0 + 1;
            int ny, nh;
            if (collapse) {
                ny = innerY + innerH / 4;
                nh = innerH / 2;
            } else {
                ny = innerY + innerH - (nt.note - lv.lo + 1) * innerH / range;
                nh = band - 1 > 2 ? band - 1 : 2;
            }
            g->fillRect(px0 - ox, ny - oy, px1 - px0, nh, color);
            if (startsHere) {
                g->fillRect(px0 - ox, ny - oy, 2, nh, COL_WHITE);
                starts++;
                if (nt.note < lowest) lowest = nt.note;
            }
        }
        // What starts here, where it fits
        if (grid && starts > 0 && cr.w >= GLYPH_W * ts * 4) {
            char nn[6];
            noteName(lowest, nn, sizeof(nn));
            if (starts > 1) {
                snprintf(buf, sizeof(buf), "%s+%d", nn, starts - 1);
            } else {
                snprintf(buf, sizeof(buf), "%s", nn);
            }
            text(g, ox, oy, cr.x + 4, cr.y + 3, cr.w - 6, buf, COL_WHITE, ts > 1 && cr.w >= GLYPH_W * 2 * 6 ? 2 : 1);
        } else if (collapse && starts > 1 && cr.w >= GLYPH_W * 2) {
            snprintf(buf, sizeof(buf), "%d", starts);
            text(g, ox, oy, cr.x + 3, cr.y + 3, cr.w - 4, buf, COL_WHITE, 1);
        }
    }

    // A finger placing or stretching notes
    for (int t = 0; t < MAX_TOUCH_POINTS; t++) {
        const Touch& tc = m_touch[t];
        if (tc.kind != T_CELL || tc.target != line || tc.longFired) continue;
        if (tc.onNote && !tc.moved) continue;
        Rect a, b;
        cellRect(row, tc.startCol, shown, a);
        cellRect(row, tc.curCol, shown, b);
        int w = b.x + b.w - a.x;
        g->drawRect(a.x - ox, a.y - oy, w - 1, a.h, COL_YELLOW);
        g->drawRect(a.x + 1 - ox, a.y + 1 - oy, w - 3, a.h - 2, COL_YELLOW);
    }

    if (playCol >= 0) {
        Rect cr;
        cellRect(row, playCol, shown, cr);
        g->drawRect(cr.x - ox, cr.y - oy, cr.w - 1, cr.h, COL_WHITE);
        g->drawRect(cr.x + 1 - ox, cr.y + 1 - oy, cr.w - 3, cr.h - 2, COL_WHITE);
    }
}

void ScreenSequencer::renderPlus(LovyanGFX* g, int ox, int oy, int y)
{
    g->fillRect(m_areaX - ox, y - oy, m_g.nameW, m_g.rowH - 1, 0x10A2);
    text(g, ox, oy, m_areaX + 8, y + (m_g.rowH - GLYPH_H * m_g.textSize) / 2, m_g.nameW - 10,
         "+ Line", COL_TEXT_DIM, m_g.textSize);
}

void ScreenSequencer::renderSheet(LovyanGFX* g, int ox, int oy)
{
    int line = m_sheetLine;
    int p = UI_SEQ_PATTERN_EDIT;
    char buf[40], name[16];
    ui_seq_get_name(line, name, sizeof(name));
    snprintf(buf, sizeof(buf), "Line %d %s", line + 1, name);
    text(g, ox, oy, m_areaX + 8, m_g.linesY + (m_g.headerRowH - GLYPH_H * m_g.textSize) / 2,
         m_areaW - 16, buf, LINE_COLORS[line % 8], m_g.textSize);

    int mode = ui_seq_get_i(p, line, "mode");
    int ch = ui_seq_get_i(p, line, "channel");
    int mute = ui_seq_get_i(p, line, "mute");
    int gate = ui_seq_get_i(p, line, "gate");
    int steps = ui_seq_get_i(p, line, "steps");
    int rate = ui_seq_get_i(p, line, "rate");
    int bars = ui_seq_get_i(p, line, "bars");

    button(g, ox, oy, m_sheetBtn[SB_MODE], mode == SEQ_MODE_GRID ? "Grid" : "Realtime", COL_ON, COL_WHITE);
    button(g, ox, oy, m_sheetBtn[SB_CH_DN], "Ch -", COL_BTN, COL_WHITE);
    snprintf(buf, sizeof(buf), "Ch %d", ch + 1);
    button(g, ox, oy, m_sheetBtn[SB_CH], buf, 0x1082, COL_WHITE);
    button(g, ox, oy, m_sheetBtn[SB_CH_UP], "Ch +", COL_BTN, COL_WHITE);
    uint16_t gridFg = mode == SEQ_MODE_GRID ? COL_WHITE : COL_TEXT_DIM;
    snprintf(buf, sizeof(buf), "Steps %d", steps);
    button(g, ox, oy, m_sheetBtn[SB_STEPS], buf, COL_BTN, gridFg);
    snprintf(buf, sizeof(buf), "Rate %s", RATE_NAMES[rate % SEQ_RATE_COUNT]);
    button(g, ox, oy, m_sheetBtn[SB_RATE], buf, COL_BTN, gridFg);
    snprintf(buf, sizeof(buf), "Bars %d", bars);
    button(g, ox, oy, m_sheetBtn[SB_BARS], buf, COL_BTN,
           mode == SEQ_MODE_REALTIME ? COL_WHITE : COL_TEXT_DIM);
    button(g, ox, oy, m_sheetBtn[SB_MUTE], mute ? "Muted" : "Mute", mute ? COL_RED : COL_BTN, COL_WHITE);
    button(g, ox, oy, m_sheetBtn[SB_GATE_DN], "Gate -", COL_BTN, gridFg);
    snprintf(buf, sizeof(buf), "Gate %d%%", gate);
    button(g, ox, oy, m_sheetBtn[SB_GATE], buf, 0x1082, gridFg);
    button(g, ox, oy, m_sheetBtn[SB_GATE_UP], "Gate +", COL_BTN, gridFg);
    button(g, ox, oy, m_sheetBtn[SB_CLEAR], "Clear", COL_BTN, COL_WHITE);
    bool last = m_haveView && m_view->lineCount <= 1;
    button(g, ox, oy, m_sheetBtn[SB_DELETE], m_confirmDelete ? "Sure?" : "Delete",
           m_confirmDelete ? COL_RED : COL_BTN, last ? COL_TEXT_DIM : COL_WHITE);
    button(g, ox, oy, m_sheetBtn[SB_COPY], "Copy", COL_BTN, COL_WHITE);
    bool canPaste = m_lineClipLine >= 0 && !(m_lineClipLine == line && m_haveView &&
                                             m_lineClipPat == m_view->shown);
    if (m_confirmPaste) {
        snprintf(buf, sizeof(buf), "Sure?");
    } else if (m_lineClipLine >= 0) {
        snprintf(buf, sizeof(buf), "Paste %d.%d", m_lineClipPat + 1, m_lineClipLine + 1);
    } else {
        snprintf(buf, sizeof(buf), "Paste");
    }
    button(g, ox, oy, m_sheetBtn[SB_PASTE], buf, m_confirmPaste ? COL_RED : COL_BTN,
           canPaste ? COL_WHITE : COL_TEXT_DIM);
    button(g, ox, oy, m_sheetBtn[SB_CLOSE], "Close", COL_SLOT_USED, COL_WHITE);
}

void ScreenSequencer::renderPatternMenu(LovyanGFX* g, int ox, int oy)
{
    const ui_seq::View& v = *m_view;
    int p = m_patMenu;
    char buf[64];
    bool used = (v.used >> p) & 1;
    snprintf(buf, sizeof(buf), "Pattern %d%s%s%s", p + 1, used ? "" : " (empty)",
             p == v.play ? "  playing" : "", p == v.edit ? "  editing" : "");
    text(g, ox, oy, m_areaX + 8, m_g.linesY + (m_g.headerRowH - GLYPH_H * m_g.textSize) / 2,
         m_areaW - 16, buf, COL_WHITE, m_g.textSize);

    button(g, ox, oy, m_patBtn[PM_EDIT], p == v.edit ? "Follow play" : "Edit here",
           p == v.edit ? COL_ON : COL_BTN, COL_WHITE);
    button(g, ox, oy, m_patBtn[PM_COPY], m_patClip == p ? "Copied" : "Copy", COL_BTN, COL_WHITE);
    bool canPaste = m_patClip >= 0 && m_patClip != p;
    if (m_patConfirm == PM_PASTE) {
        snprintf(buf, sizeof(buf), "Sure?");
    } else if (m_patClip >= 0) {
        snprintf(buf, sizeof(buf), "Paste %d", m_patClip + 1);
    } else {
        snprintf(buf, sizeof(buf), "Paste");
    }
    button(g, ox, oy, m_patBtn[PM_PASTE], buf, m_patConfirm == PM_PASTE ? COL_RED : COL_BTN,
           canPaste ? COL_WHITE : COL_TEXT_DIM);
    button(g, ox, oy, m_patBtn[PM_CLEAR], m_patConfirm == PM_CLEAR ? "Sure?" : "Clear",
           m_patConfirm == PM_CLEAR ? COL_RED : COL_BTN, used ? COL_WHITE : COL_TEXT_DIM);
    int length = ui_seq_get_i(p, -1, "length");
    if (length == 0) {
        snprintf(buf, sizeof(buf), "Length auto");
    } else {
        snprintf(buf, sizeof(buf), "Length %d bar%s", length, length > 1 ? "s" : "");
    }
    button(g, ox, oy, m_patBtn[PM_LENGTH], buf, COL_BTN, COL_WHITE);
    button(g, ox, oy, m_patBtn[PM_CLOSE], "Close", COL_SLOT_USED, COL_WHITE);
}

void ScreenSequencer::renderChain(LovyanGFX* g, int ox, int oy)
{
    const ui_seq::View& v = *m_view;
    int ts = m_g.textSize;
    char buf[40];
    snprintf(buf, sizeof(buf), "Chain (%s)", v.chainOn ? "on" : "off");
    text(g, ox, oy, m_areaX + 8, m_g.linesY + (m_g.headerRowH - GLYPH_H * ts) / 2,
         m_areaW - 16, buf, v.chainOn ? COL_PURPLE : COL_WHITE, ts);

    const Rect& lr = m_chainList;
    int rowH = m_fileRowH;
    int rows = lr.h / rowH;
    if (m_chainN == 0) {
        text(g, ox, oy, lr.x + 8, lr.y + 8, lr.w - 16, "Empty: Add puts the pattern on screen",
             COL_TEXT_DIM, ts > 1 ? 1 : ts);
    }
    for (int r = 0; r < rows; r++) {
        int i = m_chainScroll + r;
        if (i >= m_chainN) break;
        int y = lr.y + r * rowH;
        g->fillRect(lr.x - ox, y - oy, lr.w, rowH - 1, i == m_chainSel ? COL_NAME_SEL : COL_NAME_BG);
        snprintf(buf, sizeof(buf), "%2d  P%d  x%d", i + 1, m_chainP[i] + 1, m_chainR[i]);
        text(g, ox, oy, lr.x + 8, y + (rowH - GLYPH_H * ts) / 2, lr.w - 16, buf, COL_WHITE, ts);
    }

    bool sel = m_chainSel >= 0 && m_chainSel < m_chainN;
    static const char* LABELS[CB_COUNT] = {
        "Add", "Delete", "Pat -", "Pat +", "Rep -", "Rep +", "Up", "Down", "Clear", "On", "Close",
    };
    for (int b = 0; b < CB_COUNT; b++) {
        bool enabled = b == CB_ADD || b == CB_CLOSE || b == CB_ON || (b == CB_CLEAR && m_chainN > 0) || sel;
        const char* label = LABELS[b];
        uint16_t bg = COL_BTN;
        if (b == CB_CLEAR && m_chainConfirm) {
            label = "Sure?";
            bg = COL_RED;
        } else if (b == CB_ON) {
            label = v.chainOn ? "Turn off" : "Turn on";
            bg = v.chainOn ? COL_PURPLE : COL_BTN;
            enabled = m_chainN > 0;
        } else if (b == CB_CLOSE) {
            bg = COL_SLOT_USED;
        }
        button(g, ox, oy, m_chainBtn[b], label, bg, enabled ? COL_WHITE : COL_TEXT_DIM);
    }
}

void ScreenSequencer::renderFiles(LovyanGFX* g, int ox, int oy)
{
    int ts = m_g.textSize;
    const Rect& lr = m_fileList;
    if (m_fileCount == 0) {
        text(g, ox, oy, lr.x + 8, lr.y + 8, lr.w - 16, "No songs saved yet", COL_TEXT_DIM, ts);
    }
    for (int r = 0; r < m_fileRows; r++) {
        int i = m_fileScroll + r;
        if (i >= m_fileCount) break;
        int y = lr.y + r * m_fileRowH;
        g->fillRect(lr.x - ox, y - oy, lr.w, m_fileRowH - 1, i == m_fileSel ? COL_NAME_SEL : COL_NAME_BG);
        bool current = m_haveView && strcmp(m_files[i], m_view->name) == 0;
        text(g, ox, oy, lr.x + 8, y + (m_fileRowH - GLYPH_H * ts) / 2, lr.w - 16, m_files[i],
             current ? COL_YELLOW : COL_WHITE, ts);
    }

    bool sel = m_fileSel >= 0 && m_fileSel < m_fileCount;
    static const char* LABELS[FB_COUNT] = { "Save", "Save new", "Load", "New", "Delete", "Close" };
    for (int b = 0; b < FB_COUNT; b++) {
        bool confirm = m_fileConfirm == b;
        bool enabled = (b != FB_LOAD && b != FB_DELETE) || sel;
        uint16_t bg = confirm ? COL_RED : (b == FB_CLOSE ? COL_SLOT_USED : COL_BTN);
        button(g, ox, oy, m_fileBtn[b], confirm ? "Sure?" : LABELS[b], bg,
               enabled ? COL_WHITE : COL_TEXT_DIM);
    }
}

void ScreenSequencer::render(LovyanGFX* g, int ox, int oy)
{
    if (!m_haveView) return;
    int clipEnd = m_clip.y + m_clip.h;
    if (m_clip.y < m_areaY + m_g.headerH) renderHeader(g, ox, oy);
    if (m_fileSheet) {
        renderFiles(g, ox, oy);
        return;
    }
    if (m_patMenu >= 0) {
        renderPatternMenu(g, ox, oy);
        return;
    }
    if (m_chainSheet) {
        renderChain(g, ox, oy);
        return;
    }
    if (m_sheetLine >= 0) {
        renderSheet(g, ox, oy);
        return;
    }
    for (int r = 0; r < m_g.rows; r++) {
        int row = m_scroll + r;
        if (row >= m_g.total) break;
        int y = rowY(row);
        if (y + m_g.rowH <= m_clip.y || y >= clipEnd) continue;
        if (row < m_view->lineCount) {
            renderLine(g, ox, oy, row, y);
        } else {
            renderPlus(g, ox, oy, y);
        }
    }
}

void ScreenSequencer::paint(int x, int y, int w, int h)
{
    // Keep inside the area
    if (x < m_areaX) { w -= m_areaX - x; x = m_areaX; }
    if (y < m_areaY) { h -= m_areaY - y; y = m_areaY; }
    if (x + w > m_areaX + m_areaW) w = m_areaX + m_areaW - x;
    if (y + h > m_areaY + m_areaH) h = m_areaY + m_areaH - y;
    if (w <= 0 || h <= 0) return;
    m_clip = { x, y, w, h };

    if (!ui_strip::paint(x, y, w, h, COL_BG, [&](LovyanGFX* g, int ox, int oy) {
            render(g, ox, oy);
        })) {
        LovyanGFX& g = ui_gfx();
        g.setClipRect(x, y, w, h);
        g.fillRect(x, y, w, h, COL_BG);
        render(&g, 0, 0);
        g.clearClipRect();
    }
}

void ScreenSequencer::paintAll()
{
    paint(m_areaX, m_areaY, m_areaW, m_areaH);
}

void ScreenSequencer::paintHeader()
{
    paint(m_areaX, m_areaY, m_areaW, m_g.headerH);
}

void ScreenSequencer::paintRow(int row)
{
    if (row < m_scroll || row >= m_scroll + m_g.rows) return;
    paint(m_areaX, rowY(row), m_areaW, m_g.rowH);
}

void ScreenSequencer::paintCell(int line, int col)
{
    if (col < 0 || line < m_scroll || line >= m_scroll + m_g.rows || sheetOpen()) return;
    Rect r;
    cellRect(line, col, m_view->lines[line].shown, r);
    paint(r.x, r.y, r.w, r.h);
}

// --- Lifecycle ---------------------------------------------------------------

void ScreenSequencer::enter()
{
    m_isActive = true;
    memset(m_touch, 0, sizeof(m_touch));
    m_haveView = false;
    refresh(true);
    m_rev = ui_seq::revision();
    computeGeom();
    clampScroll();
    for (int l = 0; l < SEQ_MAX_LINES; l++) m_playCol[l] = playColOf(l);
    if (m_haveView) {
        m_lastPlay = m_view->play;
        m_lastNext = m_view->next;
        m_lastEdit = m_view->edit;
        m_lastRunning = m_view->running;
        m_lastChain = m_view->chainOn;
        m_lastUsed = m_view->used;
    }
    // UIManager (or the layout) paints everything right after this
}

void ScreenSequencer::leave()
{
    m_isActive = false;
    memset(m_touch, 0, sizeof(m_touch));
    closeSheets();
}

void ScreenSequencer::draw()
{
    computeGeom();
    clampScroll();
    paintAll();
}

void ScreenSequencer::moveWindow(int delta)
{
    m_window = (m_window + delta + MAX_WINDOW) % MAX_WINDOW;
    refresh(true);
    paintAll();
}

// Keeps the selected line's play position on screen, half a page at a time
void ScreenSequencer::follow()
{
    const ui_seq::View& v = *m_view;
    if (!m_follow || !v.running || v.shown != v.play || sheetOpen()) return;
    int l = m_selected >= 0 && m_selected < v.lineCount ? m_selected : 0;
    const ui_seq::LineView& lv = v.lines[l];
    if (lv.cols <= m_viewSteps || lv.pos < 0) return;
    int half = m_viewSteps / 2;
    int u = lv.pos / unitTicks(l);
    int rel = (u - lv.s0 + lv.cols) % lv.cols;
    int s0;
    if (rel >= m_viewSteps) {
        s0 = (u / half) * half;          // Jumped (the loop came round): page to it
    } else if (rel >= m_viewSteps * 3 / 4) {
        s0 = (lv.s0 + half) % lv.cols;   // Nearing the right edge
    } else {
        return;
    }
    m_window = s0;
    refresh(true);
    for (int i = 0; i < SEQ_MAX_LINES; i++) m_playCol[i] = playColOf(i);
    paintAll();
}

void ScreenSequencer::update()
{
    if (!m_isActive) return;
    checkLongPress();

    uint32_t rev = ui_seq::revision();
    if (!m_haveView || rev != m_rev) {
        // The song changed (an edit here or from Ruby, a pattern switch)
        m_rev = rev;
        int oldShown = m_haveView ? m_view->shown : -1;
        if (!refresh(true)) return;
        if (m_follow && oldShown >= 0 && m_view->shown != oldShown) {
            m_window = 0;
            refresh(true);
        }
        computeGeom();
        clampScroll();
        if (m_chainSheet) loadChain();
        for (int l = 0; l < SEQ_MAX_LINES; l++) m_playCol[l] = playColOf(l);
        m_lastPlay = m_view->play;
        m_lastNext = m_view->next;
        m_lastEdit = m_view->edit;
        m_lastRunning = m_view->running;
        m_lastChain = m_view->chainOn;
        m_lastUsed = m_view->used;
        paintAll();
        return;
    }

    uint32_t chordSerial = m_view->chordSerial;
    refresh(false);
    follow();

    const ui_seq::View& v = *m_view;
    static bool s_blink;
    bool blink = (nowUs() / BLINK_US) % 2 == 0;
    bool headerDirty = v.play != m_lastPlay || v.next != m_lastNext || v.edit != m_lastEdit ||
                       v.running != m_lastRunning || v.chainOn != m_lastChain || v.used != m_lastUsed ||
                       v.chordSerial != chordSerial ||
                       (v.next >= 0 && blink != s_blink);
    s_blink = blink;
    if (headerDirty) {
        m_lastPlay = v.play;
        m_lastNext = v.next;
        m_lastEdit = v.edit;
        m_lastRunning = v.running;
        m_lastChain = v.chainOn;
        m_lastUsed = v.used;
        paintHeader();
    }

    // The File button: unsaved mark, messages from saving
    bool dirty = ui_seq::dirty();
    char status[sizeof(m_status)];
    if (!ui_seq::status(status, sizeof(status))) status[0] = '\0';
    if (dirty != m_dirty || strcmp(status, m_status) != 0) {
        m_dirty = dirty;
        strcpy(m_status, status);
        paintHeader();
    }

    if (m_fileSheet) {
        if (ui_seq::files_serial() != m_filesSerial) {
            takeFiles();
            paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
        }
        return;
    }
    if (m_sheetLine >= 0) return;
    for (int l = 0; l < v.lineCount; l++) {
        int col = playColOf(l);
        if (col == m_playCol[l]) continue;
        int old = m_playCol[l];
        m_playCol[l] = (int16_t)col;
        paintCell(l, old);
        paintCell(l, col);
    }
}

// --- Actions -----------------------------------------------------------------

void ScreenSequencer::pressButton(int id)
{
    const ui_seq::View& v = *m_view;
    if (id >= BTN_SLOT0 && id < BTN_SLOT0 + SEQ_MAX_PATTERNS) {
        ui_seq_set_i(-1, -1, "edit_pattern", -1);
        ui_seq_set_i(-1, -1, "play_pattern", id - BTN_SLOT0);
        return;
    }
    switch (id) {
        case BTN_PAT_PREV:
        case BTN_PAT_NEXT: {
            int p = v.next >= 0 ? v.next : v.shown;
            p = (p + (id == BTN_PAT_NEXT ? 1 : SEQ_MAX_PATTERNS - 1)) % SEQ_MAX_PATTERNS;
            ui_seq_set_i(-1, -1, "edit_pattern", -1);
            ui_seq_set_i(-1, -1, "play_pattern", p);
            break;
        }
        case BTN_CHAIN:
            ui_seq_set_i(-1, -1, "chain_on", v.chainOn ? 0 : 1);
            break;
        case BTN_CHORD:
            ui_seq::preview_chord(m_selected);
            break;
        case BTN_WIN_PREV:
        case BTN_WIN_NEXT:
            m_follow = false;
            moveWindow((id == BTN_WIN_NEXT ? 1 : -1) * (m_viewSteps / 2));
            break;
        case BTN_VIEW: {
            m_viewSteps = m_viewSteps == 16 ? 8 : 16;
            int half = m_viewSteps / 2;
            m_window = (m_window / half) * half;
            refresh(true);
            for (int i = 0; i < SEQ_MAX_LINES; i++) m_playCol[i] = playColOf(i);
            paintAll();
            break;
        }
        case BTN_FOLLOW:
            m_follow = !m_follow;
            paintHeader();
            break;
        case BTN_FILE:
            if (m_fileSheet) {
                m_fileSheet = false;
                closeSheet();
            } else {
                openFiles();
            }
            break;
        case BTN_PLAY: {
            // The Tempo's Play / Stop when the clock comes from the Tempo (its
            // other destinations start and stop along with it); with another
            // clock, Start / Stop to the sequencer alone, which then follows
            // that clock's F8s.
            uint8_t clock = ui_seq::ensure_clock();
            bool fromTempo = clock && clock == ui_tempo_bit();
            if (v.running) {
                if (fromTempo) ui_tempo_set_running(false);
                ui_seq_receive(0xF, 0xFC, 0, 0);
            } else if (fromTempo) {
                ui_tempo_set_running(true);
            } else {
                ui_seq_receive(0xF, 0xFA, 0, 0);
            }
            break;
        }
        default:
            break;
    }
}

// A slot (or the narrow screens' pattern label) held down: edit that pattern
// while another plays, or go back to following the playing one
void ScreenSequencer::longPressButton(int id)
{
    if (id >= BTN_SLOT0 && id < BTN_SLOT0 + SEQ_MAX_PATTERNS) {
        openPatternMenu(id - BTN_SLOT0);
    } else if (id == BTN_PAT) {
        openPatternMenu(m_view->shown);
    } else if (id == BTN_CHAIN) {
        openChain();
    }
}

// --- Pattern menu, chain sheet ------------------------------------------------

void ScreenSequencer::openPatternMenu(int pattern)
{
    closeSheets();
    m_patMenu = pattern;
    paintAll();
}

void ScreenSequencer::pressPattern(int id)
{
    int p = m_patMenu;
    bool used = (m_view->used >> p) & 1;
    int confirmed = m_patConfirm;
    m_patConfirm = -1;
    switch (id) {
        case PM_EDIT:
            // Edit this pattern while another plays, or follow the playing one again
            ui_seq_set_i(-1, -1, "edit_pattern", m_view->edit == p ? -1 : p);
            closeSheet();
            return;
        case PM_COPY:
            m_patClip = p;
            ui_seq_set_status("Pattern copied");
            break;
        case PM_PASTE:
            if (m_patClip < 0 || m_patClip == p) break;
            if (used && confirmed != PM_PASTE) {
                m_patConfirm = PM_PASTE;
                break;
            }
            if (!ui_seq_copy_pattern(m_patClip, p)) ui_seq_set_status("Too many notes");
            break;
        case PM_CLEAR:
            if (!used) break;
            if (confirmed != PM_CLEAR) {
                m_patConfirm = PM_CLEAR;
                break;
            }
            ui_seq_clear_pattern(p);
            break;
        case PM_LENGTH: {
            static const int LENGTHS[] = { 0, 1, 2, 4, 8 };
            ui_seq_set_i(p, -1, "length", nextChoice(LENGTHS, 5, ui_seq_get_i(p, -1, "length")));
            break;
        }
        case PM_CLOSE:
            closeSheet();
            return;
        default:
            return;
    }
    paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
}

void ScreenSequencer::loadChain()
{
    m_chainN = ui_seq_get_chain(m_chainP, m_chainR, SEQ_MAX_CHAIN);
    if (m_chainSel >= m_chainN) m_chainSel = m_chainN - 1;
    int rows = m_chainList.h / m_fileRowH;
    if (rows < 1) rows = 1;
    if (m_chainSel >= 0 && m_chainSel < m_chainScroll) m_chainScroll = m_chainSel;
    if (m_chainSel >= m_chainScroll + rows) m_chainScroll = m_chainSel - rows + 1;
    int maxScroll = m_chainN - rows;
    if (m_chainScroll > maxScroll) m_chainScroll = maxScroll > 0 ? maxScroll : 0;
}

void ScreenSequencer::openChain()
{
    closeSheets();
    m_chainSheet = true;
    m_chainSel = -1;
    m_chainScroll = 0;
    loadChain();
    paintAll();
}

void ScreenSequencer::pressChain(int id)
{
    bool confirmed = m_chainConfirm;
    m_chainConfirm = false;
    bool sel = m_chainSel >= 0 && m_chainSel < m_chainN;
    int i = m_chainSel;
    bool changed = false;
    switch (id) {
        case CB_ADD: {
            if (m_chainN >= SEQ_MAX_CHAIN) break;
            int at = sel ? i + 1 : m_chainN;
            memmove(&m_chainP[at + 1], &m_chainP[at], (size_t)(m_chainN - at));
            memmove(&m_chainR[at + 1], &m_chainR[at], (size_t)(m_chainN - at));
            m_chainP[at] = (uint8_t)(m_view->shown >= 0 ? m_view->shown : 0);
            m_chainR[at] = 1;
            m_chainN++;
            m_chainSel = at;
            changed = true;
            break;
        }
        case CB_DEL:
            if (!sel) break;
            memmove(&m_chainP[i], &m_chainP[i + 1], (size_t)(m_chainN - i - 1));
            memmove(&m_chainR[i], &m_chainR[i + 1], (size_t)(m_chainN - i - 1));
            m_chainN--;
            if (m_chainSel >= m_chainN) m_chainSel = m_chainN - 1;
            changed = true;
            break;
        case CB_PAT_DN:
        case CB_PAT_UP:
            if (!sel) break;
            m_chainP[i] = (uint8_t)((m_chainP[i] + (id == CB_PAT_UP ? 1 : SEQ_MAX_PATTERNS - 1)) % SEQ_MAX_PATTERNS);
            changed = true;
            break;
        case CB_REP_DN:
        case CB_REP_UP: {
            if (!sel) break;
            int r = m_chainR[i] + (id == CB_REP_UP ? 1 : -1);
            if (r < 1 || r > SEQ_MAX_REPEAT) break;
            m_chainR[i] = (uint8_t)r;
            changed = true;
            break;
        }
        case CB_UP:
        case CB_DOWN: {
            if (!sel) break;
            int j = id == CB_UP ? i - 1 : i + 1;
            if (j < 0 || j >= m_chainN) break;
            uint8_t tp = m_chainP[i], tr = m_chainR[i];
            m_chainP[i] = m_chainP[j];
            m_chainR[i] = m_chainR[j];
            m_chainP[j] = tp;
            m_chainR[j] = tr;
            m_chainSel = j;
            changed = true;
            break;
        }
        case CB_CLEAR:
            if (m_chainN == 0) break;
            if (!confirmed) {
                m_chainConfirm = true;
                break;
            }
            m_chainN = 0;
            m_chainSel = -1;
            changed = true;
            break;
        case CB_ON:
            ui_seq_set_i(-1, -1, "chain_on", m_view->chainOn ? 0 : 1);
            break;
        case CB_CLOSE:
            closeSheet();
            return;
        default:
            return;
    }
    if (changed) ui_seq_set_chain(m_chainP, m_chainR, m_chainN);
    loadChain();
    paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
}

void ScreenSequencer::closeSheets()
{
    m_sheetLine = -1;
    m_fileSheet = false;
    m_patMenu = -1;
    m_chainSheet = false;
    m_confirmDelete = false;
    m_confirmPaste = false;
    m_patConfirm = -1;
    m_chainConfirm = false;
    m_fileConfirm = -1;
}

void ScreenSequencer::openSheet(int line)
{
    closeSheets();
    m_sheetLine = line;
    m_selected = line;
    m_confirmDelete = false;
    paintAll();
}

void ScreenSequencer::closeSheet()
{
    closeSheets();
    refresh(true);
    computeGeom();
    clampScroll();
    paintAll();
}

void ScreenSequencer::pressSheet(int id)
{
    int line = m_sheetLine;
    int p = UI_SEQ_PATTERN_EDIT;
    if (id != SB_DELETE) m_confirmDelete = false;
    bool pasteConfirmed = m_confirmPaste;
    m_confirmPaste = false;
    switch (id) {
        case SB_MODE:
            ui_seq_set_i(p, line, "mode", ui_seq_get_i(p, line, "mode") == SEQ_MODE_GRID
                                              ? SEQ_MODE_REALTIME : SEQ_MODE_GRID);
            break;
        case SB_CH_DN:
        case SB_CH_UP: {
            int ch = ui_seq_get_i(p, line, "channel") + (id == SB_CH_UP ? 1 : 15);
            ui_seq_set_i(p, line, "channel", ch % 16);
            break;
        }
        case SB_STEPS:
            ui_seq_set_i(p, line, "steps", nextChoice(STEP_CHOICES, 5, ui_seq_get_i(p, line, "steps")));
            break;
        case SB_RATE:
            ui_seq_set_i(p, line, "rate", (ui_seq_get_i(p, line, "rate") + 1) % SEQ_RATE_COUNT);
            break;
        case SB_BARS:
            ui_seq_set_i(p, line, "bars", nextChoice(BAR_CHOICES, 4, ui_seq_get_i(p, line, "bars")));
            break;
        case SB_MUTE:
            ui_seq_set_i(p, line, "mute", ui_seq_get_i(p, line, "mute") ? 0 : 1);
            break;
        case SB_GATE_DN:
        case SB_GATE_UP: {
            int gate = ui_seq_get_i(p, line, "gate") + (id == SB_GATE_UP ? 10 : -10);
            if (gate > 100) gate = 100;
            if (gate < 10) gate = 10;
            ui_seq_set_i(p, line, "gate", gate);
            break;
        }
        case SB_CLEAR:
            ui_seq_clear(p, line, ui_seq_get_i(p, line, "mode"));
            break;
        case SB_DELETE:
            if (m_haveView && m_view->lineCount <= 1) break;
            if (!m_confirmDelete) {
                m_confirmDelete = true;
                break;
            }
            ui_seq_remove_line(line);
            if (m_selected >= line && m_selected > 0) m_selected--;
            closeSheet();
            return;
        case SB_COPY:
            m_lineClipPat = m_haveView ? m_view->shown : 0;
            m_lineClipLine = line;
            ui_seq_set_status("Line copied");
            break;
        case SB_PASTE:
            if (m_lineClipLine < 0 || (m_lineClipLine == line && m_haveView && m_lineClipPat == m_view->shown)) break;
            if (!pasteConfirmed) {
                m_confirmPaste = true;
                break;
            }
            if (!ui_seq_copy_line(m_lineClipPat, m_lineClipLine, p, line)) ui_seq_set_status("Too many notes");
            break;
        case SB_CLOSE:
            closeSheet();
            return;
        default:
            return;
    }
    // Most changes show up through the song's revision; this covers the rest
    paintAll();
}

// --- File sheet --------------------------------------------------------------

void ScreenSequencer::takeFiles()
{
    m_filesSerial = ui_seq::files_serial();
    if (!m_files) {
        m_files = (char (*)[SEQ_SONG_NAME_LEN + 1])calloc(MAX_FILES, SEQ_SONG_NAME_LEN + 1);
        if (!m_files) {
            m_fileCount = 0;
            return;
        }
    }
    int n = ui_seq::file_count();
    if (n > MAX_FILES) n = MAX_FILES;
    m_fileCount = 0;
    for (int i = 0; i < n; i++) {
        if (ui_seq::file_name(i, m_files[m_fileCount], SEQ_SONG_NAME_LEN + 1)) m_fileCount++;
    }
    if (m_fileSel >= m_fileCount) m_fileSel = -1;
    clampFileScroll();
}

void ScreenSequencer::clampFileScroll()
{
    int maxScroll = m_fileCount - m_fileRows;
    if (maxScroll < 0) maxScroll = 0;
    if (m_fileScroll > maxScroll) m_fileScroll = maxScroll;
    if (m_fileScroll < 0) m_fileScroll = 0;
}

void ScreenSequencer::openFiles()
{
    closeSheets();
    m_fileSheet = true;
    m_fileConfirm = -1;
    takeFiles();
    // The current song selected, so Load / Delete have a target at once
    m_fileSel = -1;
    for (int i = 0; m_haveView && i < m_fileCount; i++) {
        if (strcmp(m_files[i], m_view->name) == 0) m_fileSel = i;
    }
    ui_seq::request(UI_SEQ_REQ_LIST, "");
    paintAll();
}

void ScreenSequencer::pressFile(int id)
{
    bool sel = m_fileSel >= 0 && m_fileSel < m_fileCount;
    bool confirmed = m_fileConfirm == id;
    m_fileConfirm = -1;
    switch (id) {
        case FB_SAVE:
            ui_seq::request(UI_SEQ_REQ_SAVE, m_haveView ? m_view->name : "");
            break;
        case FB_SAVE_NEW:
            ui_seq::request(UI_SEQ_REQ_SAVE_NEW, "");
            break;
        case FB_LOAD:
        case FB_NEW:
            if (id == FB_LOAD && !sel) break;
            // Unsaved changes: a second tap to go ahead (the autosave still has them)
            if (m_dirty && !confirmed) {
                m_fileConfirm = id;
                break;
            }
            ui_seq::request(id == FB_LOAD ? UI_SEQ_REQ_LOAD : UI_SEQ_REQ_NEW,
                            id == FB_LOAD ? m_files[m_fileSel] : "");
            closeSheet();
            return;
        case FB_DELETE:
            if (!sel) break;
            if (!confirmed) {
                m_fileConfirm = id;
                break;
            }
            ui_seq::request(UI_SEQ_REQ_DELETE, m_files[m_fileSel]);
            m_fileSel = -1;
            break;
        case FB_CLOSE:
            closeSheet();
            return;
        default:
            return;
    }
    paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
}

// --- Touch -------------------------------------------------------------------

// Buttons that act on release, so a long press can mean something else
bool ScreenSequencer::holdsForLongPress(int id)
{
    return (id >= BTN_SLOT0 && id < BTN_SLOT0 + SEQ_MAX_PATTERNS) || id == BTN_PAT || id == BTN_CHAIN;
}

void ScreenSequencer::checkLongPress()
{
    int64_t now = nowUs();
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        Touch& t = m_touch[i];
        if (t.kind == T_NONE || t.moved || t.longFired || now - t.t0 < LONG_PRESS_US) continue;
        if (t.kind == T_BUTTON && holdsForLongPress(t.target)) {
            t.longFired = true;
            longPressButton(t.target);
        } else if (t.kind == T_LINE) {
            t.longFired = true;
            openSheet(t.target);
        } else if (t.kind == T_CELL && t.onNote) {
            t.longFired = true;
            ui_seq::replace_with_chord(t.target, t.step);
        }
    }
}

void ScreenSequencer::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive || !m_haveView || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    Touch& t = m_touch[touchId];

    if (pressed) {
        memset(&t, 0, sizeof(t));
        t.x0 = x;
        t.y0 = y;
        t.lastY = y;
        t.t0 = nowUs();
        t.kind = T_IGNORE;
        if (x < m_areaX || x >= m_areaX + m_areaW || y < m_areaY || y >= m_areaY + m_areaH) return;

        for (int b = 0; b < BTN_COUNT; b++) {
            if (!m_btn[b].has(x, y)) continue;
            t.kind = T_BUTTON;
            t.target = (int8_t)b;
            if (!holdsForLongPress(b)) pressButton(b);
            return;
        }
        if (m_patMenu >= 0) {
            for (int b = 0; b < PM_COUNT; b++) {
                if (!m_patBtn[b].has(x, y)) continue;
                t.kind = T_PAT_BTN;
                pressPattern(b);
                return;
            }
            return;
        }
        if (m_chainSheet) {
            for (int b = 0; b < CB_COUNT; b++) {
                if (!m_chainBtn[b].has(x, y)) continue;
                t.kind = T_CHAIN_BTN;
                pressChain(b);
                return;
            }
            if (m_chainList.has(x, y)) t.kind = T_CHAIN_ROW;
            return;
        }
        if (m_fileSheet) {
            for (int b = 0; b < FB_COUNT; b++) {
                if (!m_fileBtn[b].has(x, y)) continue;
                t.kind = T_FILE_BTN;
                t.target = (int8_t)b;
                pressFile(b);
                return;
            }
            if (m_fileList.has(x, y)) t.kind = T_FILE_ROW;
            return;
        }
        if (m_sheetLine >= 0) {
            for (int b = 0; b < SB_COUNT; b++) {
                if (!m_sheetBtn[b].has(x, y)) continue;
                t.kind = T_SHEET;
                t.target = (int8_t)b;
                pressSheet(b);
                return;
            }
            return;
        }

        int row = lineAtRow(y);
        if (row < 0) return;
        if (row >= m_view->lineCount) {
            t.kind = T_PLUS;
            return;
        }
        const ui_seq::LineView& lv = m_view->lines[row];
        t.target = (int8_t)row;
        if (x < m_g.cellX || lv.mode != SEQ_MODE_GRID || lv.cols == 0) {
            t.kind = T_LINE;
            return;
        }
        int col = colAt(x, lv.shown);
        t.kind = T_CELL;
        t.startCol = t.curCol = (int8_t)col;
        t.step = (int8_t)((lv.s0 + col) % lv.cols);
        t.onNote = ui_seq::starting_at(row, t.step, nullptr) > 0;
        if (!t.onNote) paintRow(row);   // The new note's outline
        return;
    }

    // Released
    TouchKind kind = t.kind;
    t.kind = T_NONE;
    switch (kind) {
        case T_BUTTON:
            if (holdsForLongPress(t.target) && !t.longFired && !t.moved) pressButton(t.target);
            break;
        case T_CELL: {
            if (t.longFired) break;
            int len = t.curCol - t.startCol + 1;
            if (t.onNote) {
                if (t.moved) {
                    ui_seq_set_length(UI_SEQ_PATTERN_EDIT, t.target, t.step, len);
                } else {
                    ui_seq_remove(UI_SEQ_PATTERN_EDIT, t.target, t.step, -1);
                }
            } else {
                ui_seq::place_chord(t.target, t.step, len);
            }
            m_selected = t.target;
            paintRow(t.target);   // Without the outline, even if nothing changed
            break;
        }
        case T_LINE: {
            if (t.moved || t.longFired) break;
            int mx, my, ms;
            muteRect(m_areaX, m_g.nameW, rowY(t.target), m_g.rowH, mx, my, ms);
            if (t.x0 >= mx - 6 && t.x0 < mx + ms + 6 && t.x0 < m_g.cellX) {
                ui_seq_set_i(UI_SEQ_PATTERN_EDIT, t.target, "mute", m_view->lines[t.target].mute ? 0 : 1);
            } else if (m_selected != t.target) {
                m_selected = t.target;
                refresh(true);
                paintAll();
            }
            break;
        }
        case T_CHAIN_ROW:
            if (!t.moved) {
                int i = m_chainScroll + (t.y0 - m_chainList.y) / m_fileRowH;
                m_chainSel = i < m_chainN ? i : -1;
                m_chainConfirm = false;
                paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
            }
            break;
        case T_FILE_ROW:
            if (!t.moved) {
                int i = m_fileScroll + (t.y0 - m_fileList.y) / m_fileRowH;
                m_fileSel = i < m_fileCount ? i : -1;
                m_fileConfirm = -1;
                paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
            }
            break;
        case T_PLUS:
            if (!t.moved) {
                int index = ui_seq_add_line();
                if (index >= 0) m_selected = index;
            }
            break;
        default:
            break;
    }
}

void ScreenSequencer::onTouchMove(int touchId, int x, int y)
{
    if (!m_isActive || !m_haveView || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    Touch& t = m_touch[touchId];
    int dx = x - t.x0, dy = y - t.y0;
    bool far = dx > MOVE_SLOP || dx < -MOVE_SLOP || dy > MOVE_SLOP || dy < -MOVE_SLOP;

    switch (t.kind) {
        case T_CELL: {
            if (t.target >= m_view->lineCount) return;
            const ui_seq::LineView& lv = m_view->lines[t.target];
            int col = colAt(x, lv.shown);
            // Not before where it starts, not past the screen's right edge or the line's end
            int maxCol = t.startCol + (lv.cols - 1 - t.step);
            if (maxCol > lv.shown - 1) maxCol = lv.shown - 1;
            if (col > maxCol) col = maxCol;
            if (col < t.startCol) col = t.startCol;
            if (col != t.curCol) {
                t.curCol = (int8_t)col;
                t.moved = true;
                paintRow(t.target);
            }
            break;
        }
        case T_LINE:
        case T_PLUS: {
            if (far) t.moved = true;
            if (m_g.total <= m_g.rows) break;
            int steps = (t.lastY - y) / m_g.rowH;
            if (steps != 0 && t.moved) {
                t.lastY -= steps * m_g.rowH;
                int before = m_scroll;
                m_scroll += steps;
                clampScroll();
                if (m_scroll != before) paintAll();
            }
            break;
        }
        case T_CHAIN_ROW: {
            if (far) t.moved = true;
            int steps = (t.lastY - y) / m_fileRowH;
            if (steps != 0 && t.moved) {
                t.lastY -= steps * m_fileRowH;
                int rows = m_chainList.h / m_fileRowH;
                int maxScroll = m_chainN - (rows > 0 ? rows : 1);
                int before = m_chainScroll;
                m_chainScroll += steps;
                if (m_chainScroll > maxScroll) m_chainScroll = maxScroll;
                if (m_chainScroll < 0) m_chainScroll = 0;
                if (m_chainScroll != before) paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
            }
            break;
        }
        case T_FILE_ROW: {
            if (far) t.moved = true;
            int steps = (t.lastY - y) / m_fileRowH;
            if (steps != 0 && t.moved) {
                t.lastY -= steps * m_fileRowH;
                int before = m_fileScroll;
                m_fileScroll += steps;
                clampFileScroll();
                if (m_fileScroll != before) paint(m_areaX, m_g.linesY, m_areaW, m_g.linesH);
            }
            break;
        }
        case T_BUTTON:
            if (far) t.moved = true;
            break;
        default:
            break;
    }
}

const char* ScreenSequencer::getTitle()
{
    return "Sequencer";
}

void ScreenSequencer::getTileStatus(char* buf, int len)
{
    int p = ui_seq_get_i(-1, -1, "play_pattern");
    bool running = ui_seq_get_i(-1, -1, "running") != 0;
    snprintf(buf, len, "P%d%s", p + 1, running ? " >" : "");
}
