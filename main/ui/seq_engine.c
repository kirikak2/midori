// The Sequencer's song model and playback (docs/SEQUENCER.md).

#include "seq_engine.h"
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

_Static_assert(sizeof(seq_note_t) == 8, "seq_note_t is saved as is");

static const uint8_t RATE_TICKS[SEQ_RATE_COUNT] = { 96, 48, 24, 12, 32, 16 };

// Note arrays grow by this many notes
#define NOTES_CHUNK 32

// --- Memory: note arrays and patterns go to PSRAM when there is some --------

static void *mem_calloc(size_t n, size_t size)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return calloc(n, size);
}

static void *mem_realloc(void *ptr, size_t size)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return realloc(ptr, size);
}

static void mem_free(void *ptr)
{
    free(ptr);   // heap_caps memory is freed with free() as well
}

// --- Note arrays -------------------------------------------------------------

static void notes_free(seq_engine_t *e, seq_notes_t *a)
{
    e->song_notes -= a->n;
    mem_free(a->v);
    a->v = NULL;
    a->n = 0;
    a->cap = 0;
}

static bool notes_less(const seq_note_t *a, uint16_t pos, uint8_t note)
{
    return a->pos < pos || (a->pos == pos && a->note < note);
}

// First index whose (pos, note) is not below (pos, note)
static int notes_lower_bound(const seq_notes_t *a, uint16_t pos, uint8_t note)
{
    int lo = 0, hi = a->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (notes_less(&a->v[mid], pos, note)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static bool notes_insert(seq_engine_t *e, seq_notes_t *a, const seq_note_t *nt)
{
    if (a->n >= SEQ_MAX_NOTES || e->song_notes >= SEQ_MAX_SONG_NOTES) return false;
    if (a->n == a->cap) {
        int cap = a->cap + NOTES_CHUNK;
        if (cap > SEQ_MAX_NOTES) cap = SEQ_MAX_NOTES;
        seq_note_t *v = (seq_note_t *)mem_realloc(a->v, (size_t)cap * sizeof(seq_note_t));
        if (!v) return false;
        a->v = v;
        a->cap = (uint16_t)cap;
    }
    int i = notes_lower_bound(a, nt->pos, nt->note);
    memmove(&a->v[i + 1], &a->v[i], (size_t)(a->n - i) * sizeof(seq_note_t));
    a->v[i] = *nt;
    a->n++;
    e->song_notes++;
    return true;
}

static void notes_erase(seq_engine_t *e, seq_notes_t *a, int i)
{
    memmove(&a->v[i], &a->v[i + 1], (size_t)(a->n - i - 1) * sizeof(seq_note_t));
    a->n--;
    e->song_notes--;
}

static bool notes_copy(seq_engine_t *e, seq_notes_t *dst, const seq_notes_t *src)
{
    dst->v = NULL;
    dst->n = 0;
    dst->cap = 0;
    if (src->n == 0) return true;
    if (e->song_notes + src->n > SEQ_MAX_SONG_NOTES) return false;
    dst->v = (seq_note_t *)mem_realloc(NULL, (size_t)src->n * sizeof(seq_note_t));
    if (!dst->v) return false;
    memcpy(dst->v, src->v, (size_t)src->n * sizeof(seq_note_t));
    dst->n = src->n;
    dst->cap = src->n;
    e->song_notes += src->n;
    return true;
}

// --- Sounding notes ----------------------------------------------------------

static void sounding_off(seq_engine_t *e, seq_sounding_t *s)
{
    if (!s->used) return;
    s->used = false;
    if (e->out.note_off) e->out.note_off(e->out.ctx, s->token, s->channel, s->note);
}

static void all_notes_off(seq_engine_t *e)
{
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) sounding_off(e, &e->sounding[i]);
}

void seq_line_release(seq_engine_t *e, int line)
{
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        if (e->sounding[i].used && e->sounding[i].line == line) sounding_off(e, &e->sounding[i]);
    }
}

