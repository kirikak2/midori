#ifndef SCREEN_MIDI_DEVICES_H
#define SCREEN_MIDI_DEVICES_H

#include "ui_manager.h"

#ifdef __cplusplus

// MIDI Devices: a crossbar of the registered devices (docs/UI_MIDI_DEVICES.md).
// Rows are MIDI INs, columns MIDI OUTs; a cell is a route (MIDI.route). The
// script is the "Script" device (its midi_out is its row, its midi_in its
// column). A long press on a cell
// opens its picker: the channels it passes, or (Filters tab) the named
// filters chained on it and their parameters (docs/MIDI_FILTER_REGISTRY.md).
// Rows and columns that do not fit scroll (drag); the corner button shows all
// of them, only the patched ones or only the unpatched ones.
class ScreenMidiDevices : public Screen {
public:
    ScreenMidiDevices();
    ~ScreenMidiDevices() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    const char* getTitle() override;
    void getTileStatus(char* buf, int len) override;

    static constexpr int MAX_DEVS = UI_MIDIDEV_MAX;

private:
    enum Overlay { OVERLAY_NONE = 0, OVERLAY_CHANNEL, OVERLAY_DETAIL, OVERLAY_PARAMS };
    enum PickTab { TAB_CHANNEL = 0, TAB_FILTERS };
    // Which rows / columns the grid shows: every device, those with a patch
    // (a row: from it, a column: to it), or those with none
    enum View { VIEW_ALL = 0, VIEW_PATCHED, VIEW_UNPATCHED, VIEW_COUNT };

    // A button of the Filters tab: a registered filter, or one a chain names
    // that is not registered (shown grey, can only be taken off)
    struct FilterItem {
        char name[UI_FILTER_NAME_LEN];
        char label[UI_FILTER_LABEL_LEN];
        bool registered;
    };
    static constexpr int FILTER_ITEMS = UI_FILTER_MAX + UI_FILTER_CHAIN_MAX;

    // The picker's working copies, on the heap: the UI task's stack is small
    // and this screen is a static object (docs/MEMORY_ALLOCATION.md)
    struct Scratch {
        ui_filter_chain_t chain;   // The patch's chain as last loaded / edited
        ui_filter_def_t def;       // The filter on the parameter page
        FilterItem items[FILTER_ITEMS];
        int itemCount;
    };

    // What the grid shows, rebuilt from the model when it changes. On the
    // heap with the screen's other working state (docs/MEMORY_ALLOCATION.md).
    struct Grid {
        ui_mididev_t devs[MAX_DEVS];
        int count;
        uint32_t sel[MAX_DEVS][MAX_DEVS];   // What each patch passes (0: none), by device
        int rows[MAX_DEVS];     // Device indices with a MIDI IN, as the view filters them
        int rowCount;
        int cols[MAX_DEVS];     // Device indices with a MIDI OUT, as the view filters them
        int colCount;
        int x0, y0;             // Top-left cell
        int cellW, cellH;
        int nameW, headerH;
        int shownRows, shownCols;   // Device rows / columns on the screen at once
        int rowOff, colOff;         // The first of them (scrolled)
    };

    bool m_isActive;
    bool m_needsRedraw;
    uint32_t m_lastCheckMs;
    uint32_t m_lastDiagMs;
    uint32_t m_signature;       // Model + routes, to notice changes
    Grid* m_grid;
    View m_view;
    // Devices patched / unpatched on this screen since the view was chosen:
    // they stay in it (bits by device index) rather than vanish under the finger
    uint32_t m_keepRows, m_keepCols;
    int m_keepCount;            // The device count they were taken with

    // Touch: one finger at a time; a hold is a long press
    int m_touchId;
    int m_touchX, m_touchY;
    uint32_t m_touchMs;
    bool m_longFired;
    bool m_dragging;            // Scrolling the grid
    int m_dragRowOff, m_dragColOff;   // The offsets when the drag began

    Overlay m_overlay;
    int m_overlayFrom, m_overlayTo;   // OVERLAY_CHANNEL: device indices
    int m_overlayDev;                 // OVERLAY_DETAIL
    PickTab m_pickTab;                // Kept between pickers
    Scratch* m_scratch;
    int m_paramStage;                 // OVERLAY_PARAMS: the chain position edited
    int m_paramPage;
    int m_repeatHit;                  // A held -/+ on the parameter page, -1 none
    uint32_t m_repeatMs;

    void rebuild();
    uint32_t signature() const;

    // Hit testing. Returns false outside the grid. row / col are positions in
    // rows / cols (scrolling taken into account); -1 a header.
    bool hit(int x, int y, int* row, int* col) const;
    bool cornerHit(int x, int y) const;    // The view button
    int  rowY(int row) const;              // Top of row (a position in rows)
    int  colX(int col) const;              // Left of col (... in cols)
    void keep(int from, int to);           // A patch changed here: keep its row / column
    void setView(View view);
    void tap(int x, int y);
    void longPress(int x, int y);

    void drawGrid();
    void drawCell(int row, int col);
    void drawViewButton();
    void drawScrollbars();
    void drawChannelPicker();
    void drawPickerHeader(int px, int py, int pw, bool tabs);
    void drawFilterButtons(int px, int py);
    void drawParams();
    void drawDetail();
    bool channelPickerHit(int x, int y, int* channel) const;
    int  tabHit(int x, int y) const;               // PickTab, or -1
    int  filterSlotHit(int x, int y) const;        // Button slot, or -1
    int  paramHit(int x, int y) const;             // PARAM_* code, or -1
    void tapPicker(int x, int y);
    void tapParams(int x, int y);
    void applyParamHit(int code);

    // The open patch's chain (into the scratch) and the Filters tab's buttons
    bool scratch();
    bool loadChain();
    void saveChain();
    void loadItems();
    int  chainPosition(const char* name) const;   // -1 when not in the chain
    bool loadDef(const char* name);
    int  paramRows() const;                        // Rows per page
    void displayName(const ui_mididev_t& d, char* buf, int len) const;
    bool isUp(const ui_mididev_t& d) const;
    bool isScript(const ui_mididev_t& d) const;
};

// Global instance
ScreenMidiDevices& getScreenMidiDevices();

#endif // __cplusplus

#endif // SCREEN_MIDI_DEVICES_H
