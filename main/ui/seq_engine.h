// The Sequencer's song model and playback (docs/SEQUENCER.md), free of
// ESP-IDF so it builds and runs on the host (main/ui/host_test).
//
// Not thread-safe: ui_sequencer.cpp holds a lock around every call. Time
// comes from outside: seq_engine_clock() for each MIDI Timing Clock (F8)
// received, seq_engine_subtick() for the three interpolated ticks between
// two of them. Notes go out through the seq_output_t callbacks.

#ifndef SEQ_ENGINE_H
#define SEQ_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_PPQN              96
#define SEQ_TICKS_PER_CLOCK   4      // One F8 (24 PPQN)
#define SEQ_TICKS_PER_BAR     384    // 4/4
#define SEQ_MAX_LINES         16
#define SEQ_DEFAULT_LINES     8
#define SEQ_MAX_PATTERNS      16
#define SEQ_MAX_STEPS         64
#define SEQ_MAX_BARS          8
#define SEQ_MAX_NOTES         512    // Per array (one line's grid or realtime notes in one pattern)
#define SEQ_MAX_SONG_NOTES    8192   // In the whole song
#define SEQ_MAX_CHORD         8
#define SEQ_MAX_CHAIN         64
#define SEQ_MAX_REPEAT        16
#define SEQ_MAX_SOUNDING      128
#define SEQ_NAME_LEN          8      // Line names
#define SEQ_SONG_NAME_LEN     16

// A pattern argument meaning "the one being edited" (seq_resolve_pattern)
#define SEQ_PATTERN_EDIT      (-2)

typedef enum {
    SEQ_MODE_GRID = 0,
    SEQ_MODE_REALTIME = 1,
} seq_mode_t;

typedef enum {
    SEQ_RATE_4 = 0,
    SEQ_RATE_8,
    SEQ_RATE_16,
    SEQ_RATE_32,
    SEQ_RATE_8T,
    SEQ_RATE_16T,
    SEQ_RATE_COUNT,
} seq_rate_t;

typedef enum {
    SEQ_SWITCH_PATTERN = 0,   // A queued pattern starts when the playing one ends
    SEQ_SWITCH_BAR = 1,       // ... at the next bar line
} seq_switch_t;

// One note. Grid: pos / len in steps. Realtime: in ticks (96 PPQN).
typedef struct {
    uint16_t pos;
    uint16_t len;
    uint8_t  note;
    uint8_t  velocity;
    uint8_t  flags;      // Reserved (0)
    uint8_t  reserved;   // 0
} seq_note_t;

// Sorted by (pos, note)
typedef struct {
    seq_note_t *v;
    uint16_t    n;
    uint16_t    cap;
} seq_notes_t;

// A line's part of a pattern
typedef struct {
    uint8_t     steps;   // Grid: 4 / 8 / 16 / 32 / 64
    uint8_t     rate;    // Grid: seq_rate_t
    uint8_t     bars;    // Realtime: 1 / 2 / 4 / 8
    uint8_t     flags;   // Reserved
    seq_notes_t grid;
    seq_notes_t rt;
} seq_pattern_line_t;

typedef struct {
    uint8_t            length;   // 0: the longest line's, 1..8: bars
    seq_pattern_line_t lines[SEQ_MAX_LINES];
} seq_pattern_t;

// A line (track): the same in every pattern
typedef struct {
    uint8_t mode;        // seq_mode_t
    uint8_t channel;     // 0..15
    uint8_t mute;
    uint8_t velocity;    // Grid: 0 = the chord's, 1..127 fixed
    uint8_t gate;        // Grid: 1..100 %
    uint8_t quantize;    // Realtime recording: 0 none, else seq_rate_t + 1
    uint8_t rec_mode;    // Realtime recording: 0 overdub, 1 replace
    char    name[SEQ_NAME_LEN + 1];
} seq_line_t;

typedef struct {
    uint8_t pattern;
    uint8_t repeat;      // 1..16
} seq_chain_entry_t;

// Where notes go. note_on returns a token handed back to note_off, so the
// note off reaches wherever the note on went.
typedef uint32_t (*seq_note_on_fn)(void *ctx, uint8_t channel, uint8_t note, uint8_t velocity);
typedef void (*seq_note_off_fn)(void *ctx, uint32_t token, uint8_t channel, uint8_t note);

typedef struct {
    seq_note_on_fn  note_on;
    seq_note_off_fn note_off;
    void           *ctx;
} seq_output_t;

typedef struct {
    bool     used;
    uint8_t  line;
    uint8_t  channel;
    uint8_t  note;
    uint32_t token;
    uint32_t off_tick;   // song_tick at which it ends
} seq_sounding_t;

