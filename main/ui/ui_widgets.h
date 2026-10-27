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

// --- Envelopes (docs/ENVELOPE.md) -----------------------------------------

// Where an envelope's parts go inside its cell
struct EnvGeom {
    int x, y, w, h;      // The cell
    int px, py, pw, ph;  // Plot: py is level 1.0, py + ph level 0
    float unit;          // Pixels of an A / D / R segment at max_time
    float hold;          // Pixels of the sustain segment
    int handleR;         // Handle radius (a grabbed one is drawn larger)
    int textSize;
    int labelY;          // Label top, from the cell's top edge
    int valuesY;         // Values row top, from the cell's top edge; -1 for none
};

EnvGeom ui_env_geom_for(int x, int y, int w, int h);

// Fraction of a segment a time takes: ln(1 + t) / ln(1 + max_time), and back
float ui_env_time_to_norm(float ms, float maxTime);
float ui_env_norm_to_time(float n, float maxTime);

// The five corners of the curve in screen pixels: start, attack peak, end of
// decay, end of sustain, end of release. Handle h (0..3) is point h + 1.
void ui_env_points(const EnvGeom& geom, const envelope_config_t* e,
                   float xs[5], float ys[5]);

// The handle nearest (x, y), skipping those set in excludeMask
int ui_env_nearest_handle(const EnvGeom& geom, const envelope_config_t* e,
                          int x, int y, uint8_t excludeMask);

// Moves handle h so it sits at (hx, hy), as far as its stages allow. Values
// come out quantized; returns whether any changed.
bool ui_env_drag(const EnvGeom& geom, envelope_config_t* e, int h, float hx, float hy);

// The whole envelope. grabbedMask has bit h set for each handle a finger holds.
void ui_env_paint(LovyanGFX* g, int ox, int oy, const EnvGeom& geom,
                  const envelope_config_t* e, uint8_t grabbedMask);

#endif // __cplusplus

#endif // UI_WIDGETS_H
