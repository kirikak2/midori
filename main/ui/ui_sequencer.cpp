// The Sequencer on the device (docs/SEQUENCER.md): its picoruby-midi
// transport, the task that plays it, and the ui_seq_* C API. The song model
// and playback themselves are seq_engine.c.
//
// Whatever arrives on the transport (the clock from MIDI::Clock's esp_timer,
// notes from the MIDI input task, a packet from Ruby) is stamped with the
// time and queued; the sequencer task takes it from there. Forwarding from
// inside send_packet would run a route from inside another route, and keep
// the sender waiting. Between two F8s the task plays three interpolated
// ticks, timed by a one-shot esp_timer from the measured clock period
// (CONFIG_FREERTOS_HZ=100 cannot time 5 ms steps).
//
// The engine is not thread-safe: every call holds s_lock. Its output
// callbacks run with s_lock held and only send MIDI.

#include "ui_sequencer.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

extern "C" {
#include "midi_transport.h"
#include "midi_route.h"
}

static const char* TAG = "UI_SEQ";

static constexpr int QUEUE_LEN = 64;
static constexpr uint32_t TASK_STACK = 4096;
// Above the MIDI input task (4) and the Ruby VM (3): an F8 plays at once
static constexpr UBaseType_t TASK_PRIO = 5;
static constexpr BaseType_t TASK_CORE = 1;
// A longer gap between two F8s (slower than 10 BPM) is the clock stopping
// and starting again, not its period
static constexpr int64_t MAX_CLOCK_GAP_US = 250000;

enum : uint8_t {
    ITEM_PACKET = 0,
    ITEM_SUBTICK,
};

struct Item {
    uint8_t kind;
    uint8_t cin, b1, b2, b3;
    uint32_t gen;      // ITEM_SUBTICK: the clock it belongs to
    int64_t time;      // When the packet arrived
};

static seq_engine_t* s_eng;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static esp_timer_handle_t s_timer;
static uint8_t s_bit;                  // Transport registry bit, 0 if none
static volatile uint8_t s_outMask;     // Destinations of its own, besides the routes
static volatile uint32_t s_dropped;    // Packets the queue had no room for

// Clock timing (sequencer task only, but for s_clockGen read by the timer)
static int64_t s_lastClock;
static int64_t s_period;               // Measured F8 period (µs), 0 until known
static volatile uint32_t s_clockGen;   // Bumped by each clock: stale subticks are dropped

struct Lock {
    Lock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(s_lock); }
};

// --- Output ------------------------------------------------------------------

// The token remembers where the note on went, for its note off: out_mask in
// the low byte, the routes' destinations in the next.
static uint32_t out_note_on(void* ctx, uint8_t channel, uint8_t note, uint8_t velocity)
{
    (void)ctx;
    uint8_t status = 0x90 | (channel & 0x0F);
    // Route destinations go through MIDI_route_dispatch so their filters
    // (MIDI.filter) and listeners (MIDI.on) see the note; out_mask is direct.
    uint8_t route = 0;
    if (s_bit) route = MIDI_route_targets(s_bit, 0x9, status) & (uint8_t)~s_bit;   // Never back into itself
    uint8_t mask = s_outMask & (uint8_t)~route & (uint8_t)~s_bit;
    if (mask) MIDI_transport_send(mask, 0, 0x9, status, note, velocity);
    if (s_bit) MIDI_route_dispatch(s_bit, route, 0x9, status, note, velocity);
    return (uint32_t)mask | ((uint32_t)route << 8);
}

static void out_note_off(void* ctx, uint32_t token, uint8_t channel, uint8_t note)
{
    (void)ctx;
    uint8_t status = 0x80 | (channel & 0x0F);
    uint8_t mask = token & 0xFF;
    uint8_t route = (token >> 8) & 0xFF;
    if (mask) MIDI_transport_send(mask, 0, 0x8, status, note, 0);
    if (s_bit) MIDI_route_dispatch(s_bit, route, 0x8, status, note, 0);
}

// --- Transport ---------------------------------------------------------------

static void enqueue(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    if (!s_queue) return;
    Item item = { ITEM_PACKET, cin, b1, b2, b3, 0, esp_timer_get_time() };
    if (xQueueSend(s_queue, &item, 0) != pdTRUE) s_dropped = s_dropped + 1;
}

static int seq_tx_send_packet(void* ctx, uint8_t cable, uint8_t cin,
                              uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)ctx; (void)cable;
    enqueue(cin, b1, b2, b3);
    return 0;
}

