/*
 * PicoRuby UI - ESP32/M5Stack port
 */

#include <stddef.h>
#include <stdio.h>
#include "../../include/ui.h"
#include "ui_common.h"

bool picoruby_ui_pop_event(picoruby_ui_event_t *event)
{
    if (event == NULL) return false;

    ui_event_t ui_evt;
    if (!ui_event_pop(&ui_evt)) {
        return false;
    }

    /* Initialize all fields */
    event->type = PICORUBY_UI_EVENT_NONE;
    event->bpm = 0.0f;
    event->pad_index = 0;
    event->pad_state = false;
    event->sync_mode = false;
    event->screen = 0;
    event->screen_from = 0;
    event->screen_source = 0;
    event->screen_layout = -1;
    event->hit_ball = 0;
    event->hit_side = 0;
    event->hit_note = 0;
    event->hit_velocity = 0;
    event->knob_bank = 0;
    event->knob_index = 0;
    event->knob_final = false;
    event->knob_value = 0.0f;
    event->xypad_slot = 0;
    event->xypad_phase = 0;
    event->xypad_channel = 0;
    event->xypad_note = 0;
    event->xypad_bend = 0.0f;
    event->xypad_x = 0.0f;
    event->xypad_y = 0.0f;
    event->widget_layout = 0;
    event->widget_id = 0;
    event->widget_kind = 0;
    event->widget_phase = 0;
    event->widget_state = false;
    event->widget_final = false;
    event->widget_value = 0.0f;
    event->key_note = 0;
    event->key_velocity = 0;
    event->key_channel = 0;
    event->key_on = false;
    event->mididev_kind = 0;
    event->mididev_from = 0;
    event->mididev_to = 0;
    event->mididev_channel = -1;
    event->mididev_sel = 0;
    event->seq_kind = 0;
    event->seq_pattern = -1;
    event->seq_from = -1;
    event->seq_line = -1;
    event->seq_step = 0;

    /* Convert event type and data */
    switch (ui_evt.type) {
        case UI_EVENT_BPM_CHANGE:
            event->type = PICORUBY_UI_EVENT_BPM_CHANGE;
            event->bpm = ui_evt.data.bpm;
            break;
        case UI_EVENT_PAD_PRESS:
            event->type = PICORUBY_UI_EVENT_PAD_PRESS;
            event->pad_index = ui_evt.data.pad.index;
            event->pad_state = ui_evt.data.pad.state;
            break;
        case UI_EVENT_PAD_RELEASE:
            event->type = PICORUBY_UI_EVENT_PAD_RELEASE;
            event->pad_index = ui_evt.data.pad.index;
            event->pad_state = ui_evt.data.pad.state;
            break;
        case UI_EVENT_SYNC_MODE:
            event->type = PICORUBY_UI_EVENT_SYNC_MODE;
            event->sync_mode = ui_evt.data.sync_mode;
            break;
        case UI_EVENT_SCREEN_CHANGE:
            event->type = PICORUBY_UI_EVENT_SCREEN_CHANGE;
            event->screen = ui_evt.data.screen.index;
            event->screen_from = ui_evt.data.screen.from;
            event->screen_source = ui_evt.data.screen.source;
            event->screen_layout = ui_evt.data.screen.layout;
            break;
        case UI_EVENT_TOMBOLA_HIT:
            event->type = PICORUBY_UI_EVENT_TOMBOLA_HIT;
            event->hit_ball = ui_evt.data.tombola.ball;
            event->hit_side = ui_evt.data.tombola.side;
            event->hit_note = ui_evt.data.tombola.note;
            event->hit_velocity = ui_evt.data.tombola.velocity;
            break;
        case UI_EVENT_KNOB_CHANGE:
            event->type = PICORUBY_UI_EVENT_KNOB_CHANGE;
            event->knob_bank = ui_evt.data.knob.bank;
            event->knob_index = ui_evt.data.knob.index;
            event->knob_final = ui_evt.data.knob.final;
            event->knob_value = ui_evt.data.knob.value;
            break;
        case UI_EVENT_KNOB_BANK:
            event->type = PICORUBY_UI_EVENT_KNOB_BANK;
            event->knob_bank = ui_evt.data.knob_bank;
            break;
        case UI_EVENT_XYPAD_TOUCH:
            event->type = PICORUBY_UI_EVENT_XYPAD_TOUCH;
            event->xypad_slot = ui_evt.data.xypad.slot;
            event->xypad_phase = ui_evt.data.xypad.phase;
            event->xypad_channel = ui_evt.data.xypad.channel;
            event->xypad_note = ui_evt.data.xypad.note;
            event->xypad_bend = ui_evt.data.xypad.bend_semitones;
            event->xypad_x = ui_evt.data.xypad.x;
            event->xypad_y = ui_evt.data.xypad.y;
            break;
        case UI_EVENT_WIDGET:
            event->type = PICORUBY_UI_EVENT_WIDGET;
            event->widget_layout = ui_evt.data.widget.layout;
            event->widget_id = ui_evt.data.widget.id;
            event->widget_kind = ui_evt.data.widget.kind;
            event->widget_phase = ui_evt.data.widget.phase;
            event->widget_state = ui_evt.data.widget.state;
            event->widget_final = ui_evt.data.widget.final;
            event->widget_value = ui_evt.data.widget.value;
            break;
        case UI_EVENT_KEY:
            event->type = PICORUBY_UI_EVENT_KEY;
            event->key_note = ui_evt.data.key.note;
            event->key_velocity = ui_evt.data.key.velocity;
            event->key_channel = ui_evt.data.key.channel;
            event->key_on = ui_evt.data.key.on;
            break;
        case UI_EVENT_MIDI_DEVICES:
            event->type = PICORUBY_UI_EVENT_MIDI_DEVICES;
            event->mididev_kind = ui_evt.data.mididev.kind;
            event->mididev_from = ui_evt.data.mididev.from;
            event->mididev_to = ui_evt.data.mididev.to;
            event->mididev_channel = ui_evt.data.mididev.channel;
            event->mididev_sel = ui_evt.data.mididev.sel;
            break;
        case UI_EVENT_SEQUENCER:
            event->type = PICORUBY_UI_EVENT_SEQUENCER;
            event->seq_kind = ui_evt.data.seq.kind;
            event->seq_pattern = ui_evt.data.seq.pattern;
            event->seq_from = ui_evt.data.seq.from;
            event->seq_line = ui_evt.data.seq.line;
            event->seq_step = ui_evt.data.seq.step;
            break;
        default:
            break;
    }

    return true;
}

