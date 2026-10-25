/*
 * Host-build unit tests for the Sequencer engine (main/ui/seq_engine.c).
 * No FreeRTOS / ESP-IDF needed:
 *
 *     make -C main/ui/host_test
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../seq_engine.h"

#define EXPECT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "FAIL %s:%d  %s=%lld  %s=%lld\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        exit(1); \
    } \
} while (0)

/* What the engine sent: note on / off with the tick they went out on */
typedef struct {
    int on;          /* 1 note on, 0 note off */
    int channel;
    int note;
    int velocity;
    uint32_t tick;   /* song_tick when it went out */
    uint32_t token;
} event_t;

static event_t g_ev[4096];
static int g_n;
static seq_engine_t *g_e;
static uint32_t g_token;

static uint32_t out_on(void *ctx, uint8_t ch, uint8_t note, uint8_t vel)
{
    (void)ctx;
    event_t *ev = &g_ev[g_n++];
    ev->on = 1; ev->channel = ch; ev->note = note; ev->velocity = vel;
    ev->tick = g_e->song_tick;
    ev->token = ++g_token;
    return ev->token;
}

static void out_off(void *ctx, uint32_t token, uint8_t ch, uint8_t note)
{
    (void)ctx;
    event_t *ev = &g_ev[g_n++];
    ev->on = 0; ev->channel = ch; ev->note = note; ev->velocity = 0;
    ev->tick = g_e->song_tick;
    ev->token = token;
}

/* Other messages (bank select, program change), in order, with the event
 * count at the time so their place among the notes can be checked */
typedef struct { int status, d1, d2, at_event; uint32_t tick; } msg_t;
static msg_t g_msg[256];
static int g_msg_n;

static void out_msg(void *ctx, uint8_t status, uint8_t d1, uint8_t d2)
{
    (void)ctx;
    msg_t *m = &g_msg[g_msg_n++];
    m->status = status; m->d1 = d1; m->d2 = d2;
    m->at_event = g_n;
    m->tick = g_e->song_tick;
}

/* Steps and pattern switches (out.event) */
typedef struct { int kind, a, b, c; uint32_t tick; } seq_ev_t;
static seq_ev_t g_sev[4096];
static int g_sev_n;

static void out_event(void *ctx, int kind, int a, int b, int c)
{
    (void)ctx;
    if (g_sev_n >= 4096) return;
    seq_ev_t *ev = &g_sev[g_sev_n++];
    ev->kind = kind; ev->a = a; ev->b = b; ev->c = c;
    ev->tick = g_e->song_tick;
}

static void clear_events(void) { g_n = 0; g_msg_n = 0; g_sev_n = 0; }

/* Plays `ticks` ticks the way the device does: an F8 then three subticks */
static void run_ticks(int ticks)
{
    for (int t = 0; t < ticks; t++) {
        if (seq_engine_pending(g_e) > 0) {
            seq_engine_subtick(g_e);
        } else {
            seq_engine_clock(g_e);
        }
    }
}

static int count_on(int note)
{
    int c = 0;
    for (int i = 0; i < g_n; i++) if (g_ev[i].on && g_ev[i].note == note) c++;
    return c;
}

static const event_t *nth_on(int note, int nth)
{
    for (int i = 0; i < g_n; i++) {
        if (g_ev[i].on && g_ev[i].note == note && nth-- == 0) return &g_ev[i];
    }
    return NULL;
}

static const event_t *off_for(uint32_t token)
{
    for (int i = 0; i < g_n; i++) if (!g_ev[i].on && g_ev[i].token == token) return &g_ev[i];
    return NULL;
}

static void fresh(void)
{
    if (g_e) seq_engine_free(g_e);
    seq_output_t out = { .note_on = out_on, .note_off = out_off, .ctx = NULL, .message = out_msg,
                         .event = out_event };
    g_e = seq_engine_new(&out);
    clear_events();
}

static void test_defaults(void)
{
    printf("defaults\n");
    fresh();
    EXPECT_EQ(g_e->line_count, SEQ_DEFAULT_LINES);
    for (int l = 0; l < SEQ_DEFAULT_LINES; l++) EXPECT_EQ(g_e->lines[l].channel, l);
    EXPECT_EQ(g_e->play_pattern, 0);
    EXPECT_EQ(g_e->patterns[0] == NULL, 1);
    EXPECT_EQ(seq_pattern_ticks(g_e, 0), SEQ_TICKS_PER_BAR);   /* empty slot: a bar */
    EXPECT_EQ(g_e->chord_n, 1);
    EXPECT_EQ(g_e->chord[0], 60);
}

static void test_grid_playback(void)
{
    printf("grid playback\n");
    fresh();
    uint8_t kick = 36;
    seq_grid_place(g_e, 0, 0, 0, 1, &kick, NULL, 1);
    seq_grid_place(g_e, 0, 0, 8, 1, &kick, NULL, 1);
    EXPECT_EQ(seq_pattern_ticks(g_e, 0), 16 * 24);

    /* F8s while stopped do nothing */
    for (int i = 0; i < 10; i++) seq_engine_clock(g_e);
    EXPECT_EQ(g_n, 0);

    seq_engine_start(g_e);
    run_ticks(384 * 2);   /* two loops */
    EXPECT_EQ(count_on(36), 4);
    EXPECT_EQ(nth_on(36, 0)->tick, 0);         /* the F8 after Start is the downbeat */
    EXPECT_EQ(nth_on(36, 1)->tick, 8 * 24);
    EXPECT_EQ(nth_on(36, 2)->tick, 384);       /* back to the top */
    EXPECT_EQ(nth_on(36, 0)->channel, 0);
    EXPECT_EQ(nth_on(36, 0)->velocity, 100);
    /* Gate 100 %, one step: off 24 ticks later */
    EXPECT_EQ(off_for(nth_on(36, 0)->token)->tick, 24);

    /* Stop: what sounds goes off, F8s no longer play */
    clear_events();
    seq_grid_set_length(g_e, 0, 0, 0, 4);
    seq_engine_start(g_e);
    run_ticks(10);
    seq_engine_stop(g_e);
    EXPECT_EQ(g_n, 2);
    EXPECT_EQ(g_ev[1].on, 0);
    run_ticks(400);
    EXPECT_EQ(g_n, 2);
}

