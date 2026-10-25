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

static void rec_drop_all(seq_engine_t *e);

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
    pl->program = SEQ_PROGRAM_NONE;
    pl->bank_msb = SEQ_PROGRAM_NONE;
    pl->bank_lsb = SEQ_PROGRAM_NONE;
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
    e->armed_line = -1;
    e->rec_pass = 0;
    rec_drop_all(e);
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

bool seq_pline_copy(seq_engine_t *e, int src_pattern, int src_line, int dst_pattern, int dst_line)
{
    int s = seq_resolve_pattern(e, src_pattern);
    int d = seq_resolve_pattern(e, dst_pattern);
    if (s < 0 || d < 0 || !line_ok(e, src_line) || !line_ok(e, dst_line)) return false;
    if (s == d && src_line == dst_line) return true;
    seq_pattern_t *dst = seq_pattern_get(e, d, true);
    if (!dst) return false;
    seq_pattern_line_t *to = &dst->lines[dst_line];
    const seq_pattern_t *src = e->patterns[s];
    if (!src) {
        // An empty slot's line: defaults, no notes
        if (e->running && d == e->play_pattern) seq_line_release(e, dst_line);
        notes_free(e, &to->grid);
        notes_free(e, &to->rt);
        pline_defaults(to);
        e->edits++;
        return true;
    }
    const seq_pattern_line_t *from = &src->lines[src_line];
    int need = from->grid.n + from->rt.n - to->grid.n - to->rt.n;
    if (e->song_notes + need > SEQ_MAX_SONG_NOTES) return false;
    seq_notes_t grid, rt;
    if (!notes_copy(e, &grid, &from->grid)) return false;
    if (!notes_copy(e, &rt, &from->rt)) {
        notes_free(e, &grid);
        return false;
    }
    if (e->running && d == e->play_pattern) seq_line_release(e, dst_line);
    notes_free(e, &to->grid);
    notes_free(e, &to->rt);
    to->steps = from->steps;
    to->rate = from->rate;
    to->bars = from->bars;
    to->flags = from->flags;
    to->program = from->program;
    to->bank_msb = from->bank_msb;
    to->bank_lsb = from->bank_lsb;
    to->grid = grid;
    to->rt = rt;
    e->edits++;
    return true;
}

static int none_or(uint8_t v)
{
    return v == SEQ_PROGRAM_NONE ? -1 : v;
}

int seq_pline_program(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? none_or(pat->lines[line].program) : -1;
}

int seq_pline_bank_msb(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? none_or(pat->lines[line].bank_msb) : -1;
}

int seq_pline_bank_lsb(const seq_engine_t *e, int pattern, int line)
{
    const seq_pattern_t *pat = pattern_peek(e, pattern);
    return (pat && line >= 0 && line < SEQ_MAX_LINES) ? none_or(pat->lines[line].bank_lsb) : -1;
}

bool seq_pline_set_program(seq_engine_t *e, int pattern, int line, int program)
{
    if (!line_ok(e, line) || program < -1 || program > 127) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    pat->lines[line].program = program < 0 ? SEQ_PROGRAM_NONE : (uint8_t)program;
    e->edits++;
    return true;
}

bool seq_pline_set_bank(seq_engine_t *e, int pattern, int line, int msb, int lsb)
{
    if (!line_ok(e, line) || msb < -1 || msb > 127 || lsb < -1 || lsb > 127) return false;
    seq_pattern_t *pat = seq_pattern_get(e, pattern, true);
    if (!pat) return false;
    pat->lines[line].bank_msb = msb < 0 ? SEQ_PROGRAM_NONE : (uint8_t)msb;
    pat->lines[line].bank_lsb = lsb < 0 ? SEQ_PROGRAM_NONE : (uint8_t)lsb;
    e->edits++;
    return true;
}