int picoruby_ui_events_available(void)
{
    return ui_event_available();
}

float picoruby_ui_get_bpm(void)
{
    return ui_get_bpm();
}

void picoruby_ui_set_bpm(float bpm)
{
    ui_set_bpm(bpm);
}

void picoruby_ui_add_log(const char *text)
{
    if (text != NULL) {
        ui_add_log(text);
    }
}

void picoruby_ui_pad_set(int index, const char *label, int color, int type)
{
    if (index < 0 || index >= UI_PAD_COUNT) return;
    ui_pad_set((uint8_t)index, label, (pad_color_t)color, (pad_type_t)type);
}

void picoruby_ui_pad_clear(int index)
{
    if (index < 0 || index >= UI_PAD_COUNT) return;
    ui_pad_clear((uint8_t)index);
}

void picoruby_ui_pad_clear_all(void)
{
    ui_pad_clear_all();
}

bool picoruby_ui_pad_get_state(int index)
{
    if (index < 0 || index >= UI_PAD_COUNT) return false;
    return ui_pad_get_state((uint8_t)index);
}

void picoruby_ui_pad_set_label(int index, const char *label)
{
    if (index < 0 || index >= UI_PAD_COUNT) return;
    ui_pad_set_label((uint8_t)index, label);
}

void picoruby_ui_pad_set_color(int index, int color)
{
    if (index < 0 || index >= UI_PAD_COUNT) return;
    ui_pad_set_color((uint8_t)index, (pad_color_t)color);
}

/* Knobs */