// A note removed from the playing pattern stops now rather than at its end
static void release_note(seq_engine_t *e, int pattern, int line, uint8_t note)
{
    if (!e->running || pattern != e->play_pattern) return;
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        seq_sounding_t *s = &e->sounding[i];
        if (s->used && s->line == line && s->note == note) sounding_off(e, s);
    }
}

static void fire(seq_engine_t *e, int line, uint8_t channel, uint8_t note, uint8_t velocity,
                 uint32_t duration)
{
    if (duration < 1) duration = 1;
    seq_sounding_t *slot = NULL;
    // The same note still sounding on this line and channel ends first
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        seq_sounding_t *s = &e->sounding[i];
        if (s->used && s->line == line && s->channel == channel && s->note == note) {
            sounding_off(e, s);
            slot = s;
            break;
        }
    }
    if (!slot) {
        seq_sounding_t *earliest = NULL;
        for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
            seq_sounding_t *s = &e->sounding[i];
            if (!s->used) {
                slot = s;
                break;
            }
            if (!earliest || (int32_t)(s->off_tick - earliest->off_tick) < 0) earliest = s;
        }
        if (!slot) {
            // Full: the one due to end first makes room
            sounding_off(e, earliest);
            slot = earliest;
        }
    }
    uint32_t token = e->out.note_on ? e->out.note_on(e->out.ctx, channel, note, velocity) : 0;
    slot->used = true;
    slot->line = (uint8_t)line;
    slot->channel = channel;
    slot->note = note;
    slot->token = token;
    slot->off_tick = e->song_tick + duration;
}

// --- Song --------------------------------------------------------------------

static void line_defaults(seq_line_t *l, int channel)
{
    memset(l, 0, sizeof(*l));
    l->mode = SEQ_MODE_GRID;
    l->channel = (uint8_t)(channel & 0x0F);
    l->velocity = 0;
    l->gate = 100;
}

static void pline_defaults(seq_pattern_line_t *pl)
{
    pl->steps = 16;
    pl->rate = SEQ_RATE_16;
    pl->bars = 1;
    pl->flags = 0;
    pl->grid.v = NULL;
    pl->grid.n = pl->grid.cap = 0;
    pl->rt.v = NULL;
    pl->rt.n = pl->rt.cap = 0;
}

static void pattern_free(seq_engine_t *e, int p)
{
    seq_pattern_t *pat = e->patterns[p];
    if (!pat) return;
    for (int l = 0; l < SEQ_MAX_LINES; l++) {
        notes_free(e, &pat->lines[l].grid);
        notes_free(e, &pat->lines[l].rt);
    }
    mem_free(pat);
    e->patterns[p] = NULL;
}

seq_engine_t *seq_engine_new(const seq_output_t *out)
{
    seq_engine_t *e = (seq_engine_t *)mem_calloc(1, sizeof(seq_engine_t));
    if (!e) return NULL;
    if (out) e->out = *out;
    seq_engine_reset(e, SEQ_DEFAULT_LINES);
    return e;
}

void seq_engine_free(seq_engine_t *e)
{
    if (!e) return;
    all_notes_off(e);
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) pattern_free(e, p);
    mem_free(e);
}

void seq_engine_reset(seq_engine_t *e, int lines)
{
    seq_engine_stop(e);
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) pattern_free(e, p);
    if (lines < 1) lines = 1;
    if (lines > SEQ_MAX_LINES) lines = SEQ_MAX_LINES;
    e->line_count = (uint8_t)lines;
    for (int l = 0; l < SEQ_MAX_LINES; l++) line_defaults(&e->lines[l], l);
    e->chain_len = 0;
    e->chain_on = false;
    e->name[0] = '\0';
    e->bpm_x100 = 0;
    e->song_notes = 0;
    e->song_tick = 0;
    e->pattern_tick = 0;
    e->play_pattern = 0;
    e->next_pattern = -1;
    e->edit_pattern = -1;
    e->switch_mode = SEQ_SWITCH_PATTERN;
    e->chain_pos = 0;
    e->chain_repeat = 0;
    e->pending = 0;
    e->edits++;
    e->pattern_changes++;
    e->chord[0] = 60;
    e->chord_vel[0] = 100;
    e->chord_n = 1;
    e->held_count = 0;
    memset(e->held, 0, sizeof(e->held));
}