void seq_send_programs(seq_engine_t *e, int line)
{
    const seq_pattern_t *pat = e->patterns[e->play_pattern];
    if (!pat || !e->out.message) return;
    for (int l = 0; l < e->line_count; l++) {
        if (line >= 0 && l != line) continue;
        const seq_pattern_line_t *pl = &pat->lines[l];
        if (pl->program == SEQ_PROGRAM_NONE) continue;
        uint8_t ch = e->lines[l].channel & 0x0F;
        if (pl->bank_msb != SEQ_PROGRAM_NONE) e->out.message(e->out.ctx, 0xB0 | ch, 0, pl->bank_msb);
        if (pl->bank_lsb != SEQ_PROGRAM_NONE) e->out.message(e->out.ctx, 0xB0 | ch, 32, pl->bank_lsb);
        e->out.message(e->out.ctx, 0xC0 | ch, pl->program, 0);
    }
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
        int from = e->play_pattern;
        e->play_pattern = (int8_t)p;
        if (e->out.event) e->out.event(e->out.ctx, SEQ_EVENT_PATTERN, p, from, 0);
        e->pattern_changes++;
        // A different pattern: its sounds (the same one looping sends nothing again)
        e->programs_due = true;
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
    if (e->armed_line == line) {
        rec_drop_all(e);   // Its notes go with it
        e->armed_line = -1;
    } else if (e->armed_line > line) {
        e->armed_line--;
    }
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

// --- Realtime recording --------------------------------------------------------

bool seq_recording(const seq_engine_t *e)
{
    return e->running && e->armed_line >= 0 && e->armed_line < e->line_count &&
           e->lines[e->armed_line].mode == SEQ_MODE_REALTIME;
}

int seq_rec_open(const seq_engine_t *e)
{
    int n = 0;
    for (int i = 0; i < SEQ_REC_OPEN; i++) n += e->rec[i].used ? 1 : 0;
    return n;
}

void seq_rec_restart(seq_engine_t *e)
{
    e->rec_pass = 0;
}

static bool rec_holds(const seq_engine_t *e, uint8_t note)
{
    for (int i = 0; i < SEQ_REC_OPEN; i++) {
        if (e->rec[i].used && e->rec[i].note == note) return true;
    }
    return false;
}

// Where an input lands: its song tick, and its tick in the armed line of the
// playing pattern. The tick played last, or (`next`, or nothing played yet)
// the one to play next.
static void rec_now(const seq_engine_t *e, bool next, uint32_t *song, uint32_t *lpos)
{
    uint32_t pt;
    if (next || e->pattern_tick == 0 || e->song_tick == 0) {
        *song = e->song_tick;
        pt = e->pattern_tick >= seq_pattern_ticks(e, e->play_pattern) ? 0 : e->pattern_tick;
    } else {
        *song = e->song_tick - 1;
        pt = e->pattern_tick - 1;
    }
    uint32_t lt = seq_line_ticks(e, e->play_pattern, e->armed_line);
    *lpos = lt ? pt % lt : 0;
}

// Replace: the armed line's notes starting at `pos` go
static void rec_erase_at(seq_engine_t *e, int pattern, uint32_t pos)
{
    seq_pattern_t *pat = e->patterns[pattern];
    if (!pat) return;
    seq_notes_t *a = &pat->lines[e->armed_line].rt;
    int i = notes_lower_bound(a, (uint16_t)pos, 0);
    bool any = false;
    while (i < a->n && a->v[i].pos == pos) {
        notes_erase(e, a, i);
        any = true;
    }
    if (any) e->edits++;
}

// A held note let go (or cut where it is): into the line it was played on
static void rec_commit(seq_engine_t *e, seq_rec_note_t *r, uint32_t song_end)
{
    r->used = false;
    int line = e->armed_line;
    if (line < 0 || line >= e->line_count) return;
    uint32_t lt = seq_line_ticks(e, r->pattern, line);
    if (lt == 0) return;
    uint32_t len = song_end - r->start;
    if ((int32_t)len < 1) len = 1;
    if (len > lt) len = lt;   // Held past a whole loop: one loop long
    uint32_t pos = r->pos;
    uint8_t q = e->lines[line].quantize;
    if (q > 0 && q <= SEQ_RATE_COUNT) {
        uint32_t qt = (uint32_t)seq_rate_ticks(q - 1);
        pos = (pos + qt / 2) / qt * qt % lt;   // The nearest; the length stays
    }
    // Replace: a note moved ahead of the pass clears its way there now, so the
    // pass does not take it out when it gets there
    if (e->rec_pass == 1 && r->pattern == e->play_pattern) {
        uint32_t d = (pos + lt - e->rec_next) % lt;
        if (d < e->rec_left && d >= e->rec_ahead) {
            for (uint32_t k = e->rec_ahead; k <= d; k++) rec_erase_at(e, r->pattern, (e->rec_next + k) % lt);
            e->rec_ahead = (uint16_t)(d + 1);
        }
    }
    if (!seq_rt_add(e, r->pattern, line, (int)pos, (int)len, r->note, r->velocity)) e->rec_dropped++;
}

static void rec_close_all(seq_engine_t *e, uint32_t song_end)
{
    for (int i = 0; i < SEQ_REC_OPEN; i++) {
        if (e->rec[i].used) rec_commit(e, &e->rec[i], song_end);
    }
}

// Notes held as nothing (their line or pattern is going away)
static void rec_drop_all(seq_engine_t *e)
{
    for (int i = 0; i < SEQ_REC_OPEN; i++) e->rec[i].used = false;
}

bool seq_set_armed(seq_engine_t *e, int line)
{
    if (line >= 0 && (!line_ok(e, line) || e->lines[line].mode != SEQ_MODE_REALTIME)) return false;
    if (line < 0) line = -1;
    if (line == e->armed_line) return true;
    rec_close_all(e, e->song_tick);
    e->armed_line = (int8_t)line;
    e->rec_pass = 0;
    return true;
}

static void rec_input(seq_engine_t *e, bool on, uint8_t note, uint8_t velocity, bool next)
{
    uint32_t song, lpos;
    rec_now(e, next, &song, &lpos);
    seq_rec_note_t *r = NULL;
    for (int i = 0; i < SEQ_REC_OPEN; i++) {
        if (e->rec[i].used && e->rec[i].note == note) r = &e->rec[i];
    }
    if (r) rec_commit(e, r, song);   // Its note off, or a second note on without one
    if (!on) return;
    for (int i = 0; i < SEQ_REC_OPEN; i++) {
        if (!e->rec[i].used) {
            r = &e->rec[i];
            r->used = true;
            r->pattern = e->play_pattern;
            r->note = note;
            r->velocity = clamp_velocity(velocity);
            r->pos = (uint16_t)lpos;
            r->start = song;
            return;
        }
    }
    e->rec_dropped++;   // More held at once than it keeps
}

void seq_engine_input(seq_engine_t *e, uint8_t status, uint8_t note, uint8_t velocity)
{
    seq_engine_input_at(e, status, note, velocity, false);
}

void seq_engine_input_at(seq_engine_t *e, uint8_t status, uint8_t note, uint8_t velocity, bool next)
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
        if (seq_recording(e)) rec_input(e, true, note, velocity, next);
    } else if (type == 0x80 || type == 0x90) {
        if (held) {
            e->held[note >> 5] &= ~bit;
            e->held_count--;
        }
        if (seq_recording(e)) rec_input(e, false, note, 0, next);
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
        // A note being recorded is not played over the finger holding it
        bool recording = line == e->armed_line && seq_recording(e);
        for (int i = notes_lower_bound(a, (uint16_t)lpos, 0); i < a->n && a->v[i].pos == lpos; i++) {
            if (recording && rec_holds(e, a->v[i].note)) continue;
            fire(e, line, ln->channel, a->v[i].note, a->v[i].velocity, a->v[i].len);
        }
    }
}