void picoruby_ui_knob_set(int bank, int index, const char *label, int color,
                          float min, float max, float step, float value,
                          int origin, float sensitivity, bool notify)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return;
    if (index < 0 || index >= UI_KNOB_COUNT) return;
    ui_knob_set_config((uint8_t)bank, (uint8_t)index, label, (uint16_t)color,
                       min, max, step, value, (uint8_t)origin, sensitivity,
                       notify);
}

void picoruby_ui_knob_clear(int bank, int index)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return;
    if (index < 0 || index >= UI_KNOB_COUNT) return;
    ui_knob_clear((uint8_t)bank, (uint8_t)index);
}

void picoruby_ui_knob_clear_all(void)
{
    ui_knob_clear_all();
}

float picoruby_ui_knob_get_value(int bank, int index)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return 0.0f;
    if (index < 0 || index >= UI_KNOB_COUNT) return 0.0f;
    return ui_knob_get_value((uint8_t)bank, (uint8_t)index);
}

bool picoruby_ui_knob_set_value(int bank, int index, float value)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return false;
    if (index < 0 || index >= UI_KNOB_COUNT) return false;
    /* notify=false: this call came from Ruby, which invokes the block itself
     * rather than going the long way round through the event queue. */
    return ui_knob_set_value((uint8_t)bank, (uint8_t)index, value, false);
}

bool picoruby_ui_knob_reset(int bank, int index)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return false;
    if (index < 0 || index >= UI_KNOB_COUNT) return false;
    return ui_knob_reset((uint8_t)bank, (uint8_t)index);
}

void picoruby_ui_knob_set_label(int bank, int index, const char *label)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return;
    if (index < 0 || index >= UI_KNOB_COUNT) return;
    ui_knob_set_label((uint8_t)bank, (uint8_t)index, label);
}

void picoruby_ui_knob_set_color(int bank, int index, int color)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return;
    if (index < 0 || index >= UI_KNOB_COUNT) return;
    ui_knob_set_color((uint8_t)bank, (uint8_t)index, (uint16_t)color);
}

bool picoruby_ui_knob_assigned(int bank, int index)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return false;
    if (index < 0 || index >= UI_KNOB_COUNT) return false;
    const knob_config_t *k = ui_knob_get_config((uint8_t)bank, (uint8_t)index);
    return k != NULL && k->assigned;
}

int picoruby_ui_knob_count(void)
{
    return UI_KNOB_COUNT;
}

int picoruby_ui_knob_banks(void)
{
    return UI_KNOB_BANKS;
}

int picoruby_ui_knob_get_bank(void)
{
    return ui_knob_get_bank();
}

void picoruby_ui_knob_set_bank(int bank)
{
    if (bank < 0 || bank >= UI_KNOB_BANKS) return;
    ui_knob_set_bank((uint8_t)bank);
}

/* Screen switching (declared in ui_manager.h; use externs to avoid pulling a
 * C++ header into this C file). */
extern void ui_set_screen(ui_screen_index_t index);
extern ui_screen_index_t ui_get_current_screen(void);

void picoruby_ui_set_screen(int index)
{
    ui_set_screen((ui_screen_index_t)index);
}

int picoruby_ui_current_screen(void)
{
    return (int)ui_get_current_screen();
}

extern bool ui_request_screen(ui_screen_index_t index, int arg);
extern bool ui_request_back(void);
extern bool ui_request_step(int dir);
extern int  ui_get_requested_screen(int *arg);
extern void ui_menu_set_hidden(uint32_t hidden);
extern uint32_t ui_menu_get_hidden(void);
extern void ui_script_cleanup(void);
extern void ui_set_loading(const char *label, int done, int total);

bool picoruby_ui_request_screen(int index, int arg)
{
    if (index < 0 || index >= UI_SCREEN_COUNT) return false;
    return ui_request_screen((ui_screen_index_t)index, arg);
}

bool picoruby_ui_request_back(void)
{
    return ui_request_back();
}

bool picoruby_ui_request_step(int dir)
{
    return ui_request_step(dir);
}

int picoruby_ui_requested_screen(int *arg)
{
    return ui_get_requested_screen(arg);
}