static bool seq_tx_is_connected(void* ctx)
{
    (void)ctx;
    return true;
}

static const midi_transport_ops_t s_transport_ops = {
    .send_packet     = seq_tx_send_packet,
    .read_bytes      = nullptr,     // Its output is pushed, not read
    .bytes_available = nullptr,
    .is_connected    = seq_tx_is_connected,
    .transport_id    = MIDI_TRANSPORT_ID_NONE,   // Not a wire transport
};

static const midi_transport_t s_transport = {
    .ops = &s_transport_ops,
    .ctx = nullptr,
};

// --- Task --------------------------------------------------------------------

static void subtick_timer_cb(void* arg)
{
    (void)arg;
    Item item = { ITEM_SUBTICK, 0, 0, 0, 0, s_clockGen, 0 };
    xQueueSend(s_queue, &item, 0);   // Lost only if the queue is full; the next F8 catches up
}

static void arm_subtick()
{
    if (s_period <= 0) return;   // Unknown yet: the next F8 plays what is due
    esp_timer_stop(s_timer);
    esp_timer_start_once(s_timer, (uint64_t)(s_period / SEQ_TICKS_PER_CLOCK));
}

// No subtick of an earlier clock may run after this point
static void cancel_subticks()
{
    s_clockGen = s_clockGen + 1;
    esp_timer_stop(s_timer);
}

static void on_clock(int64_t time)
{
    int64_t dt = time - s_lastClock;
    if (s_lastClock != 0 && dt > 0 && dt < MAX_CLOCK_GAP_US) {
        s_period = s_period ? (s_period * 3 + dt) / 4 : dt;
    }
    s_lastClock = time;

    cancel_subticks();
    int pending;
    {
        Lock lock;
        seq_engine_clock(s_eng);
        pending = seq_engine_pending(s_eng);
    }
    if (pending > 0) arm_subtick();
}

static void handle_packet(const Item& it)
{
    switch (it.cin & 0x0F) {
        case 0xF:   // Single-byte system messages
            switch (it.b1) {
                case 0xF8:
                    on_clock(it.time);
                    return;
                case 0xFA: {
                    cancel_subticks();
                    Lock lock;
                    seq_engine_start(s_eng);
                    return;
                }
                case 0xFB: {
                    cancel_subticks();
                    Lock lock;
                    seq_engine_continue(s_eng);
                    return;
                }
                case 0xFC: {
                    cancel_subticks();
                    Lock lock;
                    seq_engine_stop(s_eng);
                    return;
                }
                default:
                    return;
            }
        case 0x3:   // Three-byte system common: Song Position Pointer
            if (it.b1 == 0xF2) {
                Lock lock;
                seq_engine_song_position(s_eng, (uint16_t)((it.b2 & 0x7F) | ((it.b3 & 0x7F) << 7)));
            }
            return;
        case 0x8:
        case 0x9: {
            Lock lock;
            seq_engine_input(s_eng, it.b1, it.b2, it.b3);
            return;
        }
        default:
            return;
    }
}

static void seq_task(void* arg)
{
    (void)arg;
    Item it;
    for (;;) {
        if (xQueueReceive(s_queue, &it, portMAX_DELAY) != pdTRUE) continue;
        if (it.kind == ITEM_SUBTICK) {
            if (it.gen != s_clockGen) continue;   // A clock came first and played it
            bool more;
            {
                Lock lock;
                more = seq_engine_subtick(s_eng);
            }
            if (more) arm_subtick();
        } else {
            handle_packet(it);
        }
    }
}

// --- C API -------------------------------------------------------------------

extern "C" {

void ui_seq_init(void)
{
    if (s_eng) return;
    s_lock = xSemaphoreCreateRecursiveMutex();
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(Item));
    seq_output_t out = { out_note_on, out_note_off, nullptr };
    s_eng = s_lock && s_queue ? seq_engine_new(&out) : nullptr;
    esp_timer_create_args_t targs = {};
    targs.callback = subtick_timer_cb;
    targs.name = "seq_subtick";
    if (!s_eng || esp_timer_create(&targs, &s_timer) != ESP_OK ||
        xTaskCreatePinnedToCore(seq_task, "sequencer", TASK_STACK, nullptr, TASK_PRIO,
                                nullptr, TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "cannot start the sequencer");
        if (s_eng) seq_engine_free(s_eng);
        s_eng = nullptr;
        return;
    }

    s_bit = MIDI_transport_register(&s_transport);
    if (s_bit == 0) {
        ESP_LOGW(TAG, "MIDI transport registry full: the sequencer is no transport");
    } else {
        MIDI_route_set_source(s_bit, true);
        // A device on the MIDI Devices screen (both IN and OUT)
        ui_mididev_register("seq", "Sequencer", s_bit, UI_COLOR_GREEN, true);
    }
    ESP_LOGI(TAG, "sequencer ready (bit 0x%02x, %u bytes)", s_bit, (unsigned)sizeof(seq_engine_t));
}

