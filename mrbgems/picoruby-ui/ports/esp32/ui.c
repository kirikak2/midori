/*
 * PicoRuby UI - ESP32/M5Stack port
 */

#include <stddef.h>
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
    event->hit_ball = 0;
    event->hit_side = 0;
    event->hit_note = 0;
    event->hit_velocity = 0;
    event->knob_bank = 0;
    event->knob_index = 0;
    event->knob_final = false;
    event->knob_value = 0.0f;

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
            event->screen = ui_evt.data.screen;
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