void picoruby_ui_menu_set_hidden(uint32_t hidden)
{
    ui_menu_set_hidden(hidden);
}

uint32_t picoruby_ui_menu_get_hidden(void)
{
    return ui_menu_get_hidden();
}

void picoruby_ui_cleanup(void)
{
    ui_script_cleanup();
}

void picoruby_ui_set_loading(const char *label, int done, int total)
{
    ui_set_loading(label, done, total);
}

/* Layouts */

void picoruby_ui_display(int out[8])
{
    out[0] = UI_SCREEN_WIDTH;
    out[1] = UI_SCREEN_HEIGHT;
    out[2] = 0;
    out[3] = UI_CONTENT_Y;
    out[4] = UI_SCREEN_WIDTH;
    out[5] = UI_CONTENT_HEIGHT;
#if defined(UI_LAYOUT_LARGE)
    out[6] = 1;
#else
    out[6] = 0;
#endif
    out[7] = UI_LAYOUT_GAP;
}

bool picoruby_ui_layout_begin(int slot, const char *title)
{
    return ui_layout_begin(slot, title);
}

int picoruby_ui_layout_add_pad(int x, int y, int w, int h, const char *label,
                               int color, int type)
{
    return ui_layout_add_pad(x, y, w, h, label, (uint16_t)color, (uint8_t)type);
}

int picoruby_ui_layout_add_knob(int x, int y, int w, int h, const char *label,
                                int color, float min, float max, float step,
                                float value, int origin, float sensitivity,
                                bool notify)
{
    return ui_layout_add_knob(x, y, w, h, label, (uint16_t)color, min, max, step,
                              value, (uint8_t)origin, sensitivity, notify);
}

int picoruby_ui_layout_add_envelope(int x, int y, int w, int h, const char *label,
                                    int color, float attack, float decay,
                                    float sustain, float release, float max_time,
                                    bool notify)
{
    return ui_layout_add_envelope(x, y, w, h, label, (uint16_t)color, attack, decay,
                                  sustain, release, max_time, notify);
}

int picoruby_ui_layout_add_view(int kind, int x, int y, int w, int h)
{
    return ui_layout_add_view((uint8_t)kind, x, y, w, h);
}

bool picoruby_ui_layout_commit(void)
{
    return ui_layout_commit();
}

void picoruby_ui_layout_delete(int slot)
{
    ui_layout_delete(slot);
}

bool picoruby_ui_layout_pad_state(int slot, int id)
{
    return ui_layout_pad_state(slot, id);
}

void picoruby_ui_layout_set_label(int slot, int id, const char *label)
{
    ui_layout_set_label(slot, id, label);
}

void picoruby_ui_layout_set_color(int slot, int id, int color)
{
    ui_layout_set_color(slot, id, (uint16_t)color);
}

float picoruby_ui_layout_knob_value(int slot, int id)
{
    return ui_layout_knob_value(slot, id);
}

bool picoruby_ui_layout_knob_set_value(int slot, int id, float value)
{
    return ui_layout_knob_set_value(slot, id, value);
}

bool picoruby_ui_layout_knob_reset(int slot, int id)
{
    return ui_layout_knob_reset(slot, id);
}

bool picoruby_ui_layout_envelope_get(int slot, int id, float out[4])
{
    return ui_layout_envelope_get(slot, id, out);
}

bool picoruby_ui_layout_envelope_set(int slot, int id, float attack, float decay,
                                     float sustain, float release)
{
    return ui_layout_envelope_set(slot, id, attack, decay, sustain, release);
}

bool picoruby_ui_layout_envelope_reset(int slot, int id)
{
    return ui_layout_envelope_reset(slot, id);
}

/* Tombola sequencer */

void picoruby_ui_tombola_reset(void)
{
    ui_tombola_reset();
}

void picoruby_ui_tombola_start(void)
{
    ui_tombola_start();
}

void picoruby_ui_tombola_stop(void)
{
    ui_tombola_stop();
}

bool picoruby_ui_tombola_running(void)
{
    return ui_tombola_running();
}