void ui_seq_cleanup(void)
{
    if (!s_eng) return;
    s_outMask = 0;
    cancel_subticks();
    Lock lock;
    seq_engine_stop(s_eng);
}

void ui_seq_reset(int lines)
{
    if (!s_eng) return;
    cancel_subticks();
    Lock lock;
    seq_engine_reset(s_eng, lines);
}

static bool name_is(const char* a, const char* b)
{
    return a && strcmp(a, b) == 0;
}

static bool line_ok(int line)
{
    return line >= 0 && line < s_eng->line_count;
}

bool ui_seq_set_i(int pattern, int line, const char* name, int value)
{
    if (!s_eng || !name) return false;
    Lock lock;
    seq_engine_t* e = s_eng;

    // Song
    if (name_is(name, "out_mask")) {
        s_outMask = (uint8_t)(value & 0xFF) & (uint8_t)~s_bit;
        return true;
    }
    if (name_is(name, "play_pattern")) return seq_select_pattern(e, value);
    if (name_is(name, "edit_pattern")) {
        if (value < -1 || value >= SEQ_MAX_PATTERNS) return false;
        e->edit_pattern = (int8_t)value;
        return true;
    }
    if (name_is(name, "switch")) {
        if (value != SEQ_SWITCH_PATTERN && value != SEQ_SWITCH_BAR) return false;
        e->switch_mode = (uint8_t)value;
        return true;
    }
    if (name_is(name, "chain_on")) {
        seq_set_chain_on(e, value != 0);
        return e->chain_on == (value != 0);
    }

    // Pattern
    if (name_is(name, "length")) return seq_pattern_set_length(e, pattern, value);

    // Pattern line
    if (name_is(name, "steps")) return seq_pline_set_steps(e, pattern, line, value);
    if (name_is(name, "rate"))  return seq_pline_set_rate(e, pattern, line, value);
    if (name_is(name, "bars"))  return seq_pline_set_bars(e, pattern, line, value);

    // Line
    if (!line_ok(line)) return false;
    seq_line_t* l = &e->lines[line];
    if (name_is(name, "mode")) {
        if (value != SEQ_MODE_GRID && value != SEQ_MODE_REALTIME) return false;
        if (l->mode != value) seq_line_release(e, line);
        l->mode = (uint8_t)value;
    } else if (name_is(name, "channel")) {
        if (value < 0 || value > 15) return false;
        // Notes off on the old channel now, not when they end
        if (l->channel != value) seq_line_release(e, line);
        l->channel = (uint8_t)value;
    } else if (name_is(name, "mute")) {
        l->mute = value ? 1 : 0;
    } else if (name_is(name, "velocity")) {
        if (value < 0 || value > 127) return false;
        l->velocity = (uint8_t)value;
    } else if (name_is(name, "gate")) {
        if (value < 1 || value > 100) return false;
        l->gate = (uint8_t)value;
    } else if (name_is(name, "quantize")) {
        if (value < 0 || value > SEQ_RATE_COUNT) return false;
        l->quantize = (uint8_t)value;
    } else if (name_is(name, "rec_mode")) {
        if (value != 0 && value != 1) return false;
        l->rec_mode = (uint8_t)value;
    } else {
        return false;
    }
    e->edits++;
    return true;
}

