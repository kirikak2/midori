#ifndef SCREEN_KEYS_H
#define SCREEN_KEYS_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// One keyboard on screen (docs/KEYS.md): 1 to 4 octaves filling its area, up
// to five fingers at once. The view does the geometry -- which key is under a
// finger, where each key is drawn -- and the drawing, for the keyboard whose
// id it is given; the model and everything MIDI is ui_keys.cpp.
//
// The Keys screen has one view; a layout has one per keys widget. A keyboard
// is shown by one view at a time (the dirty bits are taken by whoever shows it).
//
// Only keys whose look changed are repainted, each through the shared strip
// (ui_strip.h) with the whole keyboard rendered and clipped to it, so a white
// key brings the black keys overlapping it along.
class KeysView {
public:
    KeysView();

    void setId(int id) { m_id = id; }
    int id() const { return m_id; }
    void setArea(int x, int y, int w, int h);

    void enter();            // Starts showing (the caller paints right after)
    void leave();            // Lets go of the fingers
    void update();           // Repaints what changed
    void draw();             // Paints everything
    void onTouch(int touchId, int x, int y, bool pressed);
    void onTouchMove(int touchId, int x, int y);

private:
    int m_id;
    int m_areaX, m_areaY, m_areaW, m_areaH;
    bool m_isActive;
    uint32_t m_gen;

    // Geometry of the range on screen, worked out from the area and the
    // model's low / octaves whenever either changes.
    struct Geom {
        int x, y, w, h;
        int low, top;          // Inclusive note range
        int whites;            // White keys in it
        int blackW, blackH;
    };
    Geom m_geom;
    void computeGeom();

    int whiteIndex(int note) const;     // White keys below note in the range
    int whiteX(int index) const;        // Left edge of the index-th white key
    void keyRect(int note, int& x, int& y, int& w, int& h) const;
    int hitTest(int x, int y, int& velocity) const;

    void render(LovyanGFX* g, int ox, int oy);
    void paintRect(int x, int y, int w, int h);
    void paintDirty(const uint32_t dirty[4]);
};

// The Keys screen: the whole content area, showing the keyboard
// ui_keys_shown() names (the default one unless a script showed another).
class ScreenKeys : public Screen {
public:
    ScreenKeys();
    ~ScreenKeys() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    const char* getTitle() override;
    void getTileStatus(char* buf, int len) override;

private:
    KeysView m_view;
    bool m_isActive;
    char m_title[16];
};

ScreenKeys& getScreenKeys();

#endif // __cplusplus

#endif // SCREEN_KEYS_H