bool picoruby_ui_tombola_set_f(const char *name, float value)
{
    return ui_tombola_set_f(name, value);
}

bool picoruby_ui_tombola_set_i(const char *name, int value)
{
    return ui_tombola_set_i(name, value);
}

float picoruby_ui_tombola_get_f(const char *name)
{
    return ui_tombola_get_f(name);
}

int picoruby_ui_tombola_get_i(const char *name)
{
    return ui_tombola_get_i(name);
}

void picoruby_ui_tombola_set_scale(const uint8_t *notes, int len)
{
    ui_tombola_set_scale(notes, len);
}

int picoruby_ui_tombola_add_ball(int note, int channel, int color, float velocity_scale)
{
    return ui_tombola_add_ball(note, channel, (uint16_t)color, velocity_scale);
}

bool picoruby_ui_tombola_remove_ball(int index)
{
    return ui_tombola_remove_ball(index);
}

void picoruby_ui_tombola_clear_balls(void)
{
    ui_tombola_clear_balls();
}

int picoruby_ui_tombola_ball_count(void)
{
    return ui_tombola_ball_count();
}

/* XYPad */

void picoruby_ui_xypad_reset(void)
{
    ui_xypad_reset();
}

void picoruby_ui_xypad_set_max_touches(int n)
{
    if (n < 0) return;
    ui_xypad_set_max_touches((uint8_t)n);
}

int picoruby_ui_xypad_get_max_touches(void)
{
    return ui_xypad_get_max_touches();
}

bool picoruby_ui_xypad_set_f(int index, const char *name, float value)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return false;
    return ui_xypad_set_f((uint8_t)index, name, value);
}

bool picoruby_ui_xypad_set_i(int index, const char *name, int value)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return false;
    return ui_xypad_set_i((uint8_t)index, name, value);
}

float picoruby_ui_xypad_get_f(int index, const char *name)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return 0.0f;
    return ui_xypad_get_f((uint8_t)index, name);
}

int picoruby_ui_xypad_get_i(int index, const char *name)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return 0;
    return ui_xypad_get_i((uint8_t)index, name);
}

void picoruby_ui_xypad_set_scale(int index, const uint8_t *notes, int len)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return;
    if (len < 0) len = 0;
    if (len > UI_XYPAD_MAX_SCALE) len = UI_XYPAD_MAX_SCALE;
    ui_xypad_set_scale((uint8_t)index, notes, (uint8_t)len);
}

int picoruby_ui_xypad_get_scale(int index, uint8_t *out, int max_len)
{
    if (index < 0 || index >= UI_XYPAD_MAX_TOUCHES) return 0;
    if (max_len < 0) max_len = 0;
    if (max_len > UI_XYPAD_MAX_SCALE) max_len = UI_XYPAD_MAX_SCALE;
    return ui_xypad_get_scale((uint8_t)index, out, (uint8_t)max_len);
}

/* Keys */

void picoruby_ui_keys_reset(void)
{
    ui_keys_reset();
}

bool picoruby_ui_keys_set_i(const char *name, int value)
{
    return ui_keys_set_i(name, value);
}

int picoruby_ui_keys_get_i(const char *name)
{
    return ui_keys_get_i(name);
}

void picoruby_ui_keys_set_key_color(int note, int color)
{
    ui_keys_set_key_color(note, color);
}

int picoruby_ui_keys_get_key_color(int note)
{
    return ui_keys_get_key_color(note);
}

void picoruby_ui_keys_clear_key_colors(void)
{
    ui_keys_clear_key_colors();
}

void picoruby_ui_keys_set_channel_color(int channel, int color)
{
    ui_keys_set_channel_color(channel, color);
}

void picoruby_ui_keys_receive(int cin, int b1, int b2, int b3)
{
    ui_keys_receive((uint8_t)cin, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3);
}

void picoruby_ui_keys_clear_lights(void)
{
    ui_keys_clear_lights();
}

bool picoruby_ui_keys_is_lit(int note)
{
    return ui_keys_is_lit(note);
}

bool picoruby_ui_keys_is_pressed(int note)
{
    return ui_keys_is_pressed(note);
}