int ui_seq_get_i(int pattern, int line, const char* name)
{
    if (!name) return 0;
    if (name_is(name, "transport_bit")) return s_bit;
    if (!s_eng) return 0;
    Lock lock;
    seq_engine_t* e = s_eng;

    if (name_is(name, "out_mask"))     return s_outMask;
    if (name_is(name, "running"))      return e->running ? 1 : 0;
    if (name_is(name, "line_count"))   return e->line_count;
    if (name_is(name, "play_pattern")) return e->play_pattern;
    if (name_is(name, "next_pattern")) return e->next_pattern;
    if (name_is(name, "edit_pattern")) return e->edit_pattern;
    if (name_is(name, "switch"))       return e->switch_mode;
    if (name_is(name, "chain_on"))     return e->chain_on ? 1 : 0;
    if (name_is(name, "chain_len"))    return e->chain_len;
    if (name_is(name, "song_tick"))    return (int)e->song_tick;
    if (name_is(name, "pattern_tick")) return (int)e->pattern_tick;
    if (name_is(name, "song_notes"))   return e->song_notes;
    if (name_is(name, "edits"))        return (int)e->edits;
    if (name_is(name, "dropped"))      return (int)s_dropped;

    int p = seq_resolve_pattern(e, pattern);
    if (name_is(name, "length")) return (p >= 0 && e->patterns[p]) ? e->patterns[p]->length : 0;
    if (name_is(name, "used"))   return (p >= 0 && e->patterns[p]) ? 1 : 0;
    if (name_is(name, "ticks")) {
        if (p < 0) return 0;
        return (int)(line < 0 ? seq_pattern_ticks(e, p) : seq_line_ticks(e, p, line));
    }
    if (name_is(name, "steps")) return seq_pline_steps(e, p, line);
    if (name_is(name, "rate"))  return seq_pline_rate(e, p, line);
    if (name_is(name, "bars"))  return seq_pline_bars(e, p, line);

    if (!line_ok(line)) return 0;
    const seq_line_t* l = &e->lines[line];
    if (name_is(name, "mode"))     return l->mode;
    if (name_is(name, "channel"))  return l->channel;
    if (name_is(name, "mute"))     return l->mute;
    if (name_is(name, "velocity")) return l->velocity;
    if (name_is(name, "gate"))     return l->gate;
    if (name_is(name, "quantize")) return l->quantize;
    if (name_is(name, "rec_mode")) return l->rec_mode;
    return 0;
}

bool ui_seq_set_name(int line, const char* name)
{
    if (!s_eng || !name) return false;
    Lock lock;
    if (line < 0) {
        strncpy(s_eng->name, name, SEQ_SONG_NAME_LEN);
        s_eng->name[SEQ_SONG_NAME_LEN] = '\0';
    } else {
        if (!line_ok(line)) return false;
        strncpy(s_eng->lines[line].name, name, SEQ_NAME_LEN);
        s_eng->lines[line].name[SEQ_NAME_LEN] = '\0';
    }
    s_eng->edits++;
    return true;
}

int ui_seq_get_name(int line, char* buf, int size)
{
    if (!buf || size <= 0) return 0;
    buf[0] = '\0';
    if (!s_eng) return 0;
    Lock lock;
    const char* src;
    if (line < 0) {
        src = s_eng->name;
    } else if (line_ok(line)) {
        src = s_eng->lines[line].name;
    } else {
        return 0;
    }
    strncpy(buf, src, (size_t)size - 1);
    buf[size - 1] = '\0';
    return (int)strlen(buf);
}

int ui_seq_add_line(void)
{
    if (!s_eng) return -1;
    Lock lock;
    return seq_line_add(s_eng);
}

bool ui_seq_remove_line(int line)
{
    if (!s_eng) return false;
    Lock lock;
    return seq_line_remove(s_eng, line);
}

bool ui_seq_clear_pattern(int pattern)
{
    if (!s_eng) return false;
    Lock lock;
    return seq_pattern_clear(s_eng, pattern);
}

bool ui_seq_copy_pattern(int src, int dst)
{
    if (!s_eng) return false;
    Lock lock;
    return seq_pattern_copy(s_eng, src, dst);
}

int ui_seq_place(int pattern, int line, int step, int len,
                 const uint8_t* notes, const uint8_t* velocities, int n)
{
    if (!s_eng) return -1;
    Lock lock;
    return seq_grid_place(s_eng, pattern, line, step, len, notes, velocities, n);
}

int ui_seq_remove(int pattern, int line, int step, int note)
{
    if (!s_eng) return 0;
    Lock lock;
    return seq_grid_remove(s_eng, pattern, line, step, note);
}

bool ui_seq_set_length(int pattern, int line, int step, int len)
{
    if (!s_eng) return false;
    Lock lock;
    return seq_grid_set_length(s_eng, pattern, line, step, len);
}

bool ui_seq_add_event(int pattern, int line, int pos, int len, int note, int velocity)
{
    if (!s_eng) return false;
    Lock lock;
    return seq_rt_add(s_eng, pattern, line, pos, len, note, velocity);
}

void ui_seq_clear(int pattern, int line, int mode)
{
    if (!s_eng) return;
    Lock lock;
    seq_notes_clear(s_eng, pattern, line, mode);
}