// --- Patterns ----------------------------------------------------------------

int seq_resolve_pattern(const seq_engine_t *e, int pattern)
{
    if (pattern == SEQ_PATTERN_EDIT) {
        pattern = e->edit_pattern >= 0 ? e->edit_pattern : e->play_pattern;
    }
    if (pattern < 0 || pattern >= SEQ_MAX_PATTERNS) return -1;
    return pattern;
}

seq_pattern_t *seq_pattern_get(seq_engine_t *e, int pattern, bool create)
{
    int p = seq_resolve_pattern(e, pattern);
    if (p < 0) return NULL;
    if (!e->patterns[p] && create) {
        seq_pattern_t *pat = (seq_pattern_t *)mem_calloc(1, sizeof(seq_pattern_t));
        if (!pat) return NULL;
        pat->length = 0;
        for (int l = 0; l < SEQ_MAX_LINES; l++) pline_defaults(&pat->lines[l]);
        e->patterns[p] = pat;
    }
    return e->patterns[p];
}

static const seq_pattern_t *pattern_peek(const seq_engine_t *e, int pattern)
{
    int p = seq_resolve_pattern(e, pattern);
    return p < 0 ? NULL : e->patterns[p];
}

static bool line_ok(const seq_engine_t *e, int line)
{
    return line >= 0 && line < e->line_count;
}

bool seq_pattern_clear(seq_engine_t *e, int pattern)
{
    int p = seq_resolve_pattern(e, pattern);
    if (p < 0) return false;
    if (e->running && p == e->play_pattern) all_notes_off(e);
    pattern_free(e, p);
    e->edits++;
    return true;
}

bool seq_pattern_copy(seq_engine_t *e, int src, int dst)
{
    int s = seq_resolve_pattern(e, src);
    int d = seq_resolve_pattern(e, dst);
    if (s < 0 || d < 0) return false;
    if (s == d) return true;
    if (!e->patterns[s]) return seq_pattern_clear(e, d);

    // Over the song's limit: refuse before the destination is touched
    int src_n = 0, dst_n = 0;
    for (int l = 0; l < SEQ_MAX_LINES; l++) {
        src_n += e->patterns[s]->lines[l].grid.n + e->patterns[s]->lines[l].rt.n;
        if (e->patterns[d]) dst_n += e->patterns[d]->lines[l].grid.n + e->patterns[d]->lines[l].rt.n;
    }
    if (e->song_notes - dst_n + src_n > SEQ_MAX_SONG_NOTES) return false;

    seq_pattern_t *copy = (seq_pattern_t *)mem_calloc(1, sizeof(seq_pattern_t));
    if (!copy) return false;
    *copy = *e->patterns[s];
    // The destination's notes go first, so they do not count against the copy
    if (e->running && d == e->play_pattern) all_notes_off(e);
    pattern_free(e, d);
    bool ok = true;
    for (int l = 0; l < SEQ_MAX_LINES && ok; l++) {
        seq_pattern_line_t *from = &e->patterns[s]->lines[l];
        ok = notes_copy(e, &copy->lines[l].grid, &from->grid) &&
             notes_copy(e, &copy->lines[l].rt, &from->rt);
    }
    if (!ok) {
        // Over a limit: undo what was copied
        for (int l = 0; l < SEQ_MAX_LINES; l++) {
            if (copy->lines[l].grid.v != e->patterns[s]->lines[l].grid.v) notes_free(e, &copy->lines[l].grid);
            if (copy->lines[l].rt.v != e->patterns[s]->lines[l].rt.v) notes_free(e, &copy->lines[l].rt);
        }
        mem_free(copy);
        e->edits++;
        return false;
    }
    e->patterns[d] = copy;
    e->edits++;
    return true;
}

