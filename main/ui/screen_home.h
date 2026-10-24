#ifndef SCREEN_HOME_H
#define SCREEN_HOME_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// The menu every other screen is reached from (docs/UI_NAVIGATION.md): one
// tile per screen, grouped Perform / Tempo & state / System, plus one per
// script-defined layout. The nav bar's Home button comes back here, and Back
// flips between the last two screens used without counting this one.
class ScreenHome : public Screen {
public:
    ScreenHome();
    ~ScreenHome() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

private:
    static constexpr int STATUS_LEN = 20;

    bool m_isActive;

    // The menu as last drawn. Rebuilt every status poll; a change in which
    // tiles exist (a layout defined, a screen hidden) repaints everything.
    // Heap arrays of m_capacity items, grown when layouts are added (there is
    // no fixed number of them)
    MenuItem* m_items;
    int m_itemCount;
    char (*m_status)[STATUS_LEN];
    int m_capacity;
    int64_t m_lastPollUs;

    // First visible row, when there are more tiles than fit (CoreS3 with
    // layouts defined).
    int m_scrollRow;

    // Only the first finger operates the menu. A touch that travels further
    // than a tap scrolls instead of opening the tile it started on.
    int m_touchId;
    int m_startX, m_startY;
    int m_lastY;
    int m_dragAccum;
    bool m_scrolled;
    int m_pressedTile;   // Highlighted while held, -1 for none

    int columns() const;
    int visibleRows() const;
    int totalRows() const;
    void tileRect(int slot, int& x, int& y, int& w, int& h) const;  // slot = on-screen position
    int hitTest(int x, int y) const;   // Item index, or -1

    bool reserve(int capacity);
    void tileLabel(const MenuItem& item, char* buf, int len) const;
    void tileStatus(const MenuItem& item, char* buf, int len) const;

    void drawTile(int item, bool pressed);
    void drawAll();
    void poll(bool force);
};

ScreenHome& getScreenHome();

#endif // __cplusplus

#endif // SCREEN_HOME_H