int ui_seq_notes_read(int pattern, int line, int mode, int start, int* out, int max)
{
    if (!s_eng || !out || max <= 0 || start < 0) return 0;
    Lock lock;
    const seq_notes_t* a = seq_notes_get(s_eng, pattern, line, mode);
    if (!a) return 0;
    int n = 0;
    for (int i = start; i < a->n && n < max; i++, n++) {
        out[n * 4 + 0] = a->v[i].pos;
        out[n * 4 + 1] = a->v[i].len;
        out[n * 4 + 2] = a->v[i].note;
        out[n * 4 + 3] = a->v[i].velocity;
    }
    return n;
}

int ui_seq_get_chord(uint8_t* notes, uint8_t* velocities, int max)
{
    if (!s_eng) return 0;
    Lock lock;
    int n = s_eng->chord_n < max ? s_eng->chord_n : max;
    for (int i = 0; i < n; i++) {
        if (notes) notes[i] = s_eng->chord[i];
        if (velocities) velocities[i] = s_eng->chord_vel[i];
    }
    return n;
}

void ui_seq_set_chord(const uint8_t* notes, const uint8_t* velocities, int n)
{
    if (!s_eng || n < 0 || (n > 0 && !notes)) return;
    Lock lock;
    seq_set_chord(s_eng, notes, velocities, n);
}

int ui_seq_get_chain(uint8_t* patterns, uint8_t* repeats, int max)
{
    if (!s_eng) return 0;
    Lock lock;
    int n = s_eng->chain_len < max ? s_eng->chain_len : max;
    for (int i = 0; i < n; i++) {
        if (patterns) patterns[i] = s_eng->chain[i].pattern;
        if (repeats) repeats[i] = s_eng->chain[i].repeat;
    }
    return n;
}

bool ui_seq_set_chain(const uint8_t* patterns, const uint8_t* repeats, int n)
{
    if (!s_eng || n < 0 || n > SEQ_MAX_CHAIN || (n > 0 && (!patterns || !repeats))) return false;
    seq_chain_entry_t entries[SEQ_MAX_CHAIN];
    for (int i = 0; i < n; i++) {
        entries[i].pattern = patterns[i];
        entries[i].repeat = repeats[i];
    }
    Lock lock;
    return seq_set_chain(s_eng, entries, n);
}

void ui_seq_receive(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    enqueue(cin, b1, b2, b3);
}

}  // extern "C"

// --- For the screen ----------------------------------------------------------

