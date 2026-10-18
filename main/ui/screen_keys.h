#ifndef SCREEN_KEYS_H
#define SCREEN_KEYS_H

#include "ui_manager.h"

#ifdef __cplusplus

#include <M5Unified.h>

// The on-screen keyboard (docs/KEYS.md): 1 to 4 octaves filling its area,
// up to five fingers at once. This class does the geometry -- which key is
// under a finger, where each key is drawn -- and the drawing; the model and
// everything MIDI is ui_keys.cpp.
//
// Only keys whose look changed are repainted, each through the shared strip
// (ui_strip.h) with the whole keyboard rendered and clipped to it, so a white
// key brings the black keys overlapping it along.
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

    // Where the keyboard is drawn and touched. The whole content area by
    // default; a layout narrows it to its widget's cell while it shows, and
    // puts it back (resetArea) before anything else can show this screen.
    void setArea(int x, int y, int w, int h);
    void resetArea();

private:
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

ScreenKeys& getScreenKeys();

#endif // __cplusplus

#endif // SCREEN_KEYS_H
