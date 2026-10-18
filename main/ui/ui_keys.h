#ifndef UI_KEYS_H
#define UI_KEYS_H

#include "ui_common.h"

#ifdef __cplusplus

// The keyboard model behind the ui_keys_* C API in ui_common.h, as
// ScreenKeys sees it. See docs/KEYS.md.
//
// Configuration and lights are written from any task (Ruby, the MIDI input
// task forwarding a route, the note scheduler) and read by the UI task while
// drawing: plain stores of small values, so a frame drawn while one lands may
// show the old or the new value, and the dirty bits make sure the next one
// shows the new. The touch side (touch_* / release_all) is the UI task's only.
namespace ui_keys {

// Lowest note and number of octaves currently shown.
int low();
int octaves();
int top();                    // low() + 12 * octaves()
bool labels();
bool velocity_by_position();
int velocity();

inline bool is_black(int note)
{
    switch (note % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

// What a key looks like right now: pressed > lit > its own colour > white / black.
uint16_t key_fill(int note);

// Bumped by ui_keys_reset() / ui_keys_cleanup(). The screen compares it to
// let go of the fingers' notes and repaint everything.
uint32_t generation();

// Notes whose look changed since the last call (bit n of out[n / 32]), and
// whether everything did (range or base colours). Cleared.
void take_dirty(uint32_t out[4], bool& full);

// Fingers. note < 0 means the finger is over no key. A key sounds while any
// finger holds it; the note on / off go to the keyboard's destinations as of
// the moment it went down, so a config change mid-press cannot strand a note.
void touch_down(int touchId, int note, int velocity);
void touch_move(int touchId, int note, int velocity);
void touch_up(int touchId);
void release_all();

}  // namespace ui_keys

#endif // __cplusplus

#endif // UI_KEYS_H