// The pattern changed under notes being recorded: they end where they were
// played. One that began on this very tick (played a hair early, onto the
// new pattern's downbeat) moves to the new pattern's start instead.
static void rec_pattern_changed(seq_engine_t *e)
{
    for (int i = 0; i < SEQ_REC_OPEN; i++) {
        seq_rec_note_t *r = &e->rec[i];
        if (!r->used || r->pattern == e->play_pattern) continue;
        if (r->start == e->song_tick) {
            r->pattern = e->play_pattern;
            r->pos = 0;
        } else {
            rec_commit(e, r, e->song_tick);
        }
    }
    e->rec_pass = 0;
}

// Replace: the armed line's notes go as the playing position passes them,
// for one loop from when recording began
static void rec_replace_tick(seq_engine_t *e)
{
    int line = e->armed_line;
    if (e->lines[line].rec_mode != 1) return;
    uint32_t lt = seq_line_ticks(e, e->play_pattern, line);
    if (lt == 0) return;
    if (e->rec_pass == 0) {
        e->rec_pass = 1;
        e->rec_left = (uint16_t)lt;
        e->rec_ahead = 0;
    }
    if (e->rec_pass != 1) return;
    uint32_t lpos = e->pattern_tick % lt;
    if (e->rec_ahead > 0) {
        e->rec_ahead--;   // Cleared already by a note quantized ahead
    } else {
        rec_erase_at(e, e->play_pattern, lpos);
    }
    e->rec_next = (uint16_t)((lpos + 1) % lt);
    if (--e->rec_left == 0) e->rec_pass = 2;
}

// Lines watched reaching a step (muted ones and an empty slot's too: the
// position moves all the same). A realtime line's step is a sixteenth.
static void step_events(seq_engine_t *e)
{
    for (int l = 0; l < e->line_count; l++) {
        if (!(e->step_events & (1u << l))) continue;
        uint32_t lt = seq_line_ticks(e, e->play_pattern, l);
        if (lt == 0) continue;
        uint32_t lpos = e->pattern_tick % lt;
        uint32_t unit = e->lines[l].mode == SEQ_MODE_GRID
                            ? (uint32_t)seq_rate_ticks(seq_pline_rate(e, e->play_pattern, l))
                            : SEQ_PPQN / 4;
        if (lpos % unit == 0) e->out.event(e->out.ctx, SEQ_EVENT_STEP, l, (int)(lpos / unit), e->play_pattern);
    }
}

static void process_tick(seq_engine_t *e)
{
    int before = e->play_pattern;
    // Pattern end (or a shorter pattern after an edit): next pattern / loop
    if (e->pattern_tick >= seq_pattern_ticks(e, e->play_pattern)) {
        end_of_pattern(e);
    } else if (e->switch_mode == SEQ_SWITCH_BAR && !e->chain_on && e->next_pattern >= 0 &&
               e->pattern_tick % SEQ_TICKS_PER_BAR == 0 && e->pattern_tick > 0) {
        int next = e->next_pattern;
        e->next_pattern = -1;
        set_play_pattern(e, next);
    }
    if (e->play_pattern != before) rec_pattern_changed(e);
    if (seq_recording(e)) rec_replace_tick(e);

    // Notes off before notes on, so a repeated note is retriggered
    for (int i = 0; i < SEQ_MAX_SOUNDING; i++) {
        seq_sounding_t *s = &e->sounding[i];
        if (s->used && (int32_t)(e->song_tick - s->off_tick) >= 0) sounding_off(e, s);
    }

    // A pattern starting: its sounds before its first notes
    if (e->programs_due) {
        e->programs_due = false;
        seq_send_programs(e, -1);
    }

    const seq_pattern_t *pat = e->patterns[e->play_pattern];
    if (pat) {
        for (int l = 0; l < e->line_count; l++) play_line(e, pat, l);
    }
    if (e->step_events && e->out.event) step_events(e);

    e->song_tick++;
    e->pattern_tick++;
}

