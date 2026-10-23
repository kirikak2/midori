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
    uint8_t screen;      /* For screen change events: the new screen */
    uint8_t screen_from;   /* ...the screen left */
    uint8_t screen_source; /* ...0 touch / 1 script / 2 system */
    int8_t  screen_layout; /* ...layout slot when screen is the layout screen, else -1 */
    uint8_t hit_ball;    /* For tombola hit events */
    uint8_t hit_side;    /* For tombola hit events */
    uint8_t hit_note;    /* For tombola hit events */
    uint8_t hit_velocity;/* For tombola hit events */
    uint8_t knob_bank;   /* For knob events (0-based) */
    uint8_t knob_index;  /* For knob change events (0-based) */
    bool knob_final;     /* For knob change events */
    float knob_value;    /* For knob change events */
    uint8_t xypad_slot;    /* For XYPad touch events (0-based) */
    uint8_t xypad_phase;   /* 0=down 1=move 2=up */
    uint8_t xypad_channel;
    uint8_t xypad_note;
    float xypad_bend;      /* Meaningful when the slot's x_mode is :note */
    float xypad_x;         /* Meaningful when the slot's x_mode is :cc */
    float xypad_y;
    uint8_t widget_layout; /* For layout widget events: layout slot */
    uint8_t widget_id;     /* ...widget index within the layout */
    uint8_t widget_kind;   /* ...0 pad / 1 knob */
    uint8_t widget_phase;  /* ...0 press / 1 release / 2 change */
    bool widget_state;     /* ...pad state */
    bool widget_final;     /* ...knob: last change of a drag */
    float widget_value;    /* ...knob value */
    uint8_t key_note;      /* For key events */
    uint8_t key_velocity;  /* ...0 on release */
    uint8_t key_channel;
    bool key_on;
    uint8_t mididev_kind;  /* For MIDI Devices events: 0 select_in / 1 select_out /
                              2 patch / 3 unpatch / 4 connect / 5 disconnect */
    uint8_t mididev_from;  /* ...transport bit (the device for select / connect) */
    uint8_t mididev_to;    /* ...transport bit (patches) */
    int8_t  mididev_channel; /* ...patches: 0..15, -1 all, -2 the clock alone,
                                -3 several (mididev_sel) */
    uint32_t mididev_sel;  /* ...patches: channel bits 0..15, 0x10000 the clock */
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
#define PICORUBY_UI_EVENT_XYPAD_TOUCH   9
#define PICORUBY_UI_EVENT_WIDGET       10
#define PICORUBY_UI_EVENT_KEY          11
#define PICORUBY_UI_EVENT_MIDI_DEVICES 12

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

/* Screen switching. Requests are applied by the UI task; the request
 * functions return false (and queue nothing) for a screen that does not
 * exist or is hidden from the menu. arg is screen-specific (the Knobs bank,
 * the layout slot), -1 for none. */
void picoruby_ui_set_screen(int index);
int  picoruby_ui_current_screen(void);
bool picoruby_ui_request_screen(int index, int arg);
bool picoruby_ui_request_back(void);
bool picoruby_ui_request_step(int dir);
int  picoruby_ui_requested_screen(int *arg);
void picoruby_ui_menu_set_hidden(uint32_t hidden);
uint32_t picoruby_ui_menu_get_hidden(void);

/* Screen facts for the layout DSL: width, height, content x / y / w / h,
 * large-layout flag (0/1), default grid gap. */
void picoruby_ui_display(int out[8]);

/* Layouts (docs/UI_LAYOUT_DSL.md). Defined by begin, add_*, commit; the add
 * functions return the widget id or -1. Rectangles are screen pixels. */
bool  picoruby_ui_layout_begin(int slot, const char *title);
int   picoruby_ui_layout_add_pad(int x, int y, int w, int h, const char *label,
                                 int color, int type);
int   picoruby_ui_layout_add_knob(int x, int y, int w, int h, const char *label,
                                  int color, float min, float max, float step,
                                  float value, int origin, float sensitivity,
                                  bool notify);
