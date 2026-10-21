#ifndef SCREEN_MIDI_DEVICES_H
#define SCREEN_MIDI_DEVICES_H

#include "ui_manager.h"

#ifdef __cplusplus

// MIDI Devices: a crossbar of the registered devices (docs/UI_MIDI_DEVICES.md).
// Rows are MIDI INs, columns MIDI OUTs; a cell is a route (MIDI.route). The
// "Prog" column / row pick the program's own IN / OUT.
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
    enum Overlay { OVERLAY_NONE = 0, OVERLAY_CHANNEL, OVERLAY_DETAIL };

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
    void drawDetail();
    bool channelPickerHit(int x, int y, int* channel) const;
    void displayName(const ui_mididev_t& d, char* buf, int len) const;
    bool isUp(const ui_mididev_t& d) const;
};

// Global instance
ScreenMidiDevices& getScreenMidiDevices();

#endif // __cplusplus

#endif // SCREEN_MIDI_DEVICES_H