void seq_engine_start(seq_engine_t *e)
{
    all_notes_off(e);
    rec_close_all(e, e->song_tick);   // A Start while running: the take so far stays
    e->rec_pass = 0;
    e->running = true;
    e->song_tick = 0;
    e->pending = 0;
    e->next_pattern = -1;
    e->programs_due = true;
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
    e->programs_due = true;
    e->rec_pass = 0;
}

void seq_engine_stop(seq_engine_t *e)
{
    rec_close_all(e, e->song_tick);
    e->rec_pass = 0;
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

// --- Files (.mseq) -----------------------------------------------------------

#define CHUNK_ID(a, b, c, d) \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static const uint32_t ID_HEAD = CHUNK_ID('H', 'E', 'A', 'D');
static const uint32_t ID_LINE = CHUNK_ID('L', 'I', 'N', 'E');
static const uint32_t ID_CHAN = CHUNK_ID('C', 'H', 'A', 'N');
static const uint32_t ID_PATN = CHUNK_ID('P', 'A', 'T', 'N');
static const uint32_t ID_PLIN = CHUNK_ID('P', 'L', 'I', 'N');
static const uint32_t ID_GRID = CHUNK_ID('G', 'R', 'I', 'D');
static const uint32_t ID_RTEV = CHUNK_ID('R', 'T', 'E', 'V');
static const uint32_t ID_CRC  = CHUNK_ID('C', 'R', 'C', ' ');

// Payload sizes (a reader takes longer ones, for fields added later)
#define HEAD_SIZE  24
#define LINE_SIZE  16
#define PATN_SIZE  2
#define PLIN_SIZE  9    // 6 before programs: older files read with none
#define NOTE_SIZE  8

#define DUMP_HEAD  0
#define DUMP_LINES 1
#define DUMP_CHAIN 2
#define DUMP_PATS  3
#define DUMP_CRC   4
#define DUMP_DONE  5

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, int n)
{
    crc = ~crc;
    while (n-- > 0) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

seq_engine_t *seq_song_clone(const seq_engine_t *e)
{
    seq_engine_t *c = (seq_engine_t *)mem_calloc(1, sizeof(seq_engine_t));
    if (!c) return NULL;
    c->line_count = e->line_count;
    memcpy(c->lines, e->lines, sizeof(c->lines));
    memcpy(c->chain, e->chain, sizeof(c->chain));
    c->chain_len = e->chain_len;
    c->chain_on = e->chain_on;
    memcpy(c->name, e->name, sizeof(c->name));
    c->bpm_x100 = e->bpm_x100;
    c->play_pattern = e->play_pattern;
    c->next_pattern = -1;
    c->edit_pattern = -1;
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) {
        const seq_pattern_t *src = e->patterns[p];
        if (!src) continue;
        seq_pattern_t *copy = (seq_pattern_t *)mem_calloc(1, sizeof(seq_pattern_t));
        if (!copy) {
            seq_engine_free(c);
            return NULL;
        }
        *copy = *src;
        // Empty first: a failed copy must not leave the source's arrays to be freed
        for (int l = 0; l < SEQ_MAX_LINES; l++) {
            copy->lines[l].grid = (seq_notes_t){ NULL, 0, 0 };
            copy->lines[l].rt = (seq_notes_t){ NULL, 0, 0 };
        }
        c->patterns[p] = copy;
        for (int l = 0; l < SEQ_MAX_LINES; l++) {
            if (!notes_copy(c, &copy->lines[l].grid, &src->lines[l].grid) ||
                !notes_copy(c, &copy->lines[l].rt, &src->lines[l].rt)) {
                seq_engine_free(c);
                return NULL;
            }
        }
    }
    return c;
}

// Writes a chunk header; returns where its payload starts
static uint8_t *chunk_start(uint8_t *buf, uint32_t id, uint32_t size)
{
    put32(buf, id);
    put32(buf + 4, size);
    return buf + 8;
}

static int chunk_head(uint8_t *buf, const seq_engine_t *e, uint8_t flags)
{
    uint8_t *p = chunk_start(buf, ID_HEAD, HEAD_SIZE);
    memset(p, 0, HEAD_SIZE);
    put16(p, SEQ_PPQN);
    p[2] = e->line_count;
    p[3] = flags;
    put16(p + 4, e->bpm_x100);
    p[6] = (uint8_t)e->play_pattern;
    strncpy((char *)p + 8, e->name, SEQ_SONG_NAME_LEN);
    return 8 + HEAD_SIZE;
}

