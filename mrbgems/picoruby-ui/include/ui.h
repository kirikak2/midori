/*
 * PicoRuby UI - Platform-specific UI event API
 *
 * This header provides the interface for the mrubyc bindings.
 * The actual implementation is platform-specific.
 */

#ifndef PICORUBY_UI_H_
#define PICORUBY_UI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Simplified UI Event structure for mrubyc binding */
typedef struct {
    uint8_t type;        /* Event type (see UI_EVENT_* constants) */
    float bpm;           /* For BPM change events */
    uint8_t pad_index;   /* For pad events */
    bool pad_state;      /* For pad events */
    bool sync_mode;      /* For sync mode events */
    uint8_t screen;      /* For screen change events */
    uint8_t hit_ball;    /* For tombola hit events */
    uint8_t hit_side;    /* For tombola hit events */
    uint8_t hit_note;    /* For tombola hit events */
    uint8_t hit_velocity;/* For tombola hit events */
    uint8_t knob_bank;   /* For knob events (0-based) */
    uint8_t knob_index;  /* For knob change events (0-based) */
    bool knob_final;     /* For knob change events */
    float knob_value;    /* For knob change events */
} picoruby_ui_event_t;

/* Event type constants */
#define PICORUBY_UI_EVENT_NONE          0
#define PICORUBY_UI_EVENT_BPM_CHANGE    1
#define PICORUBY_UI_EVENT_PAD_PRESS     2
#define PICORUBY_UI_EVENT_PAD_RELEASE   3
#define PICORUBY_UI_EVENT_SYNC_MODE     4
#define PICORUBY_UI_EVENT_SCREEN_CHANGE 5
#define PICORUBY_UI_EVENT_TOMBOLA_HIT   6
#define PICORUBY_UI_EVENT_KNOB_CHANGE   7
#define PICORUBY_UI_EVENT_KNOB_BANK     8

/* Platform-specific functions (implemented in ports/) */
bool picoruby_ui_pop_event(picoruby_ui_event_t *event);
int picoruby_ui_events_available(void);
float picoruby_ui_get_bpm(void);
void picoruby_ui_set_bpm(float bpm);

/* Log output to Screen Log */
void picoruby_ui_add_log(const char *text);

/* Pad functions */
void picoruby_ui_pad_set(int index, const char *label, int color, int type);
void picoruby_ui_pad_clear(int index);
void picoruby_ui_pad_clear_all(void);
bool picoruby_ui_pad_get_state(int index);
void picoruby_ui_pad_set_label(int index, const char *label);
void picoruby_ui_pad_set_color(int index, int color);

/* Knobs.
 * Continuous values the user turns with a finger. Nothing here sends MIDI:
 * the Ruby block attached to the knob does that, as it does for pads.
 * bank and index are 0-based. */
void  picoruby_ui_knob_set(int bank, int index, const char *label, int color,
                           float min, float max, float step, float value,
                           int origin, float sensitivity, bool notify);
void  picoruby_ui_knob_clear(int bank, int index);
void  picoruby_ui_knob_clear_all(void);
float picoruby_ui_knob_get_value(int bank, int index);
bool  picoruby_ui_knob_set_value(int bank, int index, float value);
bool  picoruby_ui_knob_reset(int bank, int index);
void  picoruby_ui_knob_set_label(int bank, int index, const char *label);
void  picoruby_ui_knob_set_color(int bank, int index, int color);
bool  picoruby_ui_knob_assigned(int bank, int index);
int   picoruby_ui_knob_count(void);
int   picoruby_ui_knob_banks(void);
int   picoruby_ui_knob_get_bank(void);
void  picoruby_ui_knob_set_bank(int bank);

/* Screen switching (diagnostic + control) */
void picoruby_ui_set_screen(int index);
int  picoruby_ui_current_screen(void);

/* Tombola sequencer.
 * Parameters are string-keyed so this bridge stays fixed in size as the
 * sequencer grows knobs; see ui_common.h for the accepted names. */
void  picoruby_ui_tombola_reset(void);
void  picoruby_ui_tombola_start(void);
void  picoruby_ui_tombola_stop(void);
bool  picoruby_ui_tombola_running(void);
bool  picoruby_ui_tombola_set_f(const char *name, float value);
bool  picoruby_ui_tombola_set_i(const char *name, int value);
float picoruby_ui_tombola_get_f(const char *name);
int   picoruby_ui_tombola_get_i(const char *name);
void  picoruby_ui_tombola_set_scale(const uint8_t *notes, int len);
int   picoruby_ui_tombola_add_ball(int note, int channel, int color, float velocity_scale);
bool  picoruby_ui_tombola_remove_ball(int index);
void  picoruby_ui_tombola_clear_balls(void);
int   picoruby_ui_tombola_ball_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_UI_H_ */