static void test_gate_and_retrigger(void)
{
    printf("gate, off before on\n");
    fresh();
    uint8_t n = 60;
    seq_grid_place(g_e, 0, 0, 0, 1, &n, NULL, 1);
    seq_grid_place(g_e, 0, 0, 1, 1, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(30);
    /* At tick 24 the first note's off comes before the second note's on */
    int off_i = -1, on_i = -1;
    for (int i = 0; i < g_n; i++) {
        if (g_ev[i].tick != 24) continue;
        if (!g_ev[i].on && off_i < 0) off_i = i;
        if (g_ev[i].on && on_i < 0) on_i = i;
    }
    EXPECT_EQ(off_i >= 0 && on_i > off_i, 1);

    fresh();
    g_e->lines[0].gate = 50;
    seq_grid_place(g_e, 0, 0, 0, 2, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(60);
    EXPECT_EQ(off_for(nth_on(60, 0)->token)->tick, 24);   /* 48 ticks × 50 % */
}

static void test_chords_and_overlap(void)
{
    printf("chords, same-pitch overlap\n");
    fresh();
    uint8_t chord[3] = { 60, 64, 67 };
    uint8_t vel[3] = { 90, 80, 70 };
    EXPECT_EQ(seq_grid_place(g_e, 0, 1, 0, 4, chord, vel, 3), 3);
    const seq_notes_t *a = seq_notes_get(g_e, 0, 1, SEQ_MODE_GRID);
    EXPECT_EQ(a->n, 3);
    EXPECT_EQ(a->v[0].note, 60);
    EXPECT_EQ(a->v[2].velocity, 70);

    /* A note of the same pitch inside 60's length cuts 60 short */
    uint8_t c = 60;
    seq_grid_place(g_e, 0, 1, 2, 1, &c, NULL, 1);
    EXPECT_EQ(a->v[0].len, 2);
    /* Others keep their length */
    EXPECT_EQ(a->v[1].len, 4);

    /* A new note stops where the next of its pitch starts */
    uint8_t e = 64;
    seq_grid_place(g_e, 0, 1, 8, 1, &e, NULL, 1);
    seq_grid_place(g_e, 0, 1, 6, 8, &e, NULL, 1);
    int found = 0;
    for (int i = 0; i < a->n; i++) {
        if (a->v[i].pos == 6 && a->v[i].note == 64) { EXPECT_EQ(a->v[i].len, 2); found = 1; }
    }
    EXPECT_EQ(found, 1);

    /* Placing over a start replaces it */
    seq_grid_place(g_e, 0, 1, 0, 1, chord, NULL, 1);
    EXPECT_EQ(a->v[0].len, 1);
    EXPECT_EQ(a->v[0].velocity, 100);

    /* Sorted by (pos, note) */
    for (int i = 1; i < a->n; i++) {
        EXPECT_EQ(a->v[i - 1].pos < a->v[i].pos ||
                  (a->v[i - 1].pos == a->v[i].pos && a->v[i - 1].note < a->v[i].note), 1);
    }

    /* Remove one note, then the rest at a step */
    EXPECT_EQ(seq_grid_remove(g_e, 0, 1, 0, 64), 1);
    EXPECT_EQ(seq_grid_remove(g_e, 0, 1, 0, -1), 2);

    /* Length: capped by step 64 and by the next same-pitch note */
    fresh();
    uint8_t d = 50;
    seq_grid_place(g_e, 0, 0, 60, 10, &d, NULL, 1);
    a = seq_notes_get(g_e, 0, 0, SEQ_MODE_GRID);
    EXPECT_EQ(a->v[0].len, 4);
    seq_grid_place(g_e, 0, 0, 10, 1, &d, NULL, 1);
    seq_grid_set_length(g_e, 0, 0, 10, 60);
    EXPECT_EQ(a->v[0].len, 50);   /* the note at 10, up to 60 */
}

static void test_polymeter(void)
{
    printf("lines of different lengths\n");
    fresh();
    uint8_t a = 36, b = 38;
    seq_pline_set_steps(g_e, 0, 0, 16);             /* 384 ticks */
    seq_pline_set_steps(g_e, 0, 1, 4);              /* 96 ticks: four loops a pattern */
    seq_grid_place(g_e, 0, 0, 0, 1, &a, NULL, 1);
    seq_grid_place(g_e, 0, 1, 0, 1, &b, NULL, 1);
    EXPECT_EQ(seq_pattern_ticks(g_e, 0), 384);
    seq_engine_start(g_e);
    run_ticks(384);
    EXPECT_EQ(count_on(36), 1);
    EXPECT_EQ(count_on(38), 4);
    EXPECT_EQ(nth_on(38, 3)->tick, 288);

    /* Steps beyond `steps` are kept but not played */
    fresh();
    seq_pline_set_steps(g_e, 0, 0, 16);
    seq_grid_place(g_e, 0, 0, 20, 1, &a, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(384);
    EXPECT_EQ(count_on(36), 0);
    seq_engine_stop(g_e);
    seq_pline_set_steps(g_e, 0, 0, 32);
    seq_engine_start(g_e);
    run_ticks(768);
    EXPECT_EQ(count_on(36), 1);

    /* Rate: 1/8 steps are 48 ticks */
    fresh();
    seq_pline_set_rate(g_e, 0, 0, SEQ_RATE_8);
    seq_grid_place(g_e, 0, 0, 1, 1, &a, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(100);
    EXPECT_EQ(nth_on(36, 0)->tick, 48);
}

static void test_realtime_playback(void)
{
    printf("realtime notes\n");
    fresh();
    g_e->lines[2].mode = SEQ_MODE_REALTIME;
    seq_pline_set_bars(g_e, 0, 2, 1);
    seq_rt_add(g_e, 0, 2, 13, 50, 72, 99);
    seq_rt_add(g_e, 0, 2, 370, 40, 74, 99);      /* wraps past the loop end */
    seq_engine_start(g_e);
    run_ticks(384 + 40);
    EXPECT_EQ(nth_on(72, 0)->tick, 13);
    EXPECT_EQ(off_for(nth_on(72, 0)->token)->tick, 63);
    EXPECT_EQ(nth_on(74, 0)->tick, 370);
    EXPECT_EQ(off_for(nth_on(74, 0)->token)->tick, 410);
    EXPECT_EQ(nth_on(72, 0)->channel, 2);
}

/* --- Realtime recording ---------------------------------------------------- */

static void key_on(int note, int vel) { seq_engine_input(g_e, 0x90, (uint8_t)note, (uint8_t)vel); }
static void key_off(int note) { seq_engine_input(g_e, 0x80, (uint8_t)note, 0); }

/* The index of a realtime note of (pattern, line), or -1 */
static int rt_find(int pattern, int line, int pos, int note)
{
    const seq_notes_t *a = seq_notes_get(g_e, pattern, line, SEQ_MODE_REALTIME);
    if (!a) return -1;
    for (int i = 0; i < a->n; i++) {
        if (a->v[i].pos == pos && a->v[i].note == note) return i;
    }
    return -1;
}

static int rt_len(int pattern, int line, int pos, int note)
{
    int i = rt_find(pattern, line, pos, note);
    return i < 0 ? -1 : seq_notes_get(g_e, pattern, line, SEQ_MODE_REALTIME)->v[i].len;
}

static int rt_count(int pattern, int line)
{
    const seq_notes_t *a = seq_notes_get(g_e, pattern, line, SEQ_MODE_REALTIME);
    return a ? a->n : 0;
}

/* Line 2 realtime, one bar, armed */
static void fresh_recording(void)
{
    fresh();
    g_e->lines[2].mode = SEQ_MODE_REALTIME;
    EXPECT_EQ(seq_set_armed(g_e, 2), 1);
}

static void test_record_basic(void)
{
    printf("recording: notes, timing\n");
    fresh();
    EXPECT_EQ(g_e->armed_line, -1);
    EXPECT_EQ(seq_set_armed(g_e, 0), 0);            /* A grid line cannot be armed */
    EXPECT_EQ(seq_set_armed(g_e, 9), 0);
    g_e->lines[2].mode = SEQ_MODE_REALTIME;
    EXPECT_EQ(seq_set_armed(g_e, 2), 1);
    EXPECT_EQ(g_e->armed_line, 2);

    /* Stopped: the chord follows, nothing is recorded */
    key_on(70, 90);
    key_off(70);
    EXPECT_EQ(rt_count(0, 2), 0);
    EXPECT_EQ(g_e->chord[0], 70);
    EXPECT_EQ(seq_recording(g_e), 0);

    seq_engine_start(g_e);
    EXPECT_EQ(seq_recording(g_e), 1);
    /* Before the first tick: on tick 0 */
    key_on(60, 80);
    run_ticks(13);                                  /* Ticks 0..12 */
    key_off(60);
    EXPECT_EQ(rt_len(0, 2, 0, 60), 12);
    key_on(72, 90);                                 /* After tick 12 */
    EXPECT_EQ(seq_rec_open(g_e), 1);
    run_ticks(50);                                  /* .. 62 */
    key_off(72);
    EXPECT_EQ(seq_rec_open(g_e), 0);
    EXPECT_EQ(rt_len(0, 2, 12, 72), 50);
    EXPECT_EQ(seq_notes_get(g_e, 0, 2, SEQ_MODE_REALTIME)->v[rt_find(0, 2, 12, 72)].velocity, 90);
    /* Nearer the next tick */
    run_ticks(37);                                  /* .. 99 */
    seq_engine_input_at(g_e, 0x90, 74, 100, true);
    run_ticks(10);
    seq_engine_input_at(g_e, 0x90, 74, 0, true);    /* Velocity 0: a note off */
    EXPECT_EQ(rt_len(0, 2, 100, 74), 10);

    /* It plays on the next loop, on the line's channel */
    clear_events();
    run_ticks(384 - 110 + 120);
    EXPECT_EQ(nth_on(72, 0)->tick, 384 + 12);
    EXPECT_EQ(off_for(nth_on(72, 0)->token)->tick, 384 + 62);
    EXPECT_EQ(nth_on(72, 0)->channel, 2);
    EXPECT_EQ(nth_on(74, 0)->tick, 384 + 100);
    /* Recording does not play what comes in: only what was recorded sounds */
    EXPECT_EQ(count_on(70), 0);
}

static void test_record_overdub_and_hold(void)
{
    printf("recording: overdub, held notes, loop end\n");
    fresh_recording();
    seq_rt_add(g_e, 0, 2, 20, 30, 72, 100);
    seq_engine_start(g_e);
    run_ticks(11);                                  /* .. 10 */
    key_on(72, 100);                                /* Held over the recorded 72 at 20 */
    run_ticks(20);                                  /* .. 30 */
    EXPECT_EQ(count_on(72), 0);                     /* Not played under the finger */
    key_off(72);
    EXPECT_EQ(rt_count(0, 2), 2);                   /* Overdub keeps the old one */
    EXPECT_EQ(rt_len(0, 2, 10, 72), 20);
    EXPECT_EQ(rt_len(0, 2, 20, 72), 30);

    /* Held across the loop end: the length runs past it */
    run_ticks(380 - 31 + 1);                        /* .. 380 */
    key_on(50, 100);
    run_ticks(14);                                  /* .. 394 = 10 in the next loop */
    key_off(50);
    EXPECT_EQ(rt_len(0, 2, 380, 50), 14);
    /* It comes back round: on at 380 of the loop, off 14 later */
    clear_events();
    run_ticks(384);
    EXPECT_EQ(nth_on(50, 0)->tick, 384 + 380);
    EXPECT_EQ(off_for(nth_on(50, 0)->token)->tick, 384 + 394);

    /* Held for more than a loop: one loop long */
    key_on(51, 100);
    run_ticks(500);
    key_off(51);
    EXPECT_EQ(rt_count(0, 2) >= 4, 1);
    const seq_notes_t *a = seq_notes_get(g_e, 0, 2, SEQ_MODE_REALTIME);
    for (int i = 0; i < a->n; i++) {
        if (a->v[i].note == 51) EXPECT_EQ(a->v[i].len, 384);
    }
}

static void test_record_quantize(void)
{
    printf("recording: quantize\n");
    fresh_recording();
    g_e->lines[2].quantize = SEQ_RATE_16 + 1;       /* 24 ticks */
    seq_engine_start(g_e);
    run_ticks(31);                                  /* .. 30 */
    key_on(60, 100);
    run_ticks(7);                                   /* .. 37 */
    key_on(62, 100);
    run_ticks(10);
    key_off(60);
    key_off(62);
    EXPECT_EQ(rt_len(0, 2, 24, 60), 17);            /* Start rounded, length kept */
    EXPECT_EQ(rt_len(0, 2, 48, 62), 10);
    /* Rounded up past the loop end: onto its start */
    run_ticks(380 - 48 + 1);                        /* .. 380 */
    key_on(64, 100);
    run_ticks(2);
    key_off(64);
    EXPECT_EQ(rt_len(0, 2, 0, 64), 2);
}

static void test_record_stop_disarm(void)
{
    printf("recording: stop, disarm, line changes\n");
    fresh_recording();
    seq_engine_start(g_e);
    run_ticks(5);
    key_on(60, 100);
    run_ticks(6);
    seq_engine_stop(g_e);                           /* Held notes end there */
    EXPECT_EQ(seq_rec_open(g_e), 0);
    EXPECT_EQ(rt_len(0, 2, 4, 60), 7);
    key_off(60);
    EXPECT_EQ(rt_count(0, 2), 1);

    seq_engine_continue(g_e);
    key_on(62, 100);
    run_ticks(3);
    EXPECT_EQ(seq_set_armed(g_e, -1), 1);           /* Disarmed: the held note ends */
    EXPECT_EQ(rt_count(0, 2), 2);
    key_off(62);
    key_on(63, 100);
    run_ticks(3);
    key_off(63);
    EXPECT_EQ(rt_count(0, 2), 2);                   /* Nothing armed, nothing recorded */

    /* Removing lines keeps the arm on its line */
    g_e->lines[5].mode = SEQ_MODE_REALTIME;
    seq_set_armed(g_e, 5);
    seq_line_remove(g_e, 1);
    EXPECT_EQ(g_e->armed_line, 4);
    seq_line_remove(g_e, 4);
    EXPECT_EQ(g_e->armed_line, -1);

    /* More held at once than it keeps: counted, not recorded */
    fresh_recording();
    seq_engine_start(g_e);
    for (int n = 0; n < SEQ_REC_OPEN + 2; n++) key_on(40 + n, 100);
    EXPECT_EQ(g_e->rec_dropped, 2);
    run_ticks(2);
    for (int n = 0; n < SEQ_REC_OPEN + 2; n++) key_off(40 + n);
    EXPECT_EQ(rt_count(0, 2), SEQ_REC_OPEN);
}

static void test_record_pattern_switch(void)
{
    printf("recording: pattern switch\n");
    fresh_recording();
    seq_engine_start(g_e);
    seq_select_pattern(g_e, 1);                     /* At the end of this bar */
    run_ticks(370);                                 /* .. 369 */
    key_on(60, 100);
    run_ticks(30);                                  /* Past the switch at 384 */
    EXPECT_EQ(g_e->play_pattern, 1);
    EXPECT_EQ(rt_len(0, 2, 369, 60), 15);           /* Closed at the switch, in pattern 0 */
    key_off(60);
    EXPECT_EQ(rt_count(1, 2), 0);
    key_on(62, 100);                                /* Still armed, into pattern 1 */
    run_ticks(4);
    key_off(62);
    EXPECT_EQ(rt_len(1, 2, 15, 62), 4);             /* Tick 399 = 15 of pattern 1 */

    /* Played a hair early onto the next pattern's downbeat: its start */
    seq_select_pattern(g_e, 0);
    run_ticks(384 - (int)g_e->pattern_tick);        /* Pattern 1's last tick played */
    EXPECT_EQ(g_e->pattern_tick, 384);
    seq_engine_input_at(g_e, 0x90, 65, 100, true);
    run_ticks(5);
    key_off(65);
    EXPECT_EQ(g_e->play_pattern, 0);
    EXPECT_EQ(rt_len(0, 2, 0, 65), 4);              /* Ticks 0..4: off after the 4th */
    EXPECT_EQ(rt_find(1, 2, 0, 65), -1);
}

static void test_record_replace(void)
{
    printf("recording: replace\n");
    fresh_recording();
    g_e->lines[2].rec_mode = 1;
    seq_rt_add(g_e, 0, 2, 10, 5, 70, 100);
    seq_rt_add(g_e, 0, 2, 200, 5, 71, 100);
    seq_engine_start(g_e);
    run_ticks(100);
    EXPECT_EQ(rt_find(0, 2, 10, 70), -1);          /* Passed: gone, and not played */
    EXPECT_EQ(count_on(70), 0);
    EXPECT_EQ(rt_find(0, 2, 200, 71) >= 0, 1);     /* Not reached yet */
    key_on(60, 100);
    run_ticks(10);
    key_off(60);                                    /* Recorded behind the pass: stays */
    run_ticks(300);                                 /* Past 200, and a whole loop */
    EXPECT_EQ(rt_find(0, 2, 200, 71), -1);
    EXPECT_EQ(rt_len(0, 2, 99, 60), 10);
    EXPECT_EQ(g_e->rec_pass, 2);
    /* After one loop it records as overdub */
    clear_events();
    run_ticks(384);
    EXPECT_EQ(count_on(60), 1);
    EXPECT_EQ(rt_len(0, 2, 99, 60), 10);

    /* A note quantized ahead of the pass is not taken out when it gets there */
    fresh_recording();
    g_e->lines[2].rec_mode = 1;
    g_e->lines[2].quantize = SEQ_RATE_16 + 1;
    seq_rt_add(g_e, 0, 2, 47, 5, 69, 100);
    seq_rt_add(g_e, 0, 2, 48, 5, 70, 100);
    seq_rt_add(g_e, 0, 2, 50, 5, 71, 100);
    seq_engine_start(g_e);
    run_ticks(41);                                  /* .. 40 */
    key_on(60, 100);
    run_ticks(4);                                   /* .. 44 */
    key_off(60);                                    /* At 48 */
    EXPECT_EQ(rt_find(0, 2, 47, 69), -1);          /* Its way cleared */
    EXPECT_EQ(rt_find(0, 2, 48, 70), -1);
    EXPECT_EQ(rt_find(0, 2, 50, 71) >= 0, 1);
    run_ticks(20);
    EXPECT_EQ(rt_len(0, 2, 48, 60), 4);
    EXPECT_EQ(rt_find(0, 2, 50, 71), -1);          /* The pass goes on past it */
    EXPECT_EQ(rt_count(0, 2), 1);
}

static void test_events(void)
{
    printf("step and pattern events\n");
    fresh();
    seq_engine_start(g_e);
    run_ticks(100);
    EXPECT_EQ(g_sev_n, 0);                         /* No line watched */

    fresh();
    g_e->lines[2].mode = SEQ_MODE_REALTIME;
    seq_pline_set_rate(g_e, 0, 1, SEQ_RATE_8);     /* Line 1: 48 ticks a step */
    g_e->lines[1].mute = 1;                        /* Muted lines still step */
    g_e->step_events = (1u << 0) | (1u << 1) | (1u << 2);
    seq_engine_start(g_e);
    run_ticks(49);                                 /* Ticks 0..48 */
    int steps0 = 0, steps1 = 0, steps2 = 0;
    for (int i = 0; i < g_sev_n; i++) {
        EXPECT_EQ(g_sev[i].kind, SEQ_EVENT_STEP);
        EXPECT_EQ(g_sev[i].c, 0);
        if (g_sev[i].a == 0) { EXPECT_EQ(g_sev[i].b * 24, g_sev[i].tick); steps0++; }
        if (g_sev[i].a == 1) { EXPECT_EQ(g_sev[i].b * 48, g_sev[i].tick); steps1++; }
        if (g_sev[i].a == 2) { EXPECT_EQ(g_sev[i].b * 24, g_sev[i].tick); steps2++; }
    }
    EXPECT_EQ(steps0, 3);                          /* 0, 24, 48 */
    EXPECT_EQ(steps1, 2);                          /* 0, 48 */
    EXPECT_EQ(steps2, 3);                          /* Sixteenths */

    /* Line 0 loops back to step 0 */
    clear_events();
    g_e->step_events = 1;
    run_ticks(384 - 49 + 1);                       /* .. 384 */
    EXPECT_EQ(g_sev[g_sev_n - 1].b, 0);
    EXPECT_EQ(g_sev[g_sev_n - 1].tick, 384);

    /* A pattern switch: new pattern, the one before */
    clear_events();
    g_e->step_events = 0;
    seq_select_pattern(g_e, 3);
    run_ticks(384);
    EXPECT_EQ(g_sev_n, 1);
    EXPECT_EQ(g_sev[0].kind, SEQ_EVENT_PATTERN);
    EXPECT_EQ(g_sev[0].a, 3);
    EXPECT_EQ(g_sev[0].b, 0);
    /* Stopped, it switches at once; the same pattern is no switch */
    seq_engine_stop(g_e);
    clear_events();
    seq_select_pattern(g_e, 5);
    seq_select_pattern(g_e, 5);
    EXPECT_EQ(g_sev_n, 1);
    EXPECT_EQ(g_sev[0].a, 5);
    EXPECT_EQ(g_sev[0].b, 3);
}

static void test_clock_interpolation(void)
{
    printf("clock and subticks\n");
    fresh();
    g_e->lines[0].mode = SEQ_MODE_REALTIME;
    seq_rt_add(g_e, 0, 0, 1, 1, 61, 100);
    seq_rt_add(g_e, 0, 0, 3, 1, 63, 100);
    seq_engine_start(g_e);
    seq_engine_clock(g_e);                    /* tick 0 */
    EXPECT_EQ(seq_engine_pending(g_e), 3);
    EXPECT_EQ(seq_engine_subtick(g_e), 1);    /* tick 1 */
    EXPECT_EQ(count_on(61), 1);
    /* The next F8 comes early: ticks 2 and 3 are played first, none skipped */
    seq_engine_clock(g_e);                    /* ticks 2, 3, then 4 */
    EXPECT_EQ(count_on(63), 1);
    EXPECT_EQ(g_e->song_tick, 5);
    EXPECT_EQ(seq_engine_pending(g_e), 3);
    /* Subticks never run ahead of the next F8 */
    seq_engine_subtick(g_e);
    seq_engine_subtick(g_e);
    EXPECT_EQ(seq_engine_subtick(g_e), 0);
    EXPECT_EQ(seq_engine_subtick(g_e), 0);
    EXPECT_EQ(g_e->song_tick, 8);
}

static void test_continue_and_spp(void)
{
    printf("continue, song position\n");
    fresh();
    uint8_t n = 40;
    seq_grid_place(g_e, 0, 0, 4, 1, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(50);
    seq_engine_stop(g_e);
    clear_events();
    seq_engine_continue(g_e);
    run_ticks(60);                             /* 50..110: step 4 is tick 96 */
    EXPECT_EQ(count_on(40), 1);
    EXPECT_EQ(nth_on(40, 0)->tick, 96);

    seq_engine_stop(g_e);
    clear_events();
    seq_engine_song_position(g_e, 4);          /* 4 sixteenths = step 4 */
    EXPECT_EQ(g_e->pattern_tick, 96);
    seq_engine_continue(g_e);
    run_ticks(1);
    EXPECT_EQ(count_on(40), 1);

    /* Ignored while running */
    seq_engine_song_position(g_e, 0);
    EXPECT_EQ(g_e->pattern_tick, 97);
}

static void test_mute(void)
{
    printf("mute\n");
    fresh();
    uint8_t n = 45;
    seq_grid_place(g_e, 0, 3, 0, 1, &n, NULL, 1);
    g_e->lines[3].mute = 1;
    seq_engine_start(g_e);
    run_ticks(384);
    EXPECT_EQ(count_on(45), 0);
    g_e->lines[3].mute = 0;
    run_ticks(384);
    EXPECT_EQ(count_on(45), 1);
}

static void test_pattern_switch(void)
{
    printf("pattern switching\n");
    fresh();
    uint8_t a = 36, b = 48;
    seq_grid_place(g_e, 0, 0, 0, 1, &a, NULL, 1);
    seq_grid_place(g_e, 1, 0, 0, 1, &b, NULL, 1);

    /* Stopped: at once */
    seq_select_pattern(g_e, 1);
    EXPECT_EQ(g_e->play_pattern, 1);
    seq_select_pattern(g_e, 0);

    /* Running: at the end of the playing pattern */
    seq_engine_start(g_e);
    run_ticks(100);
    seq_select_pattern(g_e, 1);
    EXPECT_EQ(g_e->play_pattern, 0);
    EXPECT_EQ(g_e->next_pattern, 1);
    run_ticks(284);
    EXPECT_EQ(g_e->play_pattern, 0);
    run_ticks(1);
    EXPECT_EQ(g_e->play_pattern, 1);
    EXPECT_EQ(g_e->next_pattern, -1);
    EXPECT_EQ(nth_on(48, 0)->tick, 384);

    /* Bar switching: a 2-bar pattern changes at the bar line */
    fresh();
    seq_pattern_set_length(g_e, 0, 2);
    seq_grid_place(g_e, 1, 0, 0, 1, &b, NULL, 1);
    g_e->switch_mode = SEQ_SWITCH_BAR;
    seq_engine_start(g_e);
    run_ticks(10);
    seq_select_pattern(g_e, 1);
    run_ticks(384);
    EXPECT_EQ(g_e->play_pattern, 1);
    EXPECT_EQ(nth_on(48, 0)->tick, 384);

    /* An empty slot plays a bar of silence */
    fresh();
    seq_grid_place(g_e, 0, 0, 0, 1, &a, NULL, 1);
    seq_select_pattern(g_e, 5);
    seq_engine_start(g_e);
    run_ticks(384);
    EXPECT_EQ(g_n, 0);
}

static void test_chain(void)
{
    printf("chain\n");
    fresh();
    uint8_t a = 36, b = 48;
    seq_grid_place(g_e, 0, 0, 0, 1, &a, NULL, 1);
    seq_grid_place(g_e, 1, 0, 0, 1, &b, NULL, 1);
    seq_chain_entry_t chain[2] = { { 0, 2 }, { 1, 1 } };
    EXPECT_EQ(seq_set_chain(g_e, chain, 2), 1);
    seq_set_chain_on(g_e, true);
    seq_engine_start(g_e);
    run_ticks(384 * 6);
    /* 0 0 1 0 0 1 */
    EXPECT_EQ(count_on(36), 4);
    EXPECT_EQ(count_on(48), 2);
    EXPECT_EQ(nth_on(48, 0)->tick, 768);
    EXPECT_EQ(nth_on(48, 1)->tick, 384 * 5);

    /* Picking a pattern by hand turns the chain off */
    seq_select_pattern(g_e, 0);
    EXPECT_EQ(g_e->chain_on, 0);

    /* Song position inside the chain: 3 bars in = entry 1 */
    seq_engine_stop(g_e);
    seq_set_chain_on(g_e, true);
    seq_engine_song_position(g_e, 16 * 2 + 4);
    EXPECT_EQ(g_e->chain_pos, 1);
    EXPECT_EQ(g_e->play_pattern, 1);
    EXPECT_EQ(g_e->pattern_tick, 96);
    seq_engine_song_position(g_e, 16 + 2);
    EXPECT_EQ(g_e->chain_pos, 0);
    EXPECT_EQ(g_e->chain_repeat, 1);

    /* Bad entries are refused */
    seq_chain_entry_t bad = { 16, 1 };
    EXPECT_EQ(seq_set_chain(g_e, &bad, 1), 0);
    bad.pattern = 0; bad.repeat = 0;
    EXPECT_EQ(seq_set_chain(g_e, &bad, 1), 0);
}

static void test_pattern_copy_clear(void)
{
    printf("pattern copy / clear\n");
    fresh();
    uint8_t chord[3] = { 60, 64, 67 };
    seq_grid_place(g_e, 0, 0, 0, 2, chord, NULL, 3);
    seq_pline_set_steps(g_e, 0, 0, 32);
    EXPECT_EQ(seq_pattern_copy(g_e, 0, 3), 1);
    EXPECT_EQ(g_e->song_notes, 6);
    EXPECT_EQ(seq_pline_steps(g_e, 3, 0), 32);
    /* The copy is independent */
    seq_grid_remove(g_e, 3, 0, 0, 60);
    EXPECT_EQ(seq_notes_get(g_e, 0, 0, SEQ_MODE_GRID)->n, 3);
    EXPECT_EQ(seq_notes_get(g_e, 3, 0, SEQ_MODE_GRID)->n, 2);
    EXPECT_EQ(g_e->song_notes, 5);
    seq_pattern_clear(g_e, 3);
    EXPECT_EQ(g_e->patterns[3] == NULL, 1);
    EXPECT_EQ(g_e->song_notes, 3);
    /* Copying an empty slot empties the destination */
    seq_pattern_copy(g_e, 7, 0);
    EXPECT_EQ(g_e->patterns[0] == NULL, 1);
    EXPECT_EQ(g_e->song_notes, 0);
}

static void test_pline_copy(void)
{
    printf("line copy\n");
    fresh();
    uint8_t chord[3] = { 60, 64, 67 };
    seq_grid_place(g_e, 0, 1, 0, 2, chord, NULL, 3);
    seq_pline_set_steps(g_e, 0, 1, 32);
    uint8_t other = 40;
    seq_grid_place(g_e, 2, 4, 3, 1, &other, NULL, 1);
    EXPECT_EQ(seq_pline_copy(g_e, 0, 1, 2, 4), 1);
    EXPECT_EQ(seq_notes_get(g_e, 2, 4, SEQ_MODE_GRID)->n, 3);
    EXPECT_EQ(seq_pline_steps(g_e, 2, 4), 32);
    EXPECT_EQ(g_e->song_notes, 6);
    /* Independent */
    seq_grid_remove(g_e, 2, 4, 0, 60);
    EXPECT_EQ(seq_notes_get(g_e, 0, 1, SEQ_MODE_GRID)->n, 3);
    /* Within a pattern, and from an empty slot */
    EXPECT_EQ(seq_pline_copy(g_e, 0, 1, 0, 0), 1);
    EXPECT_EQ(seq_notes_get(g_e, 0, 0, SEQ_MODE_GRID)->n, 3);
    EXPECT_EQ(seq_pline_copy(g_e, 9, 0, 0, 0), 1);
    EXPECT_EQ(seq_notes_get(g_e, 0, 0, SEQ_MODE_GRID)->n, 0);
    EXPECT_EQ(seq_pline_steps(g_e, 0, 0), 16);
    EXPECT_EQ(seq_pline_copy(g_e, 0, 99, 0, 0), 0);
    EXPECT_EQ(g_e->song_notes, 5);
}

static void test_program_change(void)
{
    printf("program change\n");
    fresh();
    uint8_t n = 60;
    seq_grid_place(g_e, 0, 0, 0, 1, &n, NULL, 1);
    seq_grid_place(g_e, 1, 0, 0, 1, &n, NULL, 1);
    g_e->lines[0].channel = 2;
    EXPECT_EQ(seq_pline_program(g_e, 0, 0), -1);
    EXPECT_EQ(seq_pline_set_program(g_e, 0, 0, 81), 1);
    EXPECT_EQ(seq_pline_set_bank(g_e, 0, 0, 1, -1), 1);
    EXPECT_EQ(seq_pline_set_program(g_e, 1, 0, 5), 1);
    EXPECT_EQ(seq_pline_set_program(g_e, 0, 0, 128), 0);
    EXPECT_EQ(seq_pline_set_bank(g_e, 0, 0, 1, 200), 0);
    EXPECT_EQ(seq_pline_program(g_e, 0, 0), 81);
    EXPECT_EQ(seq_pline_bank_msb(g_e, 0, 0), 1);
    EXPECT_EQ(seq_pline_bank_lsb(g_e, 0, 0), -1);

    /* Start: bank MSB, then the program, before the first note */
    seq_engine_start(g_e);
    run_ticks(1);
    EXPECT_EQ(g_msg_n, 2);
    EXPECT_EQ(g_msg[0].status, 0xB2); EXPECT_EQ(g_msg[0].d1, 0); EXPECT_EQ(g_msg[0].d2, 1);
    EXPECT_EQ(g_msg[1].status, 0xC2); EXPECT_EQ(g_msg[1].d1, 81);
    EXPECT_EQ(g_msg[1].at_event, 0);           /* before the note on */
    EXPECT_EQ(count_on(60), 1);

    /* Looping the same pattern sends nothing again */
    run_ticks(384 * 2);
    EXPECT_EQ(g_msg_n, 2);

    /* A switch to another pattern sends its programs at its first tick */
    seq_select_pattern(g_e, 1);
    run_ticks(384);
    EXPECT_EQ(g_msg_n, 3);
    EXPECT_EQ(g_msg[2].status, 0xC2); EXPECT_EQ(g_msg[2].d1, 5);
    EXPECT_EQ(g_msg[2].tick % 384, 0);

    /* Lines without a program send nothing; Continue sends them again */
    seq_engine_stop(g_e);
    clear_events();
    seq_engine_continue(g_e);
    run_ticks(1);
    EXPECT_EQ(g_msg_n, 1);
    EXPECT_EQ(g_msg[0].d1, 5);

    /* Chain repeats of one pattern: once */
    fresh();
    seq_pline_set_program(g_e, 0, 0, 10);
    seq_pline_set_program(g_e, 1, 0, 11);
    seq_chain_entry_t chain[2] = { { 0, 3 }, { 1, 1 } };
    seq_set_chain(g_e, chain, 2);
    seq_set_chain_on(g_e, true);
    seq_engine_start(g_e);
    run_ticks(384 * 4 + 1);
    EXPECT_EQ(g_msg_n, 3);   /* 10 at the start, 11 at bar 4, 10 again at bar 5 */
    EXPECT_EQ(g_msg[0].d1, 10);
    EXPECT_EQ(g_msg[1].d1, 11);
    EXPECT_EQ(g_msg[2].d1, 10);

    /* Sent at once on request (an edit to the playing pattern) */
    clear_events();
    seq_send_programs(g_e, 0);
    EXPECT_EQ(g_msg_n, 1);

    /* Copied with the line */
    fresh();
    seq_pline_set_program(g_e, 0, 0, 33);
    seq_pline_set_bank(g_e, 0, 0, 2, 3);
    seq_pline_copy(g_e, 0, 0, 4, 1);
    EXPECT_EQ(seq_pline_program(g_e, 4, 1), 33);
    EXPECT_EQ(seq_pline_bank_lsb(g_e, 4, 1), 3);
}

static void test_lines_add_remove(void)
{
    printf("lines add / remove\n");
    fresh();
    EXPECT_EQ(seq_line_add(g_e), 8);
    EXPECT_EQ(g_e->lines[8].channel, 8);
    g_e->lines[0].channel = 9;                  /* 0 now free */
    EXPECT_EQ(seq_line_add(g_e), 9);
    EXPECT_EQ(g_e->lines[9].channel, 0);
    while (g_e->line_count < SEQ_MAX_LINES) seq_line_add(g_e);
    EXPECT_EQ(seq_line_add(g_e), -1);

    fresh();
    uint8_t n1 = 36, n2 = 38;
    seq_grid_place(g_e, 0, 1, 0, 1, &n1, NULL, 1);
    seq_grid_place(g_e, 0, 2, 0, 1, &n2, NULL, 1);
    seq_pline_set_steps(g_e, 0, 2, 8);
    strcpy(g_e->lines[2].name, "Snare");
    EXPECT_EQ(seq_line_remove(g_e, 1), 1);
    EXPECT_EQ(g_e->line_count, 7);
    EXPECT_EQ(strcmp(g_e->lines[1].name, "Snare"), 0);
    EXPECT_EQ(seq_notes_get(g_e, 0, 1, SEQ_MODE_GRID)->v[0].note, 38);
    EXPECT_EQ(seq_pline_steps(g_e, 0, 1), 8);
    EXPECT_EQ(g_e->song_notes, 1);
    /* The freed slot comes back empty */
    EXPECT_EQ(seq_line_add(g_e), 7);
    EXPECT_EQ(seq_notes_get(g_e, 0, 7, SEQ_MODE_GRID)->n, 0);
    EXPECT_EQ(seq_pline_steps(g_e, 0, 7), 16);

    while (g_e->line_count > 1) seq_line_remove(g_e, 0);
    EXPECT_EQ(seq_line_remove(g_e, 0), 0);
}

static void test_limits(void)
{
    printf("limits\n");
    fresh();
    /* 512 per array: 64 steps × 8 notes */
    uint8_t chord[8];
    for (int i = 0; i < 8; i++) chord[i] = (uint8_t)(40 + i);
    for (int s = 0; s < 64; s++) EXPECT_EQ(seq_grid_place(g_e, 0, 0, s, 1, chord, NULL, 8), 8);
    EXPECT_EQ(seq_notes_get(g_e, 0, 0, SEQ_MODE_GRID)->n, SEQ_MAX_NOTES);
    uint8_t extra = 100;
    EXPECT_EQ(seq_grid_place(g_e, 0, 0, 0, 1, &extra, NULL, 1), 0);

    /* 8192 in the song: patterns 0 and 1 full (8 lines × 512) */
    for (int p = 0; p < 2; p++) {
        for (int l = 0; l < 8; l++) {
            if (p == 0 && l == 0) continue;
            for (int s = 0; s < 64; s++) seq_grid_place(g_e, p, l, s, 1, chord, NULL, 8);
        }
    }
    EXPECT_EQ(g_e->song_notes, SEQ_MAX_SONG_NOTES);
    EXPECT_EQ(seq_rt_add(g_e, 2, 0, 0, 1, 60, 100), 0);
    /* A copy that would go over is refused and leaves the destination alone */
    EXPECT_EQ(seq_pattern_copy(g_e, 0, 2), 0);
    EXPECT_EQ(g_e->patterns[2] == NULL || seq_notes_get(g_e, 2, 0, SEQ_MODE_GRID)->n == 0, 1);
    /* ... one replacing as many notes is fine */
    EXPECT_EQ(seq_pattern_copy(g_e, 0, 1), 1);
    EXPECT_EQ(g_e->song_notes, SEQ_MAX_SONG_NOTES);
    seq_pattern_clear(g_e, 1);
    EXPECT_EQ(g_e->song_notes, SEQ_MAX_SONG_NOTES / 2);
}

static void test_chord_input(void)
{
    printf("chord from the input\n");
    fresh();
    seq_engine_input(g_e, 0x90, 64, 80);
    seq_engine_input(g_e, 0x90, 60, 90);
    seq_engine_input(g_e, 0x91, 67, 70);       /* channel does not matter */
    EXPECT_EQ(g_e->chord_n, 3);
    EXPECT_EQ(g_e->chord[0], 60);
    EXPECT_EQ(g_e->chord_vel[0], 90);
    EXPECT_EQ(g_e->chord[2], 67);
    seq_engine_input(g_e, 0x80, 60, 0);
    seq_engine_input(g_e, 0x90, 64, 0);        /* velocity 0 is a note off */
    EXPECT_EQ(g_e->chord_n, 3);                /* still held: 67 */
    seq_engine_input(g_e, 0x90, 72, 100);      /* added while 67 is held */
    EXPECT_EQ(g_e->chord_n, 4);
    seq_engine_input(g_e, 0x80, 67, 0);
    seq_engine_input(g_e, 0x80, 72, 0);
    EXPECT_EQ(g_e->chord_n, 4);                /* kept after letting go */
    seq_engine_input(g_e, 0x90, 50, 100);      /* everything was let go: new chord */
    EXPECT_EQ(g_e->chord_n, 1);
    EXPECT_EQ(g_e->chord[0], 50);
    seq_engine_input(g_e, 0x80, 50, 0);
    /* At most 8 notes */
    for (int i = 0; i < 10; i++) seq_engine_input(g_e, 0x90, (uint8_t)(30 + i), 100);
    EXPECT_EQ(g_e->chord_n, SEQ_MAX_CHORD);
}

static void test_edit_while_playing(void)
{
    printf("editing while playing\n");
    fresh();
    uint8_t n = 60;
    seq_grid_place(g_e, 0, 0, 0, 8, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(10);
    EXPECT_EQ(count_on(60), 1);
    /* Removing a sounding note stops it now */
    seq_grid_remove(g_e, 0, 0, 0, -1);
    EXPECT_EQ(g_ev[g_n - 1].on, 0);
    EXPECT_EQ(g_ev[g_n - 1].note, 60);

    /* Shortening the pattern under the play position wraps at the next tick */
    fresh();
    seq_pline_set_steps(g_e, 0, 0, 32);
    seq_grid_place(g_e, 0, 0, 0, 1, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(500);
    seq_pline_set_steps(g_e, 0, 0, 16);
    clear_events();
    run_ticks(1);
    EXPECT_EQ(g_e->pattern_tick, 1);
    EXPECT_EQ(count_on(60), 1);
}

static void test_reset(void)
{
    printf("reset\n");
    fresh();
    uint8_t n = 60;
    seq_grid_place(g_e, 4, 0, 0, 8, &n, NULL, 1);
    seq_line_add(g_e);
    seq_engine_reset(g_e, 3);
    EXPECT_EQ(g_e->line_count, 3);
    EXPECT_EQ(g_e->song_notes, 0);
    EXPECT_EQ(g_e->patterns[4] == NULL, 1);
    seq_engine_reset(g_e, 99);
    EXPECT_EQ(g_e->line_count, SEQ_MAX_LINES);
}

/* --- Files --- */

static uint8_t g_file[200000];
static int g_file_n;

static void dump_to_buffer(const seq_engine_t *e, uint8_t flags)
{
    static uint8_t piece[SEQ_DUMP_MAX_CHUNK];
    seq_dump_t d;
    seq_dump_begin(&d, flags);
    g_file_n = 0;
    int n;
    while ((n = seq_dump_next(e, &d, piece, sizeof(piece))) > 0) {
        memcpy(g_file + g_file_n, piece, (size_t)n);
        g_file_n += n;
    }
}

/* Feeds g_file[0..len) in pieces of `step` bytes; returns the error */
static int load_buffer(int len, int step, seq_engine_t **song, uint8_t *flags)
{
    seq_loader_t *ld = seq_load_begin();
    int err = SEQ_LOAD_OK;
    for (int i = 0; i < len && err == SEQ_LOAD_OK; i += step) {
        int n = len - i < step ? len - i : step;
        err = seq_load_feed(ld, g_file + i, n);
    }
    if (err == SEQ_LOAD_OK) err = seq_load_finish(ld, song, flags);
    else *song = NULL;
    seq_load_free(ld);
    return err;
}

static void expect_same_song(const seq_engine_t *a, const seq_engine_t *b)
{
    EXPECT_EQ(a->line_count, b->line_count);
    EXPECT_EQ(strcmp(a->name, b->name), 0);
    EXPECT_EQ(a->bpm_x100, b->bpm_x100);
    EXPECT_EQ(a->chain_len, b->chain_len);
    EXPECT_EQ(a->chain_on, b->chain_on);
    EXPECT_EQ(a->play_pattern, b->play_pattern);
    EXPECT_EQ(a->song_notes, b->song_notes);
    for (int i = 0; i < a->chain_len; i++) {
        EXPECT_EQ(a->chain[i].pattern, b->chain[i].pattern);
        EXPECT_EQ(a->chain[i].repeat, b->chain[i].repeat);
    }
    for (int l = 0; l < a->line_count; l++) {
        const seq_line_t *x = &a->lines[l], *y = &b->lines[l];
        EXPECT_EQ(x->mode, y->mode);
        EXPECT_EQ(x->channel, y->channel);
        EXPECT_EQ(x->mute, y->mute);
        EXPECT_EQ(x->velocity, y->velocity);
        EXPECT_EQ(x->gate, y->gate);
        EXPECT_EQ(x->quantize, y->quantize);
        EXPECT_EQ(x->rec_mode, y->rec_mode);
        EXPECT_EQ(strcmp(x->name, y->name), 0);
    }
    for (int p = 0; p < SEQ_MAX_PATTERNS; p++) {
        EXPECT_EQ(a->patterns[p] == NULL, b->patterns[p] == NULL);
        if (!a->patterns[p]) continue;
        EXPECT_EQ(a->patterns[p]->length, b->patterns[p]->length);
        for (int l = 0; l < a->line_count; l++) {
            const seq_pattern_line_t *x = &a->patterns[p]->lines[l], *y = &b->patterns[p]->lines[l];
            EXPECT_EQ(x->steps, y->steps);
            EXPECT_EQ(x->rate, y->rate);
            EXPECT_EQ(x->bars, y->bars);
            EXPECT_EQ(x->program, y->program);
            EXPECT_EQ(x->bank_msb, y->bank_msb);
            EXPECT_EQ(x->bank_lsb, y->bank_lsb);
            EXPECT_EQ(x->grid.n, y->grid.n);
            EXPECT_EQ(x->rt.n, y->rt.n);
            if (x->grid.n) EXPECT_EQ(memcmp(x->grid.v, y->grid.v, x->grid.n * sizeof(seq_note_t)), 0);
            if (x->rt.n) EXPECT_EQ(memcmp(x->rt.v, y->rt.v, x->rt.n * sizeof(seq_note_t)), 0);
        }
    }
}

static void build_song(void)
{
    fresh();
    seq_line_add(g_e);
    strcpy(g_e->name, "groove");
    g_e->bpm_x100 = 12350;
    strcpy(g_e->lines[0].name, "Kick");
    g_e->lines[0].channel = 9;
    g_e->lines[1].gate = 80;
    g_e->lines[1].velocity = 90;
    g_e->lines[2].mode = SEQ_MODE_REALTIME;
    g_e->lines[2].quantize = SEQ_RATE_16 + 1;
    g_e->lines[2].rec_mode = 1;
    g_e->lines[3].mute = 1;
    uint8_t kick = 36, chord[3] = { 60, 64, 67 }, vel[3] = { 90, 80, 70 };
    for (int s = 0; s < 16; s += 4) seq_grid_place(g_e, 0, 0, s, 1, &kick, NULL, 1);
    seq_grid_place(g_e, 0, 1, 0, 6, chord, vel, 3);
    seq_pline_set_steps(g_e, 0, 1, 32);
    seq_pline_set_rate(g_e, 0, 1, SEQ_RATE_8T);
    seq_pline_set_bars(g_e, 0, 2, 2);
    seq_pline_set_program(g_e, 0, 3, 81);
    seq_pline_set_bank(g_e, 0, 3, 0, 5);
    seq_rt_add(g_e, 0, 2, 13, 50, 72, 99);
    seq_rt_add(g_e, 0, 2, 700, 100, 74, 40);
    seq_pattern_copy(g_e, 0, 5);
    seq_pattern_set_length(g_e, 5, 4);
    seq_pattern_get(g_e, 9, true);   /* used but empty */
    seq_chain_entry_t chain[2] = { { 0, 2 }, { 5, 1 } };
    seq_set_chain(g_e, chain, 2);
    seq_set_chain_on(g_e, true);
    seq_select_pattern(g_e, 5);       /* turns the chain off again */
    seq_set_chain_on(g_e, true);
    seq_select_pattern(g_e, -1);
    g_e->play_pattern = 5;
}

static void test_file_round_trip(void)
{
    printf("file round trip\n");
    build_song();
    dump_to_buffer(g_e, SEQ_FILE_DIRTY);
    EXPECT_EQ(memcmp(g_file, "MSEQ", 4), 0);
    int steps[] = { 1, 7, 13, 4096, 1 << 20 };
    for (int i = 0; i < 5; i++) {
        seq_engine_t *song;
        uint8_t flags = 0;
        EXPECT_EQ(load_buffer(g_file_n, steps[i], &song, &flags), SEQ_LOAD_OK);
        EXPECT_EQ(flags & SEQ_FILE_DIRTY, SEQ_FILE_DIRTY);
        EXPECT_EQ(flags & SEQ_FILE_CHAIN_ON, SEQ_FILE_CHAIN_ON);
        expect_same_song(g_e, song);
        seq_engine_free(song);
    }

    /* An empty song: just the head, the lines and the CRC */
    fresh();
    dump_to_buffer(g_e, 0);
    seq_engine_t *song;
    EXPECT_EQ(load_buffer(g_file_n, 5, &song, NULL), SEQ_LOAD_OK);
    expect_same_song(g_e, song);
    seq_engine_free(song);
}

static void test_file_errors(void)
{
    printf("file errors\n");
    build_song();
    dump_to_buffer(g_e, 0);
    seq_engine_t *song;

    /* Cut short anywhere */
    for (int len = 0; len < g_file_n; len += 37) {
        EXPECT_EQ(load_buffer(len, 64, &song, NULL) != SEQ_LOAD_OK, 1);
        EXPECT_EQ(song == NULL, 1);
    }
    EXPECT_EQ(load_buffer(g_file_n - 1, 64, &song, NULL), SEQ_LOAD_TRUNCATED);

    /* Any flipped byte is caught */
    for (int i = 0; i < g_file_n; i += 11) {
        g_file[i] ^= 0x5A;
        EXPECT_EQ(load_buffer(g_file_n, 64, &song, NULL) != SEQ_LOAD_OK, 1);
        g_file[i] ^= 0x5A;
    }
    g_file[0] = 'X';
    EXPECT_EQ(load_buffer(g_file_n, 64, &song, NULL), SEQ_LOAD_BAD_MAGIC);
    g_file[0] = 'M';
    g_file[4] = 2;
    EXPECT_EQ(load_buffer(g_file_n, 64, &song, NULL), SEQ_LOAD_BAD_VERSION);
    g_file[4] = 1;
    EXPECT_EQ(load_buffer(g_file_n, 64, &song, NULL), SEQ_LOAD_OK);
    seq_engine_free(song);
    EXPECT_EQ(strcmp(seq_load_error_name(SEQ_LOAD_BAD_CRC), "checksum mismatch"), 0);
}

/* A chunk this version does not know is skipped (and counted in the CRC) */
static void test_file_unknown_chunk(void)
{
    printf("unknown chunks are skipped\n");
    build_song();
    dump_to_buffer(g_e, 0);
    /* Drop the CRC, append an unknown chunk, then a new CRC */
    int n = g_file_n - 12;
    uint8_t extra[8 + 5] = { 'X', 'T', 'R', 'A', 5, 0, 0, 0, 1, 2, 3, 4, 5 };
    memcpy(g_file + n, extra, sizeof(extra));
    n += sizeof(extra);
    /* CRC32 the same way the engine does */
    uint32_t crc = ~0u;
    for (int i = 0; i < n; i++) {
        crc ^= g_file[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    memcpy(g_file + n, "CRC ", 4);
    g_file[n + 4] = 4; g_file[n + 5] = 0; g_file[n + 6] = 0; g_file[n + 7] = 0;
    for (int k = 0; k < 4; k++) g_file[n + 8 + k] = (uint8_t)(crc >> (8 * k));
    n += 12;
    seq_engine_t *song;
    EXPECT_EQ(load_buffer(n, 9, &song, NULL), SEQ_LOAD_OK);
    expect_same_song(g_e, song);
    seq_engine_free(song);
}

/* A PLIN chunk as written before programs (6 bytes) reads with none */
static void test_file_old_plin(void)
{
    printf("file with the old PLIN\n");
    fresh();
    seq_pline_set_steps(g_e, 0, 0, 32);
    dump_to_buffer(g_e, 0);
    /* Find the PLIN chunk, shrink it to 6 bytes, fix the CRC */
    int at = -1;
    for (int i = 0; i + 4 <= g_file_n; i++) {
        if (memcmp(g_file + i, "PLIN", 4) == 0) { at = i; break; }
    }
    EXPECT_EQ(at >= 0, 1);
    EXPECT_EQ(g_file[at + 4], 9);
    g_file[at + 4] = 6;
    memmove(g_file + at + 8 + 6, g_file + at + 8 + 9, (size_t)(g_file_n - (at + 8 + 9)));
    int n = g_file_n - 3 - 12;
    uint32_t crc = ~0u;
    for (int i = 0; i < n; i++) {
        crc ^= g_file[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    memcpy(g_file + n, "CRC ", 4);
    g_file[n + 4] = 4; g_file[n + 5] = 0; g_file[n + 6] = 0; g_file[n + 7] = 0;
    for (int k = 0; k < 4; k++) g_file[n + 8 + k] = (uint8_t)(crc >> (8 * k));
    seq_engine_t *song;
    EXPECT_EQ(load_buffer(n + 12, 5, &song, NULL), SEQ_LOAD_OK);
    EXPECT_EQ(seq_pline_steps(song, 0, 0), 32);
    EXPECT_EQ(seq_pline_program(song, 0, 0), -1);
    EXPECT_EQ(seq_pline_bank_msb(song, 0, 0), -1);
    seq_engine_free(song);
}

static void test_file_full_song(void)
{
    printf("file of a full song\n");
    fresh();
    uint8_t chord[8];
    for (int i = 0; i < 8; i++) chord[i] = (uint8_t)(40 + i);
    for (int p = 0; p < 2; p++)
        for (int l = 0; l < 8; l++)
            for (int s = 0; s < 64; s++) seq_grid_place(g_e, p, l, s, 1, chord, NULL, 8);
    EXPECT_EQ(g_e->song_notes, SEQ_MAX_SONG_NOTES);
    dump_to_buffer(g_e, 0);
    seq_engine_t *song;
    EXPECT_EQ(load_buffer(g_file_n, 1000, &song, NULL), SEQ_LOAD_OK);
    expect_same_song(g_e, song);
    seq_engine_free(song);
}

static void test_clone_and_adopt(void)
{
    printf("clone, adopt\n");
    build_song();
    seq_engine_t *copy = seq_song_clone(g_e);
    expect_same_song(g_e, copy);
    /* Independent of the original */
    seq_grid_remove(g_e, 0, 0, 0, -1);
    EXPECT_EQ(copy->patterns[0]->lines[0].grid.n, 4);
    EXPECT_EQ(g_e->patterns[0]->lines[0].grid.n, 3);

    /* Adopted while playing: what sounds goes off, play goes on from the top */
    fresh();
    uint8_t n = 50;
    seq_grid_place(g_e, 0, 0, 0, 16, &n, NULL, 1);
    seq_engine_start(g_e);
    run_ticks(10);
    EXPECT_EQ(count_on(50), 1);
    int before = g_n;
    seq_engine_adopt(g_e, copy);
    EXPECT_EQ(g_n, before + 1);
    EXPECT_EQ(g_ev[g_n - 1].on, 0);
    EXPECT_EQ(g_e->running, 1);
    EXPECT_EQ(g_e->play_pattern, 5);
    EXPECT_EQ(g_e->pattern_tick, 0);
    EXPECT_EQ(g_e->line_count, 9);
    EXPECT_EQ(g_e->patterns[5]->length, 4);
}

int main(void)
{
    test_defaults();
    test_grid_playback();
    test_gate_and_retrigger();
    test_chords_and_overlap();
    test_polymeter();
    test_realtime_playback();
    test_record_basic();
    test_record_overdub_and_hold();
    test_record_quantize();
    test_record_stop_disarm();
    test_record_pattern_switch();
    test_record_replace();
    test_events();
    test_clock_interpolation();
    test_continue_and_spp();
    test_mute();
    test_pattern_switch();
    test_chain();
    test_pattern_copy_clear();
    test_pline_copy();
    test_program_change();
    test_lines_add_remove();
    test_limits();
    test_chord_input();
    test_edit_while_playing();
    test_reset();
    test_file_round_trip();
    test_file_errors();
    test_file_unknown_chunk();
    test_file_old_plin();
    test_file_full_song();
    test_clone_and_adopt();
    seq_engine_free(g_e);
    printf("all sequencer engine tests passed\n");
    return 0;
}