bool seq_pattern_set_length(seq_engine_t *e, int pattern, int bars)
{
    if (bars < 0 || bars > SEQ_MAX_BARS) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    pat->length = (uint8_t)bars;
    e->edits++;
    return true;
}

int seq_rate_ticks(int rate)
{
    if (rate < 0 || rate >= SEQ_RATE_COUNT) return RATE_TICKS[SEQ_RATE_16];
    return RATE_TICKS[rate];
}

int seq_pline_steps(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? pat->lines[line].steps : 16;
}

int seq_pline_rate(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? pat->lines[line].rate : SEQ_RATE_16;
}

int seq_pline_bars(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? pat->lines[line].bars : 1;
}

uint32_t seq_line_ticks(const seq_engine_t *e, int pattern, int line)
{
    if (line < 0 || line >= SEQ_MAX_LINES) return SEQ_TICKS_PER_BAR;
    if (e->lines[line].mode == SEQ_MODE_REALTIME) {
        return (uint32_t)seq_pline_bars(e, pattern, line) * SEQ_TICKS_PER_BAR;
    }
    return (uint32_t)seq_pline_steps(e, pattern, line) *
           (uint32_t)seq_rate_ticks(seq_pline_rate(e, pattern, line));
}

uint32_t seq_pattern_ticks(const seq_engine_t *e, int pattern)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    if (!pat) return SEQ_TICKS_PER_BAR;   // An empty slot plays as a bar of silence
    if (pat->length > 0) return (uint32_t)pat->length * SEQ_TICKS_PER_BAR;
    uint32_t longest = 0;
    for (int l = 0; l < e->line_count; l++) {
        uint32_t t = seq_line_ticks(e, pattern, l);
        if (t > longest) longest = t;
    }
    return longest ? longest : SEQ_TICKS_PER_BAR;
}

static bool valid_steps(int steps)
{
    return steps == 4 || steps == 8 || steps == 16 || steps == 32 || steps == 64;
}

static bool valid_bars(int bars)
{
    return bars == 1 || bars == 2 || bars == 4 || bars == 8;
}

bool seq_pline_set_steps(seq_engine_t *e, int pattern, int line, int steps)
{
    if (!line_ok(e, line) || !valid_steps(steps)) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    pat->lines[line].steps = (uint8_t)steps;
    e->edits++;
    return true;
}

bool seq_pline_set_rate(seq_engine_t *e, int pattern, int line, int rate)
{
    if (!line_ok(e, line) || rate < 0 || rate >= SEQ_RATE_COUNT) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    pat->lines[line].rate = (uint8_t)rate;
    e->edits++;
    return true;
}

bool seq_pline_set_bars(seq_engine_t *e, int pattern, int line, int bars)
{
    if (!line_ok(e, line) || !valid_bars(bars)) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    // Realtime notes past the new end stay, unplayed, until it grows back
    pat->lines[line].bars = (uint8_t)bars;
    e->edits++;
    return true;
}

static void set_play_pattern(seq_engine_t *e, int p)
{
    if (e->play_pattern != p) {
        e->play_pattern = (int8_t)p;
        e->pattern_changes++;
    }
    e->pattern_tick = 0;
}

bool seq_select_pattern(seq_engine_t *e, int pattern)
{
    if (pattern < 0) {
        e->next_pattern = -1;
        return true;
    }
    if (pattern >= SEQ_MAX_PATTERNS) return false;
    e->chain_on = false;   // Picking by hand takes over from the chain
    if (e->running) {
        e->next_pattern = (int8_t)pattern;
    } else {
        e->next_pattern = -1;
        set_play_pattern(e, pattern);
    }
    return true;
}

bool seq_set_chain(seq_engine_t *e, const seq_chain_entry_t *entries, int n)
{
    if (n < 0 || n > SEQ_MAX_CHAIN) return false;
    for (int i = 0; i < n; i++) {
        if (entries[i].pattern >= SEQ_MAX_PATTERNS) return false;
        if (entries[i].repeat < 1 || entries[i].repeat > SEQ_MAX_REPEAT) return false;
    }
    if (n > 0) memcpy(e->chain, entries, (size_t)n * sizeof(seq_chain_entry_t));
    e->chain_len = (uint8_t)n;
    if (e->chain_pos >= n) {
        e->chain_pos = 0;
        e->chain_repeat = 0;
    }
    if (n == 0) e->chain_on = false;
    e->edits++;
    return true;
}

