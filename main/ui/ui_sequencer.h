#ifndef UI_SEQUENCER_H
#define UI_SEQUENCER_H

#include "ui_common.h"
#include "seq_engine.h"

#ifdef __cplusplus

// The Sequencer as ScreenSequencer sees it (docs/SEQUENCER.md).
//
// The screen never draws from the engine itself: that would hold the
// engine's lock -- and with it the sequencer task -- for as long as a frame
// takes. It copies what it shows into a View instead (snapshot(), a short
// memcpy under the lock) and draws from that. Edits go through the
// ui_seq_* C API and the helpers below, which take the lock themselves.
namespace ui_seq {

// Notes of one line copied per snapshot: those overlapping the steps on
// screen. More than this (a dense realtime take) are dropped from the view.
constexpr int VIEW_NOTES = 128;

// Realtime lines are shown in sixteenths
constexpr int RT_UNIT_TICKS = SEQ_PPQN / 4;

struct LineView {
    uint8_t  mode, channel, mute, steps, rate, bars;
    uint8_t  lo, hi;        // Pitch range of all the line's notes (for the piano roll)
    uint16_t cols;          // Units across the line: grid steps / realtime sixteenths
    uint16_t s0;            // First unit on screen
    uint16_t shown;         // Units on screen
    uint32_t length;        // Ticks
    int32_t  pos;           // Tick within the line now; -1 when not playing what is shown
    uint16_t n;
    bool     truncated;
    char     name[SEQ_NAME_LEN + 1];
    seq_note_t notes[VIEW_NOTES];
};

struct View {
    uint32_t edits;
    uint32_t patternChanges;
    bool     running;
    bool     chainOn;
    int8_t   play, next, edit;
    int8_t   shown;         // The pattern on screen (edit, else play)
    uint8_t  lineCount;
    uint8_t  chainLen;
    uint8_t  chordN;
    uint16_t used;          // Patterns with anything in them
    uint8_t  chord[SEQ_MAX_CHORD];
    uint32_t chordSerial;   // Changes whenever the chord does
    char     name[SEQ_SONG_NAME_LEN + 1];
    LineView lines[SEQ_MAX_LINES];
};

// Fills `v` for the steps on screen (`window`, a step offset, and `view`
// steps). With `notes` false only the cheap part is refreshed -- state, the
// chord, positions, s0 / shown -- and the notes and line settings stay as
// copied last time. Returns false when there is no sequencer.
bool snapshot(View* v, int window, int view, bool notes);

// Bumped by every change to the song, and by pattern switches (cheap). The
// chord is not counted: it only shows in the header.
uint32_t revision();

// Grid editing on the shown pattern, with the chord as it is now. The
// velocities are the chord's, or the line's fixed velocity.
int  place_chord(int line, int step, int len);
// The notes starting at `step` replaced by the chord, as long as the longest was
bool replace_with_chord(int line, int step);
// Where the sequencer's clock comes from: the bit of a source routing F8 to
// it. With none, routes the Tempo's clock to it first (clock only, until the
// script's end clears the routes) -- what the screen's Play needs when no
// script set anything up. 0 when there is no clock to route.
uint8_t ensure_clock();

// How many notes start at `step`, and the longest of them
int  starting_at(int line, int step, int* longest);

}  // namespace ui_seq

#endif // __cplusplus

#endif // UI_SEQUENCER_H