typedef struct {
    // --- Song (saved) ---
    uint8_t           line_count;
    seq_line_t        lines[SEQ_MAX_LINES];
    seq_pattern_t    *patterns[SEQ_MAX_PATTERNS];   // NULL: empty slot
    seq_chain_entry_t chain[SEQ_MAX_CHAIN];
    uint8_t           chain_len;
    bool              chain_on;
    char              name[SEQ_SONG_NAME_LEN + 1];
    uint16_t          bpm_x100;
    uint16_t          song_notes;                    // Notes in every array

    // --- Playback (not saved) ---
    seq_output_t      out;
    bool              running;
    uint32_t          song_tick;      // Ticks since Start
    uint32_t          pattern_tick;   // Ticks since the playing pattern began
    int8_t            play_pattern;
    int8_t            next_pattern;   // -1: none queued
    int8_t            edit_pattern;   // -1: follows play_pattern
    uint8_t           switch_mode;    // seq_switch_t
    uint8_t           chain_pos;
    uint8_t           chain_repeat;   // Repeats of chain[chain_pos] done
    uint8_t           pending;        // Interpolated ticks left before the next F8
    uint32_t          edits;          // Bumped by every change to the song
    uint32_t          pattern_changes;   // Bumped when play_pattern changes
    seq_sounding_t    sounding[SEQ_MAX_SOUNDING];

    // --- The last chord played into it ---
    uint8_t           chord[SEQ_MAX_CHORD];
    uint8_t           chord_vel[SEQ_MAX_CHORD];
    uint8_t           chord_n;
    uint8_t           held_count;
    uint32_t          chord_serial;   // Bumped whenever the chord changes
    uint32_t          held[4];        // Notes held on the input (bit n of held[n / 32])
} seq_engine_t;

// --- Life --------------------------------------------------------------------

// A new engine holding an empty song of SEQ_DEFAULT_LINES lines (NULL when
// out of memory).
seq_engine_t *seq_engine_new(const seq_output_t *out);
void seq_engine_free(seq_engine_t *e);

// Back to an empty song of `lines` lines (1..16): stops (its notes off),
// frees every pattern, clears the chain, the name and the chord.
void seq_engine_reset(seq_engine_t *e, int lines);

// --- Transport (what arrives as a destination) --------------------------------

void seq_engine_start(seq_engine_t *e);      // FA: from the top on the next F8
void seq_engine_continue(seq_engine_t *e);   // FB: from where it stopped
void seq_engine_stop(seq_engine_t *e);       // FC: notes off, position kept
void seq_engine_song_position(seq_engine_t *e, uint16_t sixteenths);   // F2 (only while stopped)

// F8: plays any interpolated ticks still due, then this clock's tick. While
// running, seq_engine_pending() ticks are then due before the next F8.
void seq_engine_clock(seq_engine_t *e);
// One interpolated tick, if one is due. Returns whether more are.
bool seq_engine_subtick(seq_engine_t *e);
int  seq_engine_pending(const seq_engine_t *e);

// A note on / off from the input: updates the chord (and, later, recording).
void seq_engine_input(seq_engine_t *e, uint8_t status, uint8_t note, uint8_t velocity);

// --- Lines -------------------------------------------------------------------

// Adds a line at the end (channel: the lowest no other line uses). Returns
// its index, or -1 with 16 lines.
int  seq_line_add(seq_engine_t *e);
// Removes a line and its notes in every pattern. The last line stays.
bool seq_line_remove(seq_engine_t *e, int line);
// Notes off for whatever the line is sounding (after a channel change, etc.)
void seq_line_release(seq_engine_t *e, int line);

// --- Patterns ----------------------------------------------------------------

// A pattern index, with SEQ_PATTERN_EDIT resolved; -1 when out of range.
int  seq_resolve_pattern(const seq_engine_t *e, int pattern);
// The pattern, created empty if `create` (NULL when empty and not created,
// or out of memory).
seq_pattern_t *seq_pattern_get(seq_engine_t *e, int pattern, bool create);
bool seq_pattern_clear(seq_engine_t *e, int pattern);   // Frees the slot
bool seq_pattern_copy(seq_engine_t *e, int src, int dst);
bool seq_pattern_set_length(seq_engine_t *e, int pattern, int bars);   // 0 auto, 1..8

uint32_t seq_pattern_ticks(const seq_engine_t *e, int pattern);
uint32_t seq_line_ticks(const seq_engine_t *e, int pattern, int line);
int  seq_rate_ticks(int rate);
int  seq_pline_steps(const seq_engine_t *e, int pattern, int line);
int  seq_pline_rate(const seq_engine_t *e, int pattern, int line);
int  seq_pline_bars(const seq_engine_t *e, int pattern, int line);
bool seq_pline_set_steps(seq_engine_t *e, int pattern, int line, int steps);
bool seq_pline_set_rate(seq_engine_t *e, int pattern, int line, int rate);
bool seq_pline_set_bars(seq_engine_t *e, int pattern, int line, int bars);

// Plays `pattern` now if stopped, else queues it (seq_switch_t). Turns the
// chain off. -1 cancels what is queued.
bool seq_select_pattern(seq_engine_t *e, int pattern);