void seq_set_chain_on(seq_engine_t *e, bool on)
{
    if (on && e->chain_len == 0) on = false;
    if (on == e->chain_on) return;
    e->chain_on = on;
    e->next_pattern = -1;
    if (on && !e->running) {
        // Stopped: show where Start will begin
        e->chain_pos = 0;
        e->chain_repeat = 0;
        set_play_pattern(e, e->chain[0].pattern);
    } else if (on) {
        // Playing: the chain begins from its top when this pattern ends
        e->chain_pos = (uint8_t)(e->chain_len - 1);
        e->chain_repeat = (uint8_t)(e->chain[e->chain_pos].repeat - 1);
    }
    e->edits++;
}

// --- Lines -------------------------------------------------------------------

int seq_line_add(seq_engine_t *e)
{
    if (e->line_count >= SEQ_MAX_LINES) return -1;
    uint16_t used = 0;
    for (int l = 0; l < e->line_count; l++) used |= (uint16_t)(1u << e->lines[l].channel);
    int ch = 0;
    while (ch < 16 && (used & (1u << ch))) ch++;
    if (ch == 16) ch = 0;

    int index = e->line_count;
    line_defaults(&e->lines[index], ch);
    // The slot's pattern data was cleared when the line was removed (or never used)
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) {
        seq_pattern_t *pat = e->patterns[p];
        if (pat) {
            notes_free(e, &pat->lines[index].grid);
            notes_free(e, &pat->lines[index].rt);
            pline_defaults(&pat->lines[index]);
        }
    }
    e->line_count++;
    e->edits++;
    return index;
}

bool seq_line_remove(seq_engine_t *e, int line)
{
    if (!line_ok(e, line) || e->line_count <= 1) return false;
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        seq_sounding_t *s = &e->sounding[i];
        if (!s->used) continue;
        if (s->line == line) {
            sounding_off(e, s);
        } else if (s->line > line) {
            s->line--;
        }
    }
    int last = e->line_count - 1;
    memmove(&e->lines[line], &e->lines[line + 1], (size_t)(last - line) * sizeof(seq_line_t));
    line_defaults(&e->lines[last], last);
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) {
        seq_pattern_t *pat = e->patterns[p];
        if (!pat) continue;
        notes_free(e, &pat->lines[line].grid);
        notes_free(e, &pat->lines[line].rt);
        memmove(&pat->lines[line], &pat->lines[line + 1],
                (size_t)(last - line) * sizeof(seq_pattern_line_t));
        pline_defaults(&pat->lines[last]);
    }
    e->line_count--;
    e->edits++;
    return true;
}

// --- Notes -------------------------------------------------------------------

static seq_notes_t *notes_of(seq_pattern_t *pat, int line, int mode)
{
    return mode == SEQ_MODE_REALTIME ? &pat->lines[line].rt : &pat->lines[line].grid;
}

const seq_notes_t *seq_notes_get(const seq_engine_t *e, int pattern, int line, int mode)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    if (!pat || line < 0 || line >= e->line_count) return NULL;
    return mode == SEQ_MODE_REALTIME ? &pat->lines[line].rt : &pat->lines[line].grid;
}

static uint8_t clamp_velocity(int v)
{
    if (v < 1) return 1;
    if (v > 127) return 127;
    return (uint8_t)v;
}

