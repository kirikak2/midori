#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "ui_common.h"

#ifdef __cplusplus

// The layouts behind the C API in ui_common.h, as ScreenLayout sees them.
//
// A layout is built off to the side by the PicoRuby task and swapped into its
// slot whole on commit, so the UI task never sees one half-defined. Every
// access to a live layout -- the UI task drawing it, Ruby changing a label --
// holds the registry lock; a layout pointer is only good while it is held.
struct LayoutDef {
    char title[UI_LAYOUT_TITLE_LEN];
    uint32_t gen;        // Script generation it belongs to (see ui_layout_cleanup)
    uint32_t version;    // Unique per commit: a changed version is a new layout
    uint32_t dirty;      // Widgets Ruby changed (label, colour, value); bit = id
    uint8_t count;
    ui_widget_t widgets[UI_LAYOUT_MAX_WIDGETS];
};

namespace ui_layout {

// Creates the lock. Called from UIManager::init().
void init(void);

// Recursive, so code holding it can still call the C API.
void lock(void);
void unlock(void);

// The live layout in slot, or nullptr. Lock held.
LayoutDef* get(int slot);

// Bits of def->dirty, cleared. Lock held.
uint32_t take_dirty(LayoutDef* def);

void set_last_shown(int slot);

// Frees layouts left over from a script that has ended. UI task.
void collect(void);

}  // namespace ui_layout

#endif // __cplusplus

#endif // UI_LAYOUT_H