int   picoruby_ui_layout_add_view(int kind, int x, int y, int w, int h);
bool  picoruby_ui_layout_commit(void);
void  picoruby_ui_layout_delete(int slot);
bool  picoruby_ui_layout_pad_state(int slot, int id);
void  picoruby_ui_layout_set_label(int slot, int id, const char *label);
void  picoruby_ui_layout_set_color(int slot, int id, int color);
float picoruby_ui_layout_knob_value(int slot, int id);
bool  picoruby_ui_layout_knob_set_value(int slot, int id, float value);
bool  picoruby_ui_layout_knob_reset(int slot, int id);

/* A script ended (called from picoruby_esp32_midi_cleanup). Any task. */
void picoruby_ui_cleanup(void);

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

/* XYPad.
 * Up to five simultaneous touches, each an independent "slot" with its own
 * X meaning (scale + glide, or a raw CC), Y range, channel and hold flag.
 * Parameters are string-keyed like Tombola's so this bridge stays fixed in
 * size; see ui_common.h for the accepted names. index is 0-based. */
void    picoruby_ui_xypad_reset(void);
void    picoruby_ui_xypad_set_max_touches(int n);
int     picoruby_ui_xypad_get_max_touches(void);
bool    picoruby_ui_xypad_set_f(int index, const char *name, float value);
bool    picoruby_ui_xypad_set_i(int index, const char *name, int value);
float   picoruby_ui_xypad_get_f(int index, const char *name);
int     picoruby_ui_xypad_get_i(int index, const char *name);
void    picoruby_ui_xypad_set_scale(int index, const uint8_t *notes, int len);
/* Writes up to max_len notes into out, returns how many. */
int     picoruby_ui_xypad_get_scale(int index, uint8_t *out, int max_len);

/* Keys: the on-screen keyboard, also a picoruby-midi transport
 * (docs/KEYS.md). Parameters are string-keyed like Tombola's; see
 * ui_common.h for the accepted names. Colours are RGB565, -1 to clear. */
void picoruby_ui_keys_reset(void);
bool picoruby_ui_keys_set_i(const char *name, int value);
int  picoruby_ui_keys_get_i(const char *name);
void picoruby_ui_keys_set_key_color(int note, int color);
int  picoruby_ui_keys_get_key_color(int note);
void picoruby_ui_keys_clear_key_colors(void);
void picoruby_ui_keys_set_channel_color(int channel, int color);
void picoruby_ui_keys_receive(int cin, int b1, int b2, int b3);
void picoruby_ui_keys_clear_lights(void);
bool picoruby_ui_keys_is_lit(int note);
bool picoruby_ui_keys_is_pressed(int note);

/* Tempo (docs/TEMPO.md): a MIDI device that sends clock at the UI BPM.
 * tempo_bit is its transport bit (0 when the registry was full). */
int  picoruby_ui_tempo_bit(void);
void picoruby_ui_tempo_set_running(bool running);
bool picoruby_ui_tempo_running(void);
void picoruby_ui_tempo_tap(void);
bool picoruby_ui_tempo_sync(void);
void picoruby_ui_tempo_set_sync(bool on);

/* MIDI Devices (docs/UI_MIDI_DEVICES.md): the device list, the program's IN /
 * OUT and the saved patches. Devices are picoruby-midi transport bits; key
 * names a built-in ("" for a script's device), bit 0 registers a standby one.
 * Indices are positions in the list, -1 for none. */
int  picoruby_ui_mididev_register(const char *key, const char *name, int bit,
                                  int color, bool builtin);
bool picoruby_ui_mididev_unregister(int bit);
bool picoruby_ui_mididev_activate(const char *key, int bit);
int  picoruby_ui_mididev_count(void);
/* Copies entry `index`: its bit, flags (1 built-in, 2 standby), key and name */
bool picoruby_ui_mididev_get(int index, int *bit, int *flags, char *key, int key_len,
                             char *name, int name_len);
int  picoruby_ui_mididev_selected(bool out);
void picoruby_ui_mididev_select(bool out, int index, bool save);
int  picoruby_ui_mididev_restore(void);
bool picoruby_ui_mididev_save(void);
bool picoruby_ui_mididev_take_wanted(char *key, int len);
int  picoruby_ui_mididev_connected_mask(void);
int  picoruby_ui_mididev_serial(void);

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_UI_H_ */