int seq_grid_place(seq_engine_t *e, int pattern, int line, int step, int len,
                   const uint8_t *notes, const uint8_t *velocities, int n)
{
    if (!line_ok(e, line) || step < 0 || step >= SEQ_MAX_STEPS || n < 0 || (n > 0 && !notes)) return -1;
    int p = seq_resolve_pattern(e, pattern);
    seq_pattern_t *pat = seq_pattern_get(e, p, true);
    if (!pat) return -1;
    seq_notes_t *a = &pat->lines[line].grid;
    if (len < 1) len = 1;
    if (step + len > SEQ_MAX_STEPS) len = SEQ_MAX_STEPS - step;

    int placed = 0;
    for (int k = 0; k < n; k++) {
        uint8_t note = notes[k];
        if (note > 127) continue;
        int l = len;
        for (int i = 0; i < a->n; i++) {
            seq_note_t *o = &a->v[i];
            if (o->note != note) continue;
            if (o->pos == step) {
                release_note(e, p, line, note);
                notes_erase(e, a, i);
                i--;
            } else if (o->pos < step && o->pos + o->len > step) {
                o->len = (uint16_t)(step - o->pos);
            } else if (o->pos > step && o->pos < step + l) {
                l = o->pos - step;
            }
        }
        seq_note_t nt = {
            .pos = (uint16_t)step,
            .len = (uint16_t)l,
            .note = note,
            .velocity = clamp_velocity(velocities ? velocities[k] : 100),
            .flags = 0,
            .reserved = 0,
        };
        if (!notes_insert(e, a, &nt)) break;
        placed++;
    }
    e->edits++;
    return placed;
}

int seq_grid_remove(seq_engine_t *e, int pattern, int line, int step, int note)
{
    int p = seq_resolve_pattern(e, pattern);
    seq_pattern_t *pat = p < 0 ? NULL : e->patterns[p];
    if (!pat || !line_ok(e, line)) return 0;
    seq_notes_t *a = &pat->lines[line].grid;
    int removed = 0;
    int i = notes_lower_bound(a, (uint16_t)step, 0);
    while (i < a->n && a->v[i].pos == step) {
        if (note < 0 || a->v[i].note == note) {
            release_note(e, p, line, a->v[i].note);
            notes_erase(e, a, i);
            removed++;
        } else {
            i++;
        }
    }
    if (removed) e->edits++;
    return removed;
}

bool seq_grid_set_length(seq_engine_t *e, int pattern, int line, int step, int len)
{
    int p = seq_resolve_pattern(e, pattern);
    seq_pattern_t *pat = p < 0 ? NULL : e->patterns[p];
    if (!pat || !line_ok(e, line) || step < 0 || step >= SEQ_MAX_STEPS) return false;
    if (len < 1) len = 1;
    seq_notes_t *a = &pat->lines[line].grid;
    bool any = false;
    int i = notes_lower_bound(a, (uint16_t)step, 0);
    for (; i < a->n && a->v[i].pos == step; i++) {
        int l = len;
        if (step + l > SEQ_MAX_STEPS) l = SEQ_MAX_STEPS - step;
        for (int j = i + 1; j < a->n; j++) {
            if (a->v[j].note == a->v[i].note && a->v[j].pos > step) {
                if (a->v[j].pos - step < l) l = a->v[j].pos - step;
                break;
            }
        }
        a->v[i].len = (uint16_t)l;
        any = true;
    }
    if (any) e->edits++;
    return any;
}

bool seq_rt_add(seq_engine_t *e, int pattern, int line, int pos, int len, int note, int velocity)
{
    if (!line_ok(e, line) || note < 0 || note > 127) return false;
    if (pos < 0 || pos >= SEQ_MAX_BARS * SEQ_TICKS_PER_BAR) return false;
    if (len < 1) len = 1;
    if (len > SEQ_MAX_BARS * SEQ_TICKS_PER_BAR) len = SEQ_MAX_BARS * SEQ_TICKS_PER_BAR;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    seq_note_t nt = {
        .pos = (uint16_t)pos,
        .len = (uint16_t)len,
        .note = (uint8_t)note,
        .velocity = clamp_velocity(velocity),
        .flags = 0,
        .reserved = 0,
    };
    bool ok = notes_insert(e, &pat->lines[line].rt, &nt);
    e->edits++;
    return ok;
}