static int chunk_line(uint8_t *buf, const seq_engine_t *e, int index)
{
    const seq_line_t *l = &e->lines[index];
    uint8_t *p = chunk_start(buf, ID_LINE, LINE_SIZE);
    memset(p, 0, LINE_SIZE);
    p[0] = (uint8_t)index;
    p[1] = l->mode;
    p[2] = l->channel;
    p[3] = l->mute ? 1 : 0;
    p[4] = l->velocity;
    p[5] = l->gate;
    p[6] = l->quantize;
    p[7] = l->rec_mode;
    strncpy((char *)p + 8, l->name, SEQ_NAME_LEN);
    return 8 + LINE_SIZE;
}

static int chunk_chain(uint8_t *buf, const seq_engine_t *e)
{
    uint32_t size = 2 + 2u * e->chain_len;
    uint8_t *p = chunk_start(buf, ID_CHAN, size);
    p[0] = e->chain_len;
    p[1] = 0;
    for (int i = 0; i < e->chain_len; i++) {
        p[2 + i * 2] = e->chain[i].pattern;
        p[3 + i * 2] = e->chain[i].repeat;
    }
    return 8 + (int)size;
}

static int chunk_notes(uint8_t *buf, uint32_t id, int pattern, int line, const seq_notes_t *a)
{
    uint32_t size = 4 + (uint32_t)a->n * NOTE_SIZE;
    uint8_t *p = chunk_start(buf, id, size);
    p[0] = (uint8_t)pattern;
    p[1] = (uint8_t)line;
    put16(p + 2, a->n);
    p += 4;
    for (int i = 0; i < a->n; i++, p += NOTE_SIZE) {
        put16(p, a->v[i].pos);
        put16(p + 2, a->v[i].len);
        p[4] = a->v[i].note;
        p[5] = a->v[i].velocity;
        p[6] = a->v[i].flags;
        p[7] = 0;
    }
    return 8 + (int)size;
}

static bool pline_is_default(const seq_pattern_line_t *pl)
{
    return pl->steps == 16 && pl->rate == SEQ_RATE_16 && pl->bars == 1 && pl->flags == 0 &&
           pl->program == SEQ_PROGRAM_NONE && pl->bank_msb == SEQ_PROGRAM_NONE &&
           pl->bank_lsb == SEQ_PROGRAM_NONE;
}

// The next chunk of the patterns: PATN, then per line PLIN (unless default),
// GRID and RTEV (unless empty). 0 when there are no more.
static int dump_pattern_chunk(const seq_engine_t *e, seq_dump_t *d, uint8_t *buf)
{
    for (;;) {
        if (d->pattern >= SEQ_MAX_PATTERNS) return 0;
        const seq_pattern_t *pat = e->patterns[d->pattern];
        if (!pat) {
            d->pattern++;
            continue;
        }
        if (d->part == 0) {
            d->part = 1;
            d->line = 0;
            uint8_t *p = chunk_start(buf, ID_PATN, PATN_SIZE);
            p[0] = d->pattern;
            p[1] = pat->length;
            return 8 + PATN_SIZE;
        }
        if (d->line >= e->line_count) {
            d->pattern++;
            d->part = 0;
            continue;
        }
        int l = d->line, part = d->part;
        if (++d->part > 3) {
            d->part = 1;
            d->line++;
        }
        const seq_pattern_line_t *pl = &pat->lines[l];
        if (part == 1 && !pline_is_default(pl)) {
            uint8_t *p = chunk_start(buf, ID_PLIN, PLIN_SIZE);
            p[0] = d->pattern;
            p[1] = (uint8_t)l;
            p[2] = pl->steps;
            p[3] = pl->rate;
            p[4] = pl->bars;
            p[5] = pl->flags;
            p[6] = pl->program;
            p[7] = pl->bank_msb;
            p[8] = pl->bank_lsb;
            return 8 + PLIN_SIZE;
        }
        if (part == 2 && pl->grid.n) return chunk_notes(buf, ID_GRID, d->pattern, l, &pl->grid);
        if (part == 3 && pl->rt.n) return chunk_notes(buf, ID_RTEV, d->pattern, l, &pl->rt);
    }
}

void seq_dump_begin(seq_dump_t *d, uint8_t flags)
{
    memset(d, 0, sizeof(*d));
    d->flags = flags;
}

int seq_dump_next(const seq_engine_t *e, seq_dump_t *d, uint8_t *buf, int cap)
{
    if (cap < SEQ_DUMP_MAX_CHUNK) return 0;
    int n = 0;
    while (n == 0) {
        switch (d->phase) {
            case DUMP_HEAD: {
                memcpy(buf, "MSEQ", 4);
                put16(buf + 4, SEQ_FILE_VERSION);
                put16(buf + 6, 0);
                uint8_t flags = (uint8_t)((d->flags & ~SEQ_FILE_CHAIN_ON) | (e->chain_on ? SEQ_FILE_CHAIN_ON : 0));
                n = 8 + chunk_head(buf + 8, e, flags);
                d->phase = DUMP_LINES;
                d->line = 0;
                break;
            }
            case DUMP_LINES:
                if (d->line < e->line_count) {
                    n = chunk_line(buf, e, d->line++);
                } else {
                    d->phase = DUMP_CHAIN;
                }
                break;
            case DUMP_CHAIN:
                d->phase = DUMP_PATS;
                d->pattern = 0;
                d->line = 0;
                d->part = 0;
                if (e->chain_len) n = chunk_chain(buf, e);
                break;
            case DUMP_PATS:
                n = dump_pattern_chunk(e, d, buf);
                if (n == 0) d->phase = DUMP_CRC;
                break;
            case DUMP_CRC:
                // Covers everything before it, not itself
                memcpy(buf, "CRC ", 4);
                put32(buf + 4, 4);
                put32(buf + 8, d->crc);
                d->phase = DUMP_DONE;
                return 12;
            default:
                return 0;
        }
    }
    d->crc = crc32_update(d->crc, buf, n);
    return n;
}

