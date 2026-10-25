#ifndef SCREEN_SEQUENCER_H
#define SCREEN_SEQUENCER_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>
#include "ui_sequencer.h"

// The Sequencer screen (docs/SEQUENCER.md): a header (pattern slots, the
// steps on screen, view / Follow / Chain, the chord), then one row per line
// -- its name and settings on the left, its steps on the right -- and a
// "+ Line" row while there are fewer than 16. Long-pressing a line's name
// opens its settings sheet in place of the rows.
//
// Drawing reads a ui_seq::View copied once per frame (ui_sequencer.h), never
// the engine, and goes through the shared strip (ui_strip.h). A change to
// the song repaints everything; the play position only the cells it leaves
// and enters.
class ScreenSequencer : public Screen {
public:
    ScreenSequencer();
    ~ScreenSequencer() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    const char* getTitle() override;
    void getTileStatus(char* buf, int len) override;

    // The whole content area by default; a layout narrows it to its widget's
    // cell while it shows, and puts it back (resetArea) afterwards.
    void setArea(int x, int y, int w, int h);
    void resetArea();

private:
    struct Rect {
        int x, y, w, h;
        bool has(int px, int py) const { return w > 0 && px >= x && px < x + w && py >= y && py < y + h; }
    };

    enum Button {
        BTN_SLOT0 = 0,                  // .. BTN_SLOT0 + 15 (wide screens)
        BTN_PAT_PREV = SEQ_MAX_PATTERNS,  // Narrow screens: < P3 >
        BTN_PAT,
        BTN_PAT_NEXT,
        BTN_CHAIN,
        BTN_CHORD,
        BTN_WIN_PREV,
        BTN_WIN,
        BTN_WIN_NEXT,
        BTN_VIEW,
        BTN_FOLLOW,
        BTN_PLAY,
        BTN_FILE,                       // Song name; opens the File sheet
        BTN_COUNT,
    };

    enum SheetButton {
        SB_MODE = 0, SB_CH_DN, SB_CH, SB_CH_UP,
        SB_STEPS, SB_RATE, SB_BARS, SB_MUTE,
        SB_GATE_DN, SB_GATE, SB_GATE_UP, SB_CLEAR,
        SB_DELETE, SB_COPY, SB_PASTE, SB_CLOSE,
        SB_COUNT,
    };

    enum FileButton {
        FB_SAVE = 0, FB_SAVE_NEW, FB_LOAD, FB_NEW, FB_DELETE, FB_CLOSE,
        FB_COUNT,
    };
    static constexpr int MAX_FILES = 64;

    // A pattern slot held down
    enum PatternButton {
        PM_EDIT = 0, PM_COPY, PM_PASTE, PM_CLEAR, PM_LENGTH, PM_CLOSE,
        PM_COUNT,
    };

    // The chain sheet (Chain held down)
    enum ChainButton {
        CB_ADD = 0, CB_DEL, CB_PAT_DN, CB_PAT_UP, CB_REP_DN, CB_REP_UP,
        CB_UP, CB_DOWN, CB_CLEAR, CB_ON, CB_CLOSE,
        CB_COUNT,
    };

    enum TouchKind : uint8_t {
        T_NONE = 0,
        T_BUTTON,      // A header button
        T_CELL,        // A grid cell: place / lengthen / remove
        T_LINE,        // A line's name: select, mute, settings, scroll
        T_PLUS,        // "+ Line"
        T_FILE_ROW,    // A song in the File sheet's list (select, scroll)
        T_FILE_BTN,    // A File sheet button
        T_PAT_BTN,     // A pattern menu button
        T_CHAIN_ROW,   // An entry in the chain sheet's list
        T_CHAIN_BTN,   // A chain sheet button
        T_SHEET,       // A settings sheet button
        T_IGNORE,      // Landed on nothing; held until lifted
    };

    struct Touch {
        TouchKind kind;
        int8_t target;       // Button / sheet button / line
        int8_t startCol, curCol;
        int8_t step;         // Grid step pressed
        bool onNote;         // Notes start at that step
        bool moved;          // Dragged to another column (or scrolled)
        bool longFired;
        int x0, y0, lastY;
        int64_t t0;
    };

    struct Geom {
        int textSize;
        int headerH, headerRowH;
        int linesY, linesH;
        int nameW;
        int cellX, cellW;
        int rowH, rows;      // Row height, rows that fit
        int total;           // Lines plus the "+ Line" row
    };