void seq_notes_clear(seq_engine_t *e, int pattern, int line, int mode)
{
    int p = seq_resolve_pattern(e, pattern);
    seq_pattern_t *pat = p < 0 ? NULL : e->patterns[p];
    if (!pat) return;
    for (int l = 0; l < e->line_count; l++) {
        if (line >= 0 && l != line) continue;
        if (e->running && p == e->play_pattern) seq_line_release(e, l);
        if (mode < 0 || mode == SEQ_MODE_GRID) notes_free(e, notes_of(pat, l, SEQ_MODE_GRID));
        if (mode < 0 || mode == SEQ_MODE_REALTIME) notes_free(e, notes_of(pat, l, SEQ_MODE_REALTIME));
    }
    e->edits++;
}

// --- Chord -------------------------------------------------------------------

static void chord_add(seq_engine_t *e, uint8_t note, uint8_t velocity)
{
    e->chord_serial++;
    int i = 0;
    while (i < e->chord_n && e->chord[i] < note) i++;
    if (i < e->chord_n && e->chord[i] == note) {
        e->chord_vel[i] = velocity;
        return;
    }
    if (e->chord_n >= SEQ_MAX_CHORD) return;
    memmove(&e->chord[i + 1], &e->chord[i], (size_t)(e->chord_n - i));
    memmove(&e->chord_vel[i + 1], &e->chord_vel[i], (size_t)(e->chord_n - i));
    e->chord[i] = note;
    e->chord_vel[i] = velocity;
    e->chord_n++;
}

void seq_set_chord(seq_engine_t *e, const uint8_t *notes, const uint8_t *velocities, int n)
{
    e->chord_n = 0;
    e->chord_serial++;
    for (int i = 0; i < n; i++) {
        if (notes[i] > 127) continue;
        chord_add(e, notes[i], clamp_velocity(velocities ? velocities[i] : 100));
    }
}

void seq_engine_input(seq_engine_t *e, uint8_t status, uint8_t note, uint8_t velocity)
{
    if (note > 127) return;
    uint8_t type = status & 0xF0;
    uint32_t bit = 1u << (note & 31);
    bool held = (e->held[note >> 5] & bit) != 0;
    if (type == 0x90 && velocity > 0) {
        // The first note after everything was let go starts a new chord
        if (e->held_count == 0) e->chord_n = 0;
        if (!held) {
            e->held[note >> 5] |= bit;
            e->held_count++;
        }
        chord_add(e, note, velocity);
    } else if (type == 0x80 || type == 0x90) {
        if (held) {
            e->held[note >> 5] &= ~bit;
            e->held_count--;
        }
    }
}

// --- Playback ----------------------------------------------------------------

static void end_of_pattern(seq_engine_t *e)
{
    if (e->chain_on && e->chain_len > 0) {
        if (++e->chain_repeat >= e->chain[e->chain_pos].repeat) {
            e->chain_repeat = 0;
            e->chain_pos = (uint8_t)((e->chain_pos + 1) % e->chain_len);
        }
        set_play_pattern(e, e->chain[e->chain_pos].pattern);
    } else if (e->next_pattern >= 0) {
        int next = e->next_pattern;
        e->next_pattern = -1;
        set_play_pattern(e, next);
    } else {
        e->pattern_tick = 0;
    }
}

static void play_line(seq_engine_t *e, const seq_pattern_t *pat, int line)
{
    const seq_line_t *ln = &e->lines[line];
    if (ln->mute) return;
    const seq_pattern_line_t *pl = &pat->lines[line];
    uint32_t lt = seq_line_ticks(e, e->play_pattern, line);
    uint32_t lpos = e->pattern_tick % lt;

    if (ln->mode == SEQ_MODE_GRID) {
        uint32_t st = (uint32_t)seq_rate_ticks(pl->rate);
        if (lpos % st != 0) return;
        uint16_t step = (uint16_t)(lpos / st);
        const seq_notes_t *a = &pl->grid;
        for (int i = notes_lower_bound(a, step, 0); i < a->n && a->v[i].pos == step; i++) {
            const seq_note_t *nt = &a->v[i];
            uint32_t len = nt->len;
            if (step + len > pl->steps) len = pl->steps - step;   // Cut at the line's end
            uint32_t ticks = len * st;
            uint32_t dur = ticks * ln->gate / 100;
            fire(e, line, ln->channel, nt->note, nt->velocity, dur ? dur : 1);
        }
    } else {
        const seq_notes_t *a = &pl->rt;
        for (int i = notes_lower_bound(a, (uint16_t)lpos, 0); i < a->n && a->v[i].pos == lpos; i++) {
            fire(e, line, ln->channel, a->v[i].note, a->v[i].velocity, a->v[i].len);
        }
    }
}