/* --- Sequencer --- */

void picoruby_ui_seq_reset(int lines)
{
    ui_seq_reset(lines);
}

bool picoruby_ui_seq_set_i(int pattern, int line, const char *name, int value)
{
    return ui_seq_set_i(pattern, line, name, value);
}

int picoruby_ui_seq_get_i(int pattern, int line, const char *name)
{
    return ui_seq_get_i(pattern, line, name);
}

bool picoruby_ui_seq_set_name(int line, const char *name)
{
    return ui_seq_set_name(line, name);
}

int picoruby_ui_seq_get_name(int line, char *buf, int size)
{
    return ui_seq_get_name(line, buf, size);
}

int picoruby_ui_seq_add_line(void)
{
    return ui_seq_add_line();
}

bool picoruby_ui_seq_remove_line(int line)
{
    return ui_seq_remove_line(line);
}

bool picoruby_ui_seq_clear_pattern(int pattern)
{
    return ui_seq_clear_pattern(pattern);
}

bool picoruby_ui_seq_copy_pattern(int src, int dst)
{
    return ui_seq_copy_pattern(src, dst);
}

bool picoruby_ui_seq_copy_line(int src_pattern, int src_line, int dst_pattern, int dst_line)
{
    return ui_seq_copy_line(src_pattern, src_line, dst_pattern, dst_line);
}

int picoruby_ui_seq_place(int pattern, int line, int step, int len,
                          const uint8_t *notes, const uint8_t *velocities, int n)
{
    return ui_seq_place(pattern, line, step, len, notes, velocities, n);
}

int picoruby_ui_seq_remove(int pattern, int line, int step, int note)
{
    return ui_seq_remove(pattern, line, step, note);
}

bool picoruby_ui_seq_set_length(int pattern, int line, int step, int len)
{
    return ui_seq_set_length(pattern, line, step, len);
}

bool picoruby_ui_seq_add_event(int pattern, int line, int pos, int len, int note, int velocity)
{
    return ui_seq_add_event(pattern, line, pos, len, note, velocity);
}

void picoruby_ui_seq_clear(int pattern, int line, int mode)
{
    ui_seq_clear(pattern, line, mode);
}

int picoruby_ui_seq_notes_read(int pattern, int line, int mode, int start, int *out, int max)
{
    return ui_seq_notes_read(pattern, line, mode, start, out, max);
}

int picoruby_ui_seq_get_chord(uint8_t *notes, uint8_t *velocities, int max)
{
    return ui_seq_get_chord(notes, velocities, max);
}

void picoruby_ui_seq_set_chord(const uint8_t *notes, const uint8_t *velocities, int n)
{
    ui_seq_set_chord(notes, velocities, n);
}

int picoruby_ui_seq_get_chain(uint8_t *patterns, uint8_t *repeats, int max)
{
    return ui_seq_get_chain(patterns, repeats, max);
}

bool picoruby_ui_seq_set_chain(const uint8_t *patterns, const uint8_t *repeats, int n)
{
    return ui_seq_set_chain(patterns, repeats, n);
}

void picoruby_ui_seq_receive(int cin, int b1, int b2, int b3)
{
    ui_seq_receive((uint8_t)cin, (uint8_t)b1, (uint8_t)b2, (uint8_t)b3);
}

bool picoruby_ui_seq_dump_begin(bool dirty_flag)
{
    return ui_seq_dump_begin(dirty_flag);
}

int picoruby_ui_seq_dump_next(uint8_t *buf, int cap)
{
    return ui_seq_dump_next(buf, cap);
}

void picoruby_ui_seq_dump_end(bool saved, bool autosave)
{
    ui_seq_dump_end(saved, autosave);
}

bool picoruby_ui_seq_load_begin(void)
{
    return ui_seq_load_begin();
}

int picoruby_ui_seq_load_feed(const uint8_t *data, int len)
{
    return ui_seq_load_feed(data, len);
}

int picoruby_ui_seq_load_commit(bool autosave)
{
    return ui_seq_load_commit(autosave);
}

