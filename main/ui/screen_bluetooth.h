#ifndef SCREEN_BLUETOOTH_H
#define SCREEN_BLUETOOTH_H

#include "ui_manager.h"

#ifdef __cplusplus

// Bluetooth screen (docs/BLE_MIDI.md): the BLE-MIDI device (advertising on /
// off, the connected central) and the BLE-MIDI host (on / off, a scan, the
// remembered and found peripherals). Only on boards built with BLE-MIDI.
//
//   [Device: ON ] [Host: ON ]          <- toggles (saved in NVS)
//   Device: connected AA:BB:..         <- state of the device role
//   * WIDI Master       -52  Connected [x]   <- tap: connect / disconnect
//     MD-BT01           -70             [x]   [x]: forget (remembered ones)
//   ...                                       (drag to scroll)
//   [Scan] on the nav bar
class ScreenBluetooth : public Screen {
public:
    ScreenBluetooth();
    ~ScreenBluetooth() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    void onNavCenter() override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;
    void getTileStatus(char* buf, int len) override;

    struct Row;     // One peripheral in the list (heap, see m_rows)

private:
    bool m_isActive;
    bool m_needsRedraw;
    int64_t m_lastPollUs;
    uint32_t m_lastHash;

    Row* m_rows;    // Heap: the UI task's stack is small (CLAUDE.md)
    int m_rowCount;
    int m_scroll;   // First row shown

    // Snapshot of the roles, taken with the rows (draw() reads only these)
    bool m_devOn, m_hostOn, m_scanning;
    int m_devState;
    char m_devPeer[18];

    int m_touchId;
    int m_touchX, m_touchY, m_dragAccum;
    bool m_dragged;

    bool ensureRows();
    void snapshot();
    uint32_t hash() const;
    int visibleRows() const;
    int listTop() const;
    void tap(int x, int y);
};

ScreenBluetooth& getScreenBluetooth();

#endif // __cplusplus

#endif // SCREEN_BLUETOOTH_H