// --- Reading ---

enum { LD_MAGIC = 0, LD_HEADER, LD_PAYLOAD, LD_SKIP, LD_AFTER };

struct seq_loader {
    seq_engine_t *song;
    uint8_t  flags;
    uint8_t  state;
    uint8_t  hdr[8];
    int      hdr_n;
    uint32_t id, size, got;
    uint8_t *buf;
    uint32_t crc;
    bool     seen_head;
    int      error;
};

seq_loader_t *seq_load_begin(void)
{
    seq_loader_t *ld = (seq_loader_t *)calloc(1, sizeof(seq_loader_t));
    if (!ld) return NULL;
    ld->buf = (uint8_t *)mem_calloc(1, SEQ_DUMP_MAX_CHUNK);
    ld->song = seq_engine_new(NULL);
    if (!ld->buf || !ld->song) {
        seq_load_free(ld);
        return NULL;
    }
    ld->state = LD_MAGIC;
    return ld;
}

void seq_load_free(seq_loader_t *ld)
{
    if (!ld) return;
    if (ld->song) seq_engine_free(ld->song);
    mem_free(ld->buf);
    free(ld);
}

static bool steps_ok(int v) { return v == 4 || v == 8 || v == 16 || v == 32 || v == 64; }
static bool bars_ok(int v) { return v == 1 || v == 2 || v == 4 || v == 8; }

static int load_notes(seq_loader_t *ld, bool grid, const uint8_t *p, uint32_t size)
{
    seq_engine_t *s = ld->song;
    if (size < 4) return SEQ_LOAD_BAD_CHUNK;
    int pattern = p[0], line = p[1], count = get16(p + 2);
    if (pattern >= SEQ_MAX_PATTERNS || !s->patterns[pattern] || line >= s->line_count) return SEQ_LOAD_BAD_CHUNK;
    if (size < 4 + (uint32_t)count * NOTE_SIZE) return SEQ_LOAD_BAD_CHUNK;
    if (count > SEQ_MAX_NOTES) return SEQ_LOAD_TOO_MANY_NOTES;
    if (s->song_notes + count > SEQ_MAX_SONG_NOTES) return SEQ_LOAD_TOO_MANY_NOTES;
    seq_notes_t *a = grid ? &s->patterns[pattern]->lines[line].grid : &s->patterns[pattern]->lines[line].rt;
    if (a->n) return SEQ_LOAD_BAD_CHUNK;   // Twice for one line
    if (count == 0) return SEQ_LOAD_OK;
    a->v = (seq_note_t *)mem_realloc(NULL, (size_t)count * sizeof(seq_note_t));
    if (!a->v) return SEQ_LOAD_NO_MEMORY;
    a->cap = (uint16_t)count;
    p += 4;
    for (int i = 0; i < count; i++, p += NOTE_SIZE) {
        seq_note_t nt = { get16(p), get16(p + 2), p[4], p[5], p[6], 0 };
        bool ok = nt.note <= 127 && nt.velocity >= 1 && nt.velocity <= 127 && nt.len >= 1;
        if (grid) {
            ok = ok && nt.pos < SEQ_MAX_STEPS && nt.pos + nt.len <= SEQ_MAX_STEPS;
        } else {
            ok = ok && nt.pos < SEQ_MAX_BARS * SEQ_TICKS_PER_BAR && nt.len <= SEQ_MAX_BARS * SEQ_TICKS_PER_BAR;
        }
        // Strictly sorted by (pos, note)
        if (ok && i > 0) ok = notes_less(&a->v[i - 1], nt.pos, nt.note);
        if (!ok) return SEQ_LOAD_BAD_VALUE;
        a->v[a->n++] = nt;
        s->song_notes++;
    }
    return SEQ_LOAD_OK;
}