void picoruby_ui_seq_load_abort(void)
{
    ui_seq_load_abort();
}

const char *picoruby_ui_seq_load_error_name(int err)
{
    return ui_seq_load_error_name(err);
}

int picoruby_ui_seq_take_request(char *name, int size)
{
    return ui_seq_take_request(name, size);
}

void picoruby_ui_seq_set_files(const char *names)
{
    ui_seq_set_files(names);
}

void picoruby_ui_seq_set_status(const char *message)
{
    ui_seq_set_status(message);
}

/* --- Tempo --- */

int picoruby_ui_tempo_bit(void)
{
    return ui_tempo_bit();
}

void picoruby_ui_tempo_set_running(bool running)
{
    ui_tempo_set_running(running);
}

bool picoruby_ui_tempo_running(void)
{
    return ui_tempo_running();
}

void picoruby_ui_tempo_tap(void)
{
    ui_tempo_tap();
}

bool picoruby_ui_tempo_sync(void)
{
    return ui_tempo_sync();
}

void picoruby_ui_tempo_set_sync(bool on)
{
    ui_tempo_set_sync(on);
}

/* --- MIDI Devices --- */

int picoruby_ui_mididev_register(const char *key, const char *name, int bit,
                                 int color, bool builtin)
{
    return ui_mididev_register(key, name, (uint8_t)bit, (uint16_t)color, builtin);
}

bool picoruby_ui_mididev_unregister(int bit)
{
    return ui_mididev_unregister((uint8_t)bit);
}

bool picoruby_ui_mididev_activate(const char *key, int bit)
{
    return ui_mididev_activate(key, (uint8_t)bit);
}

int picoruby_ui_mididev_count(void)
{
    return ui_mididev_count();
}

bool picoruby_ui_mididev_get(int index, int *bit, int *flags, char *key, int key_len,
                             char *name, int name_len)
{
    ui_mididev_t d;
    if (!ui_mididev_get(index, &d)) return false;
    *bit = d.bit;
    *flags = d.flags;
    snprintf(key, key_len, "%s", d.key);
    snprintf(name, name_len, "%s", d.name);
    return true;
}

int picoruby_ui_mididev_selected(bool out)
{
    return ui_mididev_selected(out);
}

void picoruby_ui_mididev_select(bool out, int index, bool save)
{
    ui_mididev_select(out, index, save);
}

int picoruby_ui_mididev_restore(void)
{
    return ui_mididev_restore();
}

bool picoruby_ui_mididev_save(void)
{
    return ui_mididev_save();
}

bool picoruby_ui_mididev_take_wanted(char *key, int len)
{
    return ui_mididev_take_wanted(key, len);
}

int picoruby_ui_mididev_connected_mask(void)
{
    return ui_mididev_connected_mask();
}

int picoruby_ui_mididev_serial(void)
{
    return (int)ui_mididev_serial();
}

/* --- Named filters --- */

bool picoruby_ui_filter_define(const char *name, const char *label, const char *params)
{
    return ui_filter_define(name, label, params);
}

bool picoruby_ui_filter_undefine(const char *name)
{
    return ui_filter_undefine(name);
}

int picoruby_ui_filter_serial(void)
{
    return (int)ui_filter_serial();
}

int picoruby_ui_filter_live_count(void)
{
    return ui_filter_live_count();
}

bool picoruby_ui_filter_live_get(int index, int *src, int *dst, int *rev, char *text, int len)
{
    uint8_t s, d;
    uint32_t r;
    ui_filter_chain_t chain;
    if (!ui_filter_live_get(index, &s, &d, &r, &chain)) return false;
    *src = s;
    *dst = d;
    *rev = (int)r;
    ui_filter_chain_format(&chain, text, len);
    return true;
}

int picoruby_ui_filter_chain_set(int src, int dst, const char *text, int save)
{
    ui_filter_chain_t chain;
    ui_filter_chain_parse(text, &chain);
    return (int)ui_filter_chain_set((uint8_t)src, (uint8_t)dst, &chain,
                                    save ? UI_FILTER_SAVE_LATER : UI_FILTER_SAVE_NONE);
}
