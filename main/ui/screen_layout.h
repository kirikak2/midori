#ifndef SCREEN_LAYOUT_H
#define SCREEN_LAYOUT_H

#include "ui_manager.h"

#ifdef __cplusplus

struct LayoutDef;

// Shows one script-defined layout (docs/UI_LAYOUT_DSL.md): pads, knobs and
// the XYPad / Tombola views at the rectangles Ruby resolved from its grid.
// Which of the (up to four) layouts is shown is the screen argument, so every
// layout is the same UI_SCREEN_LAYOUT with a different slot.
//
// Pads and knobs are drawn and operated here, from the shared widget drawing
// (ui_widgets.h). The XYPad and Tombola are their own screens' views, narrowed
// to the widget's cell while this screen shows: one model each, so at most
// one of each per layout.
class ScreenLayout : public Screen {
public:
    ScreenLayout();
    ~ScreenLayout() override = default;

    void enter() override;
    void leave() override;
    void update() override;
    void draw() override;
    void onTouch(int touchId, int x, int y, bool pressed) override;
    void onTouchMove(int touchId, int x, int y) override;
    void onShowArg(int arg) override;
    const char* getTitle() override;
    const char* getNavCenterLabel() override;

private:
    bool m_isActive;
    int m_slot;
    uint32_t m_version;   // Of the layout the views were attached for
    char m_title[UI_LAYOUT_TITLE_LEN];

    // Which views are borrowed into this screen, so leave() can return them
    bool m_hasXYPad;
    bool m_hasTombola;

    // What each finger is holding: a widget id, or -1. Pads and knobs are
    // handled here; a finger that landed on a view is forwarded to it.
    int m_touchOwner[MAX_TOUCH_POINTS];
    uint8_t m_ownerKind[MAX_TOUCH_POINTS];   // ui_widget_kind_t of the owner
    int m_lastX[MAX_TOUCH_POINTS];
    int m_lastY[MAX_TOUCH_POINTS];
    // Unquantized knob value being turned (see ScreenKnobs::m_accum)
    float m_accum[MAX_TOUCH_POINTS];
    bool m_moved[MAX_TOUCH_POINTS];

    void attachViews(LayoutDef* def);
    void detachViews();
    void releaseTouches(LayoutDef* def, bool notify);

    int hitTest(LayoutDef* def, int x, int y);
    bool isHeld(int id, int exceptTouch);
    void paintWidget(LayoutDef* def, int id);

    void padPress(LayoutDef* def, int id, bool pressed);
};

ScreenLayout& getScreenLayout();

#endif // __cplusplus

#endif // SCREEN_LAYOUT_H