static int load_chunk(seq_loader_t *ld, uint32_t id, const uint8_t *p, uint32_t size)
{
    seq_engine_t *s = ld->song;
    if (id == ID_HEAD) {
        if (ld->seen_head || size < HEAD_SIZE) return SEQ_LOAD_BAD_CHUNK;
        ld->seen_head = true;
        if (get16(p) != SEQ_PPQN || p[2] < 1 || p[2] > SEQ_MAX_LINES || p[6] >= SEQ_MAX_PATTERNS) {
            return SEQ_LOAD_BAD_VALUE;
        }
        s->line_count = p[2];
        ld->flags = p[3];
        s->bpm_x100 = get16(p + 4);
        s->play_pattern = (int8_t)p[6];
        memcpy(s->name, p + 8, SEQ_SONG_NAME_LEN);
        s->name[SEQ_SONG_NAME_LEN] = '\0';
        return SEQ_LOAD_OK;
    }
    if (!ld->seen_head) return SEQ_LOAD_BAD_CHUNK;

    if (id == ID_LINE) {
        if (size < LINE_SIZE) return SEQ_LOAD_BAD_CHUNK;
        if (p[0] >= s->line_count) return SEQ_LOAD_BAD_CHUNK;
        if (p[1] > SEQ_MODE_REALTIME || p[2] > 15 || p[4] > 127 || p[5] < 1 || p[5] > 100 ||
            p[6] > SEQ_RATE_COUNT || p[7] > 1) {
            return SEQ_LOAD_BAD_VALUE;
        }
        seq_line_t *l = &s->lines[p[0]];
        l->mode = p[1];
        l->channel = p[2];
        l->mute = p[3] & 1;
        l->velocity = p[4];
        l->gate = p[5];
        l->quantize = p[6];
        l->rec_mode = p[7];
        memcpy(l->name, p + 8, SEQ_NAME_LEN);
        l->name[SEQ_NAME_LEN] = '\0';
        return SEQ_LOAD_OK;
    }
    if (id == ID_CHAN) {
        if (size < 2 || size < 2 + 2u * p[0]) return SEQ_LOAD_BAD_CHUNK;
        if (p[0] > SEQ_MAX_CHAIN) return SEQ_LOAD_BAD_VALUE;
        for (int i = 0; i < p[0]; i++) {
            uint8_t pat = p[2 + i * 2], rep = p[3 + i * 2];
            if (pat >= SEQ_MAX_PATTERNS || rep < 1 || rep > SEQ_MAX_REPEAT) return SEQ_LOAD_BAD_VALUE;
            s->chain[i].pattern = pat;
            s->chain[i].repeat = rep;
        }
        s->chain_len = p[0];
        return SEQ_LOAD_OK;
    }
    if (id == ID_PATN) {
        if (size < PATN_SIZE) return SEQ_LOAD_BAD_CHUNK;
        if (p[0] >= SEQ_MAX_PATTERNS || s->patterns[p[0]]) return SEQ_LOAD_BAD_CHUNK;
        if (p[1] > SEQ_MAX_BARS) return SEQ_LOAD_BAD_VALUE;
        seq_pattern_t *pat = seq_pattern_get(s, p[0], true);
        if (!pat) return SEQ_LOAD_NO_MEMORY;
        pat->length = p[1];
        return SEQ_LOAD_OK;
    }
    if (id == ID_PLIN) {
        if (size < 6) return SEQ_LOAD_BAD_CHUNK;
        if (p[0] >= SEQ_MAX_PATTERNS || !s->patterns[p[0]] || p[1] >= s->line_count) return SEQ_LOAD_BAD_CHUNK;
        if (!steps_ok(p[2]) || p[3] >= SEQ_RATE_COUNT || !bars_ok(p[4])) return SEQ_LOAD_BAD_VALUE;
        seq_pattern_line_t *pl = &s->patterns[p[0]]->lines[p[1]];
        pl->steps = p[2];
        pl->rate = p[3];
        pl->bars = p[4];
        pl->flags = p[5];
        if (size >= PLIN_SIZE) {
            for (int k = 6; k < 9; k++) {
                if (p[k] > 127 && p[k] != SEQ_PROGRAM_NONE) return SEQ_LOAD_BAD_VALUE;
            }
            pl->program = p[6];
            pl->bank_msb = p[7];
            pl->bank_lsb = p[8];
        }
        return SEQ_LOAD_OK;
    }
    if (id == ID_GRID) return load_notes(ld, true, p, size);
    if (id == ID_RTEV) return load_notes(ld, false, p, size);
    return SEQ_LOAD_OK;
}

static bool known_chunk(uint32_t id)
{
    return id == ID_HEAD || id == ID_LINE || id == ID_CHAN || id == ID_PATN ||
           id == ID_PLIN || id == ID_GRID || id == ID_RTEV || id == ID_CRC;
}

// A chunk's payload is all there
static void payload_done(seq_loader_t *ld)
{
    if (ld->id == ID_CRC) {
        if (get32(ld->buf) != ld->crc) {
            ld->error = SEQ_LOAD_BAD_CRC;
        } else if (!ld->seen_head) {
            ld->error = SEQ_LOAD_BAD_CHUNK;
        } else {
            ld->state = LD_AFTER;
        }
        return;
    }
    ld->crc = crc32_update(ld->crc, ld->buf, (int)ld->size);
    ld->error = load_chunk(ld, ld->id, ld->buf, ld->size);
    ld->state = LD_HEADER;
}

