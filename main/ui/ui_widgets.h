#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui_common.h"

#ifdef __cplusplus

#include <M5Unified.h>

// Drawing shared by the fixed screens (Pads, Knobs) and layout widgets
// (docs/UI_LAYOUT_DSL.md), so a pad or a knob looks the same wherever it is
// placed. Everything takes the target and its origin the way ui_strip::paint()
// hands them out: draw at screen coordinates minus (ox, oy).

// Default font cell, in pixels per text size unit
static constexpr int UI_GLYPH_W = 6;
static constexpr int UI_GLYPH_H = 8;

// --- Pads -------------------------------------------------------------------

// A pad filling (x, y, w, h). label is what to print (the caller decides
// between the configured label and a placeholder for an unassigned pad).
void ui_pad_paint(LovyanGFX* g, int ox, int oy, int x, int y, int w, int h,
                  const pad_config_t* pad, const char* label, int textSize);

// Largest text size (1..3) that fits label inside a w x h pad
int ui_pad_text_size_for(int w, int h, const char* label);

// --- Knobs ------------------------------------------------------------------

// Where a knob's parts go inside its cell. The Knobs screen fills this from
// its per-board constants; layout widgets get it from ui_knob_geom_for().
struct KnobGeom {
    int x, y, w, h;      // The cell
    int cx, cy;          // Ring centre
    int rIn, rOut;       // Ring radii
    int labelY;          // Label top, from the cell's top edge
    int textSize;        // Label and value
};

// Sizes every part from the cell alone. Chosen so the Knobs screen's cells
// come out close to the hand-tuned per-board constants.
KnobGeom ui_knob_geom_for(int x, int y, int w, int h);

// The gauge sweeps 270 degrees with the gap at the bottom. LovyanGFX measures
// arcs from 3 o'clock going clockwise, so 135 is the lower left.
static constexpr float UI_KNOB_ANGLE_START = 135.0f;
static constexpr float UI_KNOB_ANGLE_SWEEP = 270.0f;

float ui_knob_value_angle(const knob_config_t* k, float value);
float ui_knob_origin_angle(const knob_config_t* k);

// The whole knob: background, gauge, value, label, and the grab ring when a
// finger holds it. k may be NULL or unassigned (drawn as an empty track).
void ui_knob_paint(LovyanGFX* g, int ox, int oy, const KnobGeom& geom,
                   const knob_config_t* k, const char* label, bool grabbed);

// Angle (radians) a finger at (x, y) moving by (dx, dy) sweeps around the
// ring centre: the cross product of the offset with the movement over the
// distance squared, floored near the centre so a finger there cannot slam
// the knob end to end.
float ui_knob_drag_angle(const KnobGeom& geom, int x, int y, int dx, int dy);

#endif // __cplusplus

#endif // UI_WIDGETS_H