static void process_tick(seq_engine_t *e)
{
    // Pattern end (or a shorter pattern after an edit): next pattern / loop
    if (e->pattern_tick >= seq_pattern_ticks(e, e->play_pattern)) {
        end_of_pattern(e);
    } else if (e->switch_mode == SEQ_SWITCH_BAR && !e->chain_on && e->next_pattern >= 0 &&
               e->pattern_tick % SEQ_TICKS_PER_BAR == 0 && e->pattern_tick > 0) {
        int next = e->next_pattern;
        e->next_pattern = -1;
        set_play_pattern(e, next);
    }

    // Notes off before notes on, so a repeated note is retriggered
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        seq_sounding_t *s = &e->sounding[i];
        if (s->used && (int32_t)(e->song_tick - s->off_tick) >= 0) sounding_off(e, s);
    }

    const seq_pattern_t *pat = e->patterns[e->play_pattern];
    if (pat) {
        for (int l = 0; l < e->line_count; l++) play_line(e, pat, l);
    }

    e->song_tick++;
    e->pattern_tick++;
}

void seq_engine_start(seq_engine_t *e)
{
    all_notes_off(e);
    e->running = true;
    e->song_tick = 0;
    e->pending = 0;
    e->next_pattern = -1;
    if (e->chain_on && e->chain_len > 0) {
        e->chain_pos = 0;
        e->chain_repeat = 0;
        set_play_pattern(e, e->chain[0].pattern);
    } else {
        e->pattern_tick = 0;
    }
}

void seq_engine_continue(seq_engine_t *e)
{
    e->running = true;
    e->pending = 0;
}

void seq_engine_stop(seq_engine_t *e)
{
    e->running = false;
    e->pending = 0;
    all_notes_off(e);
}

void seq_engine_song_position(seq_engine_t *e, uint16_t sixteenths)
{
    if (e->running) return;
    uint32_t pos = (uint32_t)sixteenths * (SEQ_PPQN / 4);
    e->song_tick = pos;
    if (e->chain_on && e->chain_len > 0) {
        uint32_t total = 0;
        for (int i = 0; i < e->chain_len; i++) {
            total += seq_pattern_ticks(e, e->chain[i].pattern) * e->chain[i].repeat;
        }
        pos %= total;
        for (int i = 0; i < e->chain_len; i++) {
            uint32_t len = seq_pattern_ticks(e, e->chain[i].pattern);
            uint32_t span = len * e->chain[i].repeat;
            if (pos < span) {
                e->chain_pos = (uint8_t)i;
                e->chain_repeat = (uint8_t)(pos / len);
                set_play_pattern(e, e->chain[i].pattern);
                e->pattern_tick = pos % len;
                return;
            }
            pos -= span;
        }
    } else {
        e->pattern_tick = pos % seq_pattern_ticks(e, e->play_pattern);
    }
}

void seq_engine_clock(seq_engine_t *e)
{
    if (!e->running) return;
    // A clock early on its interpolation: catch up first, never skip a tick
    while (e->pending > 0) {
        process_tick(e);
        e->pending--;
    }
    process_tick(e);
    e->pending = SEQ_TICKS_PER_CLOCK - 1;
}

bool seq_engine_subtick(seq_engine_t *e)
{
    if (!e->running || e->pending == 0) return false;
    process_tick(e);
    e->pending--;
    return e->pending > 0;
}

int seq_engine_pending(const seq_engine_t *e)
{
    return e->pending;
}