int seq_load_feed(seq_loader_t *ld, const uint8_t *data, int len)
{
    if (!ld) return SEQ_LOAD_NO_MEMORY;
    while (len > 0 && ld->error == SEQ_LOAD_OK) {
        switch (ld->state) {
            case LD_MAGIC:
            case LD_HEADER: {
                int take = 8 - ld->hdr_n;
                if (take > len) take = len;
                memcpy(ld->hdr + ld->hdr_n, data, (size_t)take);
                ld->hdr_n += take;
                data += take;
                len -= take;
                if (ld->hdr_n < 8) break;
                ld->hdr_n = 0;
                if (ld->state == LD_MAGIC) {
                    if (memcmp(ld->hdr, "MSEQ", 4) != 0) {
                        ld->error = SEQ_LOAD_BAD_MAGIC;
                    } else if (get16(ld->hdr + 4) != SEQ_FILE_VERSION) {
                        ld->error = SEQ_LOAD_BAD_VERSION;
                    } else {
                        ld->crc = crc32_update(ld->crc, ld->hdr, 8);
                        ld->state = LD_HEADER;
                    }
                    break;
                }
                ld->id = get32(ld->hdr);
                ld->size = get32(ld->hdr + 4);
                ld->got = 0;
                if (ld->id != ID_CRC) ld->crc = crc32_update(ld->crc, ld->hdr, 8);
                if (!known_chunk(ld->id)) {
                    ld->state = LD_SKIP;
                } else if (ld->size > SEQ_DUMP_MAX_CHUNK - 8 || (ld->id == ID_CRC && ld->size != 4)) {
                    ld->error = SEQ_LOAD_BAD_CHUNK;
                } else {
                    ld->state = LD_PAYLOAD;
                    if (ld->size == 0) payload_done(ld);
                }
                break;
            }
            case LD_PAYLOAD: {
                uint32_t take = ld->size - ld->got;
                if (take > (uint32_t)len) take = (uint32_t)len;
                memcpy(ld->buf + ld->got, data, take);
                ld->got += take;
                data += take;
                len -= (int)take;
                if (ld->got == ld->size) payload_done(ld);
                break;
            }
            case LD_SKIP: {
                uint32_t take = ld->size - ld->got;
                if (take > (uint32_t)len) take = (uint32_t)len;
                ld->crc = crc32_update(ld->crc, data, (int)take);
                ld->got += take;
                data += take;
                len -= (int)take;
                if (ld->got == ld->size) ld->state = LD_HEADER;
                break;
            }
            default:
                return SEQ_LOAD_OK;   // Anything after the CRC is ignored
        }
    }
    return ld->error;
}

int seq_load_finish(seq_loader_t *ld, seq_engine_t **song, uint8_t *flags)
{
    *song = NULL;
    if (!ld) return SEQ_LOAD_NO_MEMORY;
    if (ld->error != SEQ_LOAD_OK) return ld->error;
    if (ld->state != LD_AFTER) return SEQ_LOAD_TRUNCATED;
    seq_engine_t *s = ld->song;
    s->chain_on = (ld->flags & SEQ_FILE_CHAIN_ON) && s->chain_len > 0;
    if (flags) *flags = ld->flags;
    ld->song = NULL;
    *song = s;
    return SEQ_LOAD_OK;
}

const char *seq_load_error_name(int err)
{
    switch (err) {
        case SEQ_LOAD_OK:             return "ok";
        case SEQ_LOAD_BAD_MAGIC:      return "not a sequencer file";
        case SEQ_LOAD_BAD_VERSION:    return "unsupported version";
        case SEQ_LOAD_BAD_CHUNK:      return "broken file";
        case SEQ_LOAD_BAD_VALUE:      return "value out of range";
        case SEQ_LOAD_TOO_MANY_NOTES: return "too many notes";
        case SEQ_LOAD_BAD_CRC:        return "checksum mismatch";
        case SEQ_LOAD_TRUNCATED:      return "file cut short";
        case SEQ_LOAD_NO_MEMORY:      return "out of memory";
        default:                      return "error";
    }
}

void seq_engine_adopt(seq_engine_t *e, seq_engine_t *song)
{
    all_notes_off(e);
    // Another song's lines: nothing stays armed
    rec_drop_all(e);
    e->armed_line = -1;
    e->rec_pass = 0;
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) {
        pattern_free(e, p);
        e->patterns[p] = song->patterns[p];
        song->patterns[p] = NULL;
    }
    e->line_count = song->line_count;
    memcpy(e->lines, song->lines, sizeof(e->lines));
    memcpy(e->chain, song->chain, sizeof(e->chain));
    e->chain_len = song->chain_len;
    e->chain_on = song->chain_on;
    memcpy(e->name, song->name, sizeof(e->name));
    e->bpm_x100 = song->bpm_x100;
    e->song_notes = song->song_notes;
    song->song_notes = 0;
    e->next_pattern = -1;
    e->edit_pattern = -1;
    e->chain_pos = 0;
    e->chain_repeat = 0;
    set_play_pattern(e, song->play_pattern);
    e->programs_due = true;
    e->edits++;
    seq_engine_free(song);
}