bool seq_set_chain(seq_engine_t *e, const seq_chain_entry_t *entries, int n);
void seq_set_chain_on(seq_engine_t *e, bool on);

// --- Notes -------------------------------------------------------------------

// The notes of one line of a pattern (mode: seq_mode_t); NULL if none.
const seq_notes_t *seq_notes_get(const seq_engine_t *e, int pattern, int line, int mode);

// Grid: puts `n` notes at `step`, `len` steps long. Same-pitch notes never
// overlap: one already sounding there is cut short, and the new one stops
// where the next one of its pitch starts. Returns how many were placed (-1
// on bad arguments; fewer than n when a limit is reached).
int  seq_grid_place(seq_engine_t *e, int pattern, int line, int step, int len,
                    const uint8_t *notes, const uint8_t *velocities, int n);
// Grid: removes the notes starting at `step` (only `note`, unless note < 0).
// Returns how many.
int  seq_grid_remove(seq_engine_t *e, int pattern, int line, int step, int note);
// Grid: makes the notes starting at `step` `len` steps long (cut where a
// same-pitch note starts and at step 64).
bool seq_grid_set_length(seq_engine_t *e, int pattern, int line, int step, int len);

// Realtime: adds a note (pos / len in ticks).
bool seq_rt_add(seq_engine_t *e, int pattern, int line, int pos, int len, int note, int velocity);

// Removes every note of a line (mode < 0: both modes; line < 0: every line).
void seq_notes_clear(seq_engine_t *e, int pattern, int line, int mode);

// --- Chord -------------------------------------------------------------------

void seq_set_chord(seq_engine_t *e, const uint8_t *notes, const uint8_t *velocities, int n);

// --- Files (.mseq) ------------------------------------------------------------
//
// A song as a file: "MSEQ", version, then chunks (id, u32 size, payload),
// little-endian, ending in a CRC32 of everything before it. Chunks a reader
// does not know are skipped. See docs/SEQUENCER.md for the layout.

#define SEQ_FILE_VERSION      1
// The largest piece seq_dump_next() writes (a full note array's chunk)
#define SEQ_DUMP_MAX_CHUNK    (8 + 4 + SEQ_MAX_NOTES * 8)

// HEAD flags
#define SEQ_FILE_CHAIN_ON     0x01
#define SEQ_FILE_DIRTY        0x02   // Changed since it was last saved by name (autosave)

// A deep copy of the song (lines, patterns, chain, name, current pattern),
// with no output and nothing playing; NULL when out of memory. Free with
// seq_engine_free(). Writing goes from a copy so edits made meanwhile
// cannot tear the file.
seq_engine_t *seq_song_clone(const seq_engine_t *e);

typedef struct {
    uint8_t  phase;
    uint8_t  pattern;
    uint8_t  line;
    uint8_t  part;
    uint8_t  flags;
    uint32_t crc;
} seq_dump_t;

void seq_dump_begin(seq_dump_t *d, uint8_t flags);
// The next piece of the file into buf (cap at least SEQ_DUMP_MAX_CHUNK).
// Returns its length; 0 once the whole file has been written.
int  seq_dump_next(const seq_engine_t *e, seq_dump_t *d, uint8_t *buf, int cap);

typedef enum {
    SEQ_LOAD_OK = 0,
    SEQ_LOAD_BAD_MAGIC = -1,
    SEQ_LOAD_BAD_VERSION = -2,
    SEQ_LOAD_BAD_CHUNK = -3,      // Out of order, too short, or a size that cannot be
    SEQ_LOAD_BAD_VALUE = -4,      // A value out of range, unsorted notes...
    SEQ_LOAD_TOO_MANY_NOTES = -5,
    SEQ_LOAD_BAD_CRC = -6,
    SEQ_LOAD_TRUNCATED = -7,
    SEQ_LOAD_NO_MEMORY = -8,
} seq_load_error_t;

typedef struct seq_loader seq_loader_t;

// Reading: feed the file in pieces of any size, then finish. Nothing of the
// live song is touched until seq_engine_adopt().
seq_loader_t *seq_load_begin(void);
int  seq_load_feed(seq_loader_t *ld, const uint8_t *data, int len);   // SEQ_LOAD_*
// Checks the file is whole (CRC included); hands over the song it holds
// and the HEAD flags. On error *song is NULL.
int  seq_load_finish(seq_loader_t *ld, seq_engine_t **song, uint8_t *flags);
void seq_load_free(seq_loader_t *ld);
const char *seq_load_error_name(int err);

// Moves a loaded song into the live engine (its notes off first, stopped
// or not: it keeps playing from the top of the song's current pattern) and
// frees what is left of `song`.
void seq_engine_adopt(seq_engine_t *e, seq_engine_t *song);

#ifdef __cplusplus
}
#endif

#endif // SEQ_ENGINE_H
