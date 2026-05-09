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
} picoruby_ui_event_t;

/* Event type constants */
#define PICORUBY_UI_EVENT_NONE          0
#define PICORUBY_UI_EVENT_BPM_CHANGE    1
#define PICORUBY_UI_EVENT_PAD_PRESS     2
#define PICORUBY_UI_EVENT_PAD_RELEASE   3
#define PICORUBY_UI_EVENT_SYNC_MODE     4
#define PICORUBY_UI_EVENT_SCREEN_CHANGE 5

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

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_UI_H_ */