namespace ui_seq {

// Whether two arcs of a circle `period` long overlap: [a, a + alen) and
// [b, b + blen), both lengths at most period
static bool arcs_overlap(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen, uint32_t period)
{
    if (period == 0 || alen == 0 || blen == 0) return false;
    return (a + period - b) % period < blen || (b + period - a) % period < alen;
}

static void fill_line(const seq_engine_t* e, int p, int l, int window, int view, LineView& lv, bool notes)
{
    const seq_line_t& ln = e->lines[l];
    lv.mode = ln.mode;
    lv.steps = (uint8_t)seq_pline_steps(e, p, l);
    lv.rate = (uint8_t)seq_pline_rate(e, p, l);
    lv.bars = (uint8_t)seq_pline_bars(e, p, l);
    lv.length = seq_line_ticks(e, p, l);
    bool grid = ln.mode == SEQ_MODE_GRID;
    lv.cols = grid ? lv.steps : (uint16_t)(lv.bars * 16);
    lv.shown = lv.cols < view ? lv.cols : (uint16_t)view;
    lv.s0 = lv.cols > view ? (uint16_t)(window % lv.cols) : 0;
    lv.pos = (e->running && p == e->play_pattern && lv.length)
                 ? (int32_t)(e->pattern_tick % lv.length) : -1;
    if (!notes) return;

    lv.channel = ln.channel;
    lv.mute = ln.mute;
    memcpy(lv.name, ln.name, sizeof(lv.name));
    lv.n = 0;
    lv.truncated = false;
    lv.lo = 127;
    lv.hi = 0;
    const seq_notes_t* a = seq_notes_get(e, p, l, ln.mode);
    if (!a) return;

    // The arc on screen, in the notes' own units
    uint32_t unit = grid ? 1 : RT_UNIT_TICKS;
    uint32_t period = grid ? lv.steps : lv.length;
    uint32_t vis0 = lv.s0 * unit, visLen = lv.shown * unit;
    for (int i = 0; i < a->n; i++) {
        const seq_note_t& nt = a->v[i];
        if (nt.pos >= period) continue;   // Past the line's end: kept, not played
        if (nt.note < lv.lo) lv.lo = nt.note;
        if (nt.note > lv.hi) lv.hi = nt.note;
        uint32_t len = nt.len;
        if (grid && nt.pos + len > period) len = period - nt.pos;
        if (len > period) len = period;
        if (!arcs_overlap(nt.pos, len, vis0, visLen, period)) continue;
        if (lv.n >= VIEW_NOTES) {
            lv.truncated = true;
            continue;
        }
        lv.notes[lv.n] = nt;
        lv.notes[lv.n].len = (uint16_t)len;
        lv.n++;
    }
}

bool snapshot(View* v, int window, int view, bool notes)
{
    if (!s_eng || !v) return false;
    Lock lock;
    const seq_engine_t* e = s_eng;
    int p = seq_resolve_pattern(e, SEQ_PATTERN_EDIT);
    v->edits = e->edits;
    v->patternChanges = e->pattern_changes;
    v->running = e->running;
    v->chainOn = e->chain_on;
    v->play = e->play_pattern;
    v->next = e->next_pattern;
    v->edit = e->edit_pattern;
    v->shown = (int8_t)p;
    v->lineCount = e->line_count;
    v->chainLen = e->chain_len;
    v->used = 0;
    for (int i = 0; i < SEQ_MAX_PATTERNS; i++) {
        if (e->patterns[i]) v->used |= (uint16_t)(1u << i);
    }
    v->chordN = e->chord_n;
    v->chordSerial = e->chord_serial;
    memcpy(v->chord, e->chord, sizeof(v->chord));
    memcpy(v->name, e->name, sizeof(v->name));
    for (int l = 0; l < e->line_count; l++) fill_line(e, p, l, window, view, v->lines[l], notes);
    return true;
}

uint32_t revision()
{
    if (!s_eng) return 0;
    Lock lock;
    return s_eng->edits + s_eng->pattern_changes;
}

static void chord_velocities(const seq_engine_t* e, int line, uint8_t* vels)
{
    uint8_t fixed = e->lines[line].velocity;
    for (int i = 0; i < e->chord_n; i++) vels[i] = fixed ? fixed : e->chord_vel[i];
}

uint8_t ensure_clock()
{
    if (!s_bit) return 0;
    uint8_t sources = MIDI_route_sources();
    for (int b = 0; b < 8; b++) {
        uint8_t src = (uint8_t)(1u << b);
        if ((sources & src) && (MIDI_route_targets(src, 0xF, 0xF8) & s_bit)) return src;
    }
    uint8_t tempo = ui_tempo_bit();
    if (!tempo || MIDI_route_add(tempo, s_bit, MIDI_ROUTE_CLOCK_ONLY) != 0) return 0;
    ESP_LOGI(TAG, "clock routed from the Tempo");
    return tempo;
}

int place_chord(int line, int step, int len)
{
    if (!s_eng) return -1;
    Lock lock;
    if (line < 0 || line >= s_eng->line_count) return -1;
    uint8_t vels[SEQ_MAX_CHORD];
    chord_velocities(s_eng, line, vels);
    return seq_grid_place(s_eng, SEQ_PATTERN_EDIT, line, step, len, s_eng->chord, vels, s_eng->chord_n);
}

int starting_at(int line, int step, int* longest)
{
    if (longest) *longest = 0;
    if (!s_eng) return 0;
    Lock lock;
    const seq_notes_t* a = seq_notes_get(s_eng, SEQ_PATTERN_EDIT, line, SEQ_MODE_GRID);
    if (!a) return 0;
    int count = 0;
    for (int i = 0; i < a->n; i++) {
        if (a->v[i].pos != step) continue;
        count++;
        if (longest && a->v[i].len > *longest) *longest = a->v[i].len;
    }
    return count;
}

bool replace_with_chord(int line, int step)
{
    if (!s_eng) return false;
    Lock lock;
    int longest = 0;
    if (starting_at(line, step, &longest) == 0) return false;
    seq_grid_remove(s_eng, SEQ_PATTERN_EDIT, line, step, -1);
    uint8_t vels[SEQ_MAX_CHORD];
    chord_velocities(s_eng, line, vels);
    return seq_grid_place(s_eng, SEQ_PATTERN_EDIT, line, step, longest, s_eng->chord, vels,
                          s_eng->chord_n) > 0;
}

}  // namespace ui_seq