    int m_areaX, m_areaY, m_areaW, m_areaH;
    bool m_isActive;
    ui_seq::View* m_view;        // Heap (UI task stack is small)
    bool m_haveView;
    uint32_t m_rev;
    int m_window;                // Step offset of what is on screen
    int m_viewSteps;             // 8 or 16
    bool m_follow;
    int m_selected;
    int m_scroll;                // First line row on screen
    int m_sheetLine;             // Line whose settings show, or -1
    bool m_confirmDelete;
    int16_t m_playCol[SEQ_MAX_LINES];
    int8_t m_lastPlay, m_lastNext, m_lastEdit;
    bool m_lastRunning, m_lastChain;
    uint16_t m_lastUsed;
    Geom m_g;
    Rect m_btn[BTN_COUNT];
    Rect m_sheetBtn[SB_COUNT];
    Touch m_touch[MAX_TOUCH_POINTS];
    Rect m_clip;                 // What paint() is repainting: render skips the rest

    // File sheet (saving is Ruby's: the buttons queue requests, ui_seq::request)
    bool m_fileSheet;
    int m_fileSel;               // Selected song, or -1
    int m_fileScroll;
    int m_fileConfirm;           // FileButton waiting for a second tap, or -1
    int m_fileCount;
    uint32_t m_filesSerial;
    char (*m_files)[SEQ_SONG_NAME_LEN + 1];   // Heap, MAX_FILES
    Rect m_fileBtn[FB_COUNT];
    Rect m_fileList;
    int m_fileRowH, m_fileRows;
    // Pattern menu, chain sheet, clipboards
    int m_patMenu;               // Pattern whose menu shows, or -1
    int m_patConfirm;            // PatternButton waiting for a second tap, or -1
    int m_patClip;               // Pattern Copy marked, or -1 (pasted as it is then)
    Rect m_patBtn[PM_COUNT];
    bool m_chainSheet;
    int m_chainSel;
    int m_chainScroll;
    bool m_chainConfirm;         // Clear waiting for a second tap
    int m_chainN;
    uint8_t m_chainP[SEQ_MAX_CHAIN], m_chainR[SEQ_MAX_CHAIN];
    Rect m_chainBtn[CB_COUNT];
    Rect m_chainList;
    int m_lineClipPat, m_lineClipLine;   // Line Copy marked, or -1
    bool m_confirmPaste;
    bool m_dirty;                // Shown on the File button
    char m_status[32];           // Fresh status message, or ""

    void computeGeom();
    bool refresh(bool notes);
    void clampScroll();
    int rowY(int row) const;
    int lineAtRow(int y) const;        // Row index on screen at y, or -1
    void cellRect(int row, int col, int shown, Rect& r) const;
    int colAt(int x, int shown) const;
    int playColOf(int line) const;
    int unitTicks(int line) const;

    void paint(int x, int y, int w, int h);
    void paintAll();
    void paintHeader();
    void paintRow(int row);
    void paintCell(int line, int col);
    void render(LovyanGFX* g, int ox, int oy);
    void renderHeader(LovyanGFX* g, int ox, int oy);
    void renderLine(LovyanGFX* g, int ox, int oy, int line, int y);
    void renderPlus(LovyanGFX* g, int ox, int oy, int y);
    void renderSheet(LovyanGFX* g, int ox, int oy);
    void renderFiles(LovyanGFX* g, int ox, int oy);
    bool sheetOpen() const { return m_sheetLine >= 0 || m_fileSheet || m_patMenu >= 0 || m_chainSheet; }
    void closeSheets();          // Every sheet shut (no repaint)
    void renderPatternMenu(LovyanGFX* g, int ox, int oy);
    void renderChain(LovyanGFX* g, int ox, int oy);
    void openPatternMenu(int pattern);
    void pressPattern(int id);
    void openChain();
    void loadChain();
    void pressChain(int id);
    void openFiles();
    void pressFile(int id);
    void takeFiles();
    void clampFileScroll();
    void button(LovyanGFX* g, int ox, int oy, const Rect& r, const char* label,
                uint16_t bg, uint16_t fg, bool outline = false);

    void pressButton(int id);
    void longPressButton(int id);
    void pressSheet(int id);
    void openSheet(int line);
    void closeSheet();
    void moveWindow(int delta);
    void follow();
    void checkLongPress();
    static bool holdsForLongPress(int id);
};

ScreenSequencer& getScreenSequencer();

#endif // __cplusplus

#endif // SCREEN_SEQUENCER_H
