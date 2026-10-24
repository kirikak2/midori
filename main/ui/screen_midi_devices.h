#ifndef SCREEN_MIDI_DEVICES_H
#define SCREEN_MIDI_DEVICES_H

#include "ui_manager.h"

#ifdef __cplusplus

// MIDI Devices: a crossbar of the registered devices (docs/UI_MIDI_DEVICES.md).
// Rows are MIDI INs, columns MIDI OUTs; a cell is a route (MIDI.route). The
// "Prog" column / row pick the program's own IN / OUT. A long press on a cell
// opens its picker: the channels it passes, or (Filters tab) the named
// filters chained on it and their parameters (docs/MIDI_FILTER_REGISTRY.md).
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

    // What the grid shows, rebuilt from the model each update
    struct Grid {
        ui_mididev_t devs[MAX_DEVS];
        int count;
        int rows[MAX_DEVS];     // Device indices with a MIDI IN
        int rowCount;
        int cols[MAX_DEVS];     // Device indices with a MIDI OUT
        int colCount;
        int x0, y0;             // Top-left cell
        int cellW, cellH;
        int nameW, headerH;
    };

    bool m_isActive;
    bool m_needsRedraw;
    uint32_t m_lastCheckMs;
    uint32_t m_lastDiagMs;
    uint32_t m_signature;       // Model + routes, to notice changes
    Grid m_grid;

    // Touch: one finger at a time; a hold is a long press
    int m_touchId;
    int m_touchX, m_touchY;
    uint32_t m_touchMs;
    bool m_longFired;

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

    // Hit testing. Returns false outside the grid. row / col are grid
    // positions; rowCount / colCount mean the Prog row / column; -1 a header.
    bool hit(int x, int y, int* row, int* col) const;
    void tap(int x, int y);
    void longPress(int x, int y);

    void drawGrid();
    void drawCell(int row, int col);
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
};

// Global instance
ScreenMidiDevices& getScreenMidiDevices();

#endif // __cplusplus

#endif // SCREEN_MIDI_DEVICES_H
