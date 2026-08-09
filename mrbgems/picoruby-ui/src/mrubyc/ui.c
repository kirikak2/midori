/*
 * PicoRuby UI - mrubyc bindings
 */

#include <mrubyc.h>
#include "../../include/ui.h"

/*
 * UI._pop_event
 * Returns: Hash with event data or nil
 */
static void
c_ui_pop_event(mrbc_vm *vm, mrbc_value v[], int argc)
{
    picoruby_ui_event_t event;
    if (!picoruby_ui_pop_event(&event)) {
        SET_NIL_RETURN();
        return;
    }

    /* Convert event type to symbol */
    const char *type_str;
    switch (event.type) {
        case PICORUBY_UI_EVENT_BPM_CHANGE:   type_str = "bpm_change"; break;
        case PICORUBY_UI_EVENT_PAD_PRESS:    type_str = "pad_press"; break;
        case PICORUBY_UI_EVENT_PAD_RELEASE:  type_str = "pad_release"; break;
        case PICORUBY_UI_EVENT_SYNC_MODE:    type_str = "sync_mode"; break;
        case PICORUBY_UI_EVENT_SCREEN_CHANGE: type_str = "screen_change"; break;
        case PICORUBY_UI_EVENT_TOMBOLA_HIT:  type_str = "tombola_hit"; break;
        case PICORUBY_UI_EVENT_KNOB_CHANGE:  type_str = "knob_change"; break;
        case PICORUBY_UI_EVENT_KNOB_BANK:    type_str = "knob_bank"; break;
        default:                             type_str = "unknown"; break;
    }

    mrbc_value hash = mrbc_hash_new(vm, 4);

    /* :type */
    mrbc_value key_type = mrbc_symbol_value(mrbc_str_to_symid("type"));
    mrbc_value val_type = mrbc_symbol_value(mrbc_str_to_symid(type_str));
    mrbc_hash_set(&hash, &key_type, &val_type);

    /* Event-specific data */
    switch (event.type) {
        case PICORUBY_UI_EVENT_BPM_CHANGE:
        {
            mrbc_value key_bpm = mrbc_symbol_value(mrbc_str_to_symid("bpm"));
            mrbc_value val_bpm = mrbc_float_value(vm, event.bpm);
            mrbc_hash_set(&hash, &key_bpm, &val_bpm);
            break;
        }
        case PICORUBY_UI_EVENT_PAD_PRESS:
        case PICORUBY_UI_EVENT_PAD_RELEASE:
        {
            mrbc_value key_index = mrbc_symbol_value(mrbc_str_to_symid("index"));
            mrbc_value val_index = mrbc_integer_value(event.pad_index);
            mrbc_hash_set(&hash, &key_index, &val_index);

            mrbc_value key_state = mrbc_symbol_value(mrbc_str_to_symid("state"));
            mrbc_value val_state = mrbc_bool_value(event.pad_state);
            mrbc_hash_set(&hash, &key_state, &val_state);
            break;
        }
        case PICORUBY_UI_EVENT_SYNC_MODE:
        {
            mrbc_value key_enabled = mrbc_symbol_value(mrbc_str_to_symid("enabled"));
            mrbc_value val_enabled = mrbc_bool_value(event.sync_mode);
            mrbc_hash_set(&hash, &key_enabled, &val_enabled);
            break;
        }
        case PICORUBY_UI_EVENT_SCREEN_CHANGE:
        {
            mrbc_value key_screen = mrbc_symbol_value(mrbc_str_to_symid("screen"));
            mrbc_value val_screen = mrbc_integer_value(event.screen);
            mrbc_hash_set(&hash, &key_screen, &val_screen);
            break;
        }
        case PICORUBY_UI_EVENT_TOMBOLA_HIT:
        {
            mrbc_value key_ball = mrbc_symbol_value(mrbc_str_to_symid("ball"));
            mrbc_value val_ball = mrbc_integer_value(event.hit_ball);
            mrbc_hash_set(&hash, &key_ball, &val_ball);

            mrbc_value key_side = mrbc_symbol_value(mrbc_str_to_symid("side"));
            mrbc_value val_side = mrbc_integer_value(event.hit_side);
            mrbc_hash_set(&hash, &key_side, &val_side);

            mrbc_value key_note = mrbc_symbol_value(mrbc_str_to_symid("note"));
            mrbc_value val_note = mrbc_integer_value(event.hit_note);
            mrbc_hash_set(&hash, &key_note, &val_note);

            mrbc_value key_vel = mrbc_symbol_value(mrbc_str_to_symid("velocity"));
            mrbc_value val_vel = mrbc_integer_value(event.hit_velocity);
            mrbc_hash_set(&hash, &key_vel, &val_vel);
            break;
        }
        case PICORUBY_UI_EVENT_KNOB_CHANGE:
        {
            /* 1-based, to match the index UI.knob was called with. */
            mrbc_value key_bank = mrbc_symbol_value(mrbc_str_to_symid("bank"));
            mrbc_value val_bank = mrbc_integer_value(event.knob_bank + 1);
            mrbc_hash_set(&hash, &key_bank, &val_bank);

            mrbc_value key_index = mrbc_symbol_value(mrbc_str_to_symid("index"));
            mrbc_value val_index = mrbc_integer_value(event.knob_index + 1);
            mrbc_hash_set(&hash, &key_index, &val_index);

            mrbc_value key_value = mrbc_symbol_value(mrbc_str_to_symid("value"));
            mrbc_value val_value = mrbc_float_value(vm, event.knob_value);
            mrbc_hash_set(&hash, &key_value, &val_value);

            mrbc_value key_final = mrbc_symbol_value(mrbc_str_to_symid("final"));
            mrbc_value val_final = mrbc_bool_value(event.knob_final);
            mrbc_hash_set(&hash, &key_final, &val_final);
            break;
        }
        case PICORUBY_UI_EVENT_KNOB_BANK:
        {
            mrbc_value key_bank = mrbc_symbol_value(mrbc_str_to_symid("bank"));
            mrbc_value val_bank = mrbc_integer_value(event.knob_bank + 1);
            mrbc_hash_set(&hash, &key_bank, &val_bank);
            break;
        }
        default:
            break;
    }

    SET_RETURN(hash);
}

/*
 * UI._events_available
 */
static void
c_ui_events_available(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_INT_RETURN(picoruby_ui_events_available());
}

/*
 * UI._bpm
 */
static void
c_ui_bpm(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_FLOAT_RETURN(picoruby_ui_get_bpm());
}

/*
 * UI._set_bpm(value)
 */
static void
c_ui_set_bpm(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1) {
        SET_NIL_RETURN();
        return;
    }
    float bpm;
    if (v[1].tt == MRBC_TT_FLOAT) {
        bpm = (float)v[1].d;
    } else if (v[1].tt == MRBC_TT_INTEGER) {
        bpm = (float)v[1].i;
    } else {
        SET_NIL_RETURN();
        return;
    }
    picoruby_ui_set_bpm(bpm);
    SET_NIL_RETURN();
}

/*
 * UI._log(text)
 * Output text to Screen Log
 */
static void
c_ui_log(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1) {
        SET_NIL_RETURN();
        return;
    }

    mrbc_value str = v[1];
    if (mrbc_type(str) != MRBC_TT_STRING) {
        /* Convert to string if not already */
        str = mrbc_send(vm, v, argc, &str, "to_s", 0);
    }

    const char *text = (const char *)mrbc_string_cstr(&str);
    picoruby_ui_add_log(text);

    SET_NIL_RETURN();
}

/*
 * UI._set_screen(index) / UI._current_screen
 */
static void
c_ui_set_screen(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc >= 1 && mrbc_type(v[1]) == MRBC_TT_INTEGER) {
        picoruby_ui_set_screen((int)mrbc_integer(v[1]));
    }
    SET_NIL_RETURN();
}

static void
c_ui_current_screen(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_INT_RETURN(picoruby_ui_current_screen());
}

/*
 * UI._pad_set(index, label, color, type)
 */
static void
c_ui_pad_set(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 4) {
        SET_NIL_RETURN();
        return;
    }

    int index = mrbc_integer(v[1]);
    const char *label = (const char *)mrbc_string_cstr(&v[2]);
    int color = mrbc_integer(v[3]);
    int type = mrbc_integer(v[4]);

    picoruby_ui_pad_set(index, label, color, type);

    SET_NIL_RETURN();
}

/*
 * UI._pad_clear(index)
 */
static void
c_ui_pad_clear(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1) {
        SET_NIL_RETURN();
        return;
    }

    int index = mrbc_integer(v[1]);
    picoruby_ui_pad_clear(index);

    SET_NIL_RETURN();
}

/*
 * UI._pad_clear_all
 */
static void
c_ui_pad_clear_all(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_pad_clear_all();
    SET_NIL_RETURN();
}

/*
 * UI._pad_get_state(index)
 */
static void
c_ui_pad_get_state(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1) {
        SET_FALSE_RETURN();
        return;
    }

    int index = mrbc_integer(v[1]);
    bool state = picoruby_ui_pad_get_state(index);

    SET_BOOL_RETURN(state);
}

/*
 * UI._pad_set_label(index, label)
 */
static void
c_ui_pad_set_label(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 2) {
        SET_NIL_RETURN();
        return;
    }

    int index = mrbc_integer(v[1]);
    const char *label = (const char *)mrbc_string_cstr(&v[2]);

    picoruby_ui_pad_set_label(index, label);

    SET_NIL_RETURN();
}

/*
 * UI._pad_set_color(index, color)
 */
static void
c_ui_pad_set_color(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 2) {
        SET_NIL_RETURN();
        return;
    }

    int index = mrbc_integer(v[1]);
    int color = mrbc_integer(v[2]);

    picoruby_ui_pad_set_color(index, color);

    SET_NIL_RETURN();
}

/* ------------------------------------------------------------------------
 * Tombola sequencer
 *
 * The Ruby-visible object is UI::Tombola (defined in mrblib/ui.rb); these are
 * the raw module functions it drives. Parameters go through _tombola_set_f /
 * _tombola_set_i keyed by name so adding a knob never touches this file.
 * ------------------------------------------------------------------------ */

/* Accept either a Symbol or a String for parameter names. */
static const char *
tombola_param_name(mrbc_value *v)
{
    if (mrbc_type(*v) == MRBC_TT_SYMBOL) {
        return mrbc_symbol_cstr(v);
    }
    if (mrbc_type(*v) == MRBC_TT_STRING) {
        return (const char *)mrbc_string_cstr(v);
    }
    return NULL;
}

static float
tombola_to_f(mrbc_value *v)
{
    if (mrbc_type(*v) == MRBC_TT_FLOAT)   return (float)v->d;
    if (mrbc_type(*v) == MRBC_TT_INTEGER) return (float)v->i;
    return 0.0f;
}

/*
 * UI._tombola_set_f(name, value) / UI._tombola_set_i(name, value)
 */
static void
c_ui_tombola_set_f(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 2) {
        SET_FALSE_RETURN();
        return;
    }
    const char *name = tombola_param_name(&v[1]);
    if (name == NULL) {
        SET_FALSE_RETURN();
        return;
    }
    SET_BOOL_RETURN(picoruby_ui_tombola_set_f(name, tombola_to_f(&v[2])));
}

static void
c_ui_tombola_set_i(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 2) {
        SET_FALSE_RETURN();
        return;
    }
    const char *name = tombola_param_name(&v[1]);
    if (name == NULL) {
        SET_FALSE_RETURN();
        return;
    }

    int value;
    switch (mrbc_type(v[2])) {
        case MRBC_TT_INTEGER: value = (int)v[2].i; break;
        case MRBC_TT_FLOAT:   value = (int)v[2].d; break;
        case MRBC_TT_TRUE:    value = 1; break;
        case MRBC_TT_FALSE:
        case MRBC_TT_NIL:     value = 0; break;
        default:
            SET_FALSE_RETURN();
            return;
    }
    SET_BOOL_RETURN(picoruby_ui_tombola_set_i(name, value));
}

/*
 * UI._tombola_get_f(name) / UI._tombola_get_i(name)
 */
static void
c_ui_tombola_get_f(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1) {
        SET_FLOAT_RETURN(0.0);
        return;
    }
    const char *name = tombola_param_name(&v[1]);
    SET_FLOAT_RETURN(name ? picoruby_ui_tombola_get_f(name) : 0.0f);
}

static void
c_ui_tombola_get_i(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 1) {
        SET_INT_RETURN(0);
        return;
    }
    const char *name = tombola_param_name(&v[1]);
    SET_INT_RETURN(name ? picoruby_ui_tombola_get_i(name) : 0);
}

/*
 * UI._tombola_set_scale(array_of_note_numbers)
 */
static void
c_ui_tombola_set_scale(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 1 || mrbc_type(v[1]) != MRBC_TT_ARRAY) {
        SET_NIL_RETURN();
        return;
    }

    uint8_t notes[16];
    int len = mrbc_array_size(&v[1]);
    if (len > (int)sizeof(notes)) len = (int)sizeof(notes);

    int count = 0;
    for (int i = 0; i < len; i++) {
        mrbc_value item = mrbc_array_get(&v[1], i);
        if (mrbc_type(item) != MRBC_TT_INTEGER) continue;
        int note = (int)item.i;
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        notes[count++] = (uint8_t)note;
    }

    if (count > 0) {
        picoruby_ui_tombola_set_scale(notes, count);
    }
    SET_NIL_RETURN();
}

/*
 * UI._tombola_add_ball(note, channel, color, velocity_scale)
 * note / channel may be nil to inherit the sequencer's defaults.
 * Returns the ball index, or -1 when all slots are taken.
 */
static void
c_ui_tombola_add_ball(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 4) {
        SET_INT_RETURN(-1);
        return;
    }

    int note    = (mrbc_type(v[1]) == MRBC_TT_INTEGER) ? (int)v[1].i : -1;
    int channel = (mrbc_type(v[2]) == MRBC_TT_INTEGER) ? (int)v[2].i : -1;
    int color   = (mrbc_type(v[3]) == MRBC_TT_INTEGER) ? (int)v[3].i : 0xFFFF;
    float scale = tombola_to_f(&v[4]);
    if (scale <= 0.0f) scale = 1.0f;

    SET_INT_RETURN(picoruby_ui_tombola_add_ball(note, channel, color, scale));
}

static void
c_ui_tombola_remove_ball(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 1 || mrbc_type(v[1]) != MRBC_TT_INTEGER) {
        SET_FALSE_RETURN();
        return;
    }
    SET_BOOL_RETURN(picoruby_ui_tombola_remove_ball((int)v[1].i));
}

static void
c_ui_tombola_clear_balls(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_tombola_clear_balls();
    SET_NIL_RETURN();
}

static void
c_ui_tombola_ball_count(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    SET_INT_RETURN(picoruby_ui_tombola_ball_count());
}

static void
c_ui_tombola_start(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_tombola_start();
    SET_NIL_RETURN();
}

static void
c_ui_tombola_stop(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_tombola_stop();
    SET_NIL_RETURN();
}

static void
c_ui_tombola_reset(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_tombola_reset();
    SET_NIL_RETURN();
}

static void
c_ui_tombola_running(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    SET_BOOL_RETURN(picoruby_ui_tombola_running());
}

/* ------------------------------------------------------------------------
 * Knobs
 *
 * The Ruby-visible API is UI.knob and friends (mrblib/ui.rb); these are the
 * raw module functions behind them. Values cross as floats and indices are
 * 0-based here, 1-based in Ruby.
 * ------------------------------------------------------------------------ */

static float
knob_to_f(mrbc_value *v)
{
    if (mrbc_type(*v) == MRBC_TT_FLOAT)   return (float)v->d;
    if (mrbc_type(*v) == MRBC_TT_INTEGER) return (float)v->i;
    return 0.0f;
}

static int
knob_to_i(mrbc_value *v)
{
    if (mrbc_type(*v) == MRBC_TT_INTEGER) return (int)v->i;
    if (mrbc_type(*v) == MRBC_TT_FLOAT)   return (int)v->d;
    return 0;
}

/*
 * UI._knob_set(bank, index, label, color, min, max, step, value,
 *              origin, sensitivity, notify)
 */
static void
c_ui_knob_set(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 11) {
        SET_NIL_RETURN();
        return;
    }

    picoruby_ui_knob_set(knob_to_i(&v[1]), knob_to_i(&v[2]),
                         (const char *)mrbc_string_cstr(&v[3]),
                         knob_to_i(&v[4]),
                         knob_to_f(&v[5]), knob_to_f(&v[6]), knob_to_f(&v[7]),
                         knob_to_f(&v[8]), knob_to_i(&v[9]), knob_to_f(&v[10]),
                         mrbc_type(v[11]) == MRBC_TT_TRUE);
    SET_NIL_RETURN();
}

/*
 * UI._knob_clear(bank, index) / UI._knob_clear_all
 */
static void
c_ui_knob_clear(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 2) {
        SET_NIL_RETURN();
        return;
    }
    picoruby_ui_knob_clear(knob_to_i(&v[1]), knob_to_i(&v[2]));
    SET_NIL_RETURN();
}

static void
c_ui_knob_clear_all(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    picoruby_ui_knob_clear_all();
    SET_NIL_RETURN();
}

/*
 * UI._knob_value(bank, index)
 */
static void
c_ui_knob_value(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 2) {
        SET_FLOAT_RETURN(0.0);
        return;
    }
    SET_FLOAT_RETURN(picoruby_ui_knob_get_value(knob_to_i(&v[1]), knob_to_i(&v[2])));
}

/*
 * UI._knob_set_value(bank, index, value)
 * Returns true when the stored value actually moved, which is what tells Ruby
 * whether the block is due to be called.
 */
static void
c_ui_knob_set_value(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 3) {
        SET_FALSE_RETURN();
        return;
    }
    SET_BOOL_RETURN(picoruby_ui_knob_set_value(knob_to_i(&v[1]), knob_to_i(&v[2]),
                                               knob_to_f(&v[3])));
}

static void
c_ui_knob_reset(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 2) {
        SET_FALSE_RETURN();
        return;
    }
    SET_BOOL_RETURN(picoruby_ui_knob_reset(knob_to_i(&v[1]), knob_to_i(&v[2])));
}

static void
c_ui_knob_set_label(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 3) {
        SET_NIL_RETURN();
        return;
    }
    picoruby_ui_knob_set_label(knob_to_i(&v[1]), knob_to_i(&v[2]),
                               (const char *)mrbc_string_cstr(&v[3]));
    SET_NIL_RETURN();
}

static void
c_ui_knob_set_color(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 3) {
        SET_NIL_RETURN();
        return;
    }
    picoruby_ui_knob_set_color(knob_to_i(&v[1]), knob_to_i(&v[2]), knob_to_i(&v[3]));
    SET_NIL_RETURN();
}

static void
c_ui_knob_assigned(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 2) {
        SET_FALSE_RETURN();
        return;
    }
    SET_BOOL_RETURN(picoruby_ui_knob_assigned(knob_to_i(&v[1]), knob_to_i(&v[2])));
}

static void
c_ui_knob_count(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    SET_INT_RETURN(picoruby_ui_knob_count());
}

static void
c_ui_knob_banks(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    SET_INT_RETURN(picoruby_ui_knob_banks());
}

static void
c_ui_knob_get_bank(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm; (void)v; (void)argc;
    SET_INT_RETURN(picoruby_ui_knob_get_bank());
}

static void
c_ui_knob_set_bank(mrbc_vm *vm, mrbc_value v[], int argc)
{
    (void)vm;
    if (argc < 1) {
        SET_NIL_RETURN();
        return;
    }
    picoruby_ui_knob_set_bank(knob_to_i(&v[1]));
    SET_NIL_RETURN();
}

/*
 * Gem initialization
 */
void
mrbc_ui_init(mrbc_vm *vm)
{
    /* Define UI module */
    mrbc_class *module_UI = mrbc_define_module(vm, "UI");

    /* UI module methods */
    mrbc_define_method(vm, module_UI, "_pop_event", c_ui_pop_event);
    mrbc_define_method(vm, module_UI, "_events_available", c_ui_events_available);
    mrbc_define_method(vm, module_UI, "_bpm", c_ui_bpm);
    mrbc_define_method(vm, module_UI, "_set_bpm", c_ui_set_bpm);
    mrbc_define_method(vm, module_UI, "_log", c_ui_log);
    mrbc_define_method(vm, module_UI, "_set_screen", c_ui_set_screen);
    mrbc_define_method(vm, module_UI, "_current_screen", c_ui_current_screen);

    /* Pad methods */
    mrbc_define_method(vm, module_UI, "_pad_set", c_ui_pad_set);
    mrbc_define_method(vm, module_UI, "_pad_clear", c_ui_pad_clear);
    mrbc_define_method(vm, module_UI, "_pad_clear_all", c_ui_pad_clear_all);
    mrbc_define_method(vm, module_UI, "_pad_get_state", c_ui_pad_get_state);
    mrbc_define_method(vm, module_UI, "_pad_set_label", c_ui_pad_set_label);
    mrbc_define_method(vm, module_UI, "_pad_set_color", c_ui_pad_set_color);

    /* Knob methods */
    mrbc_define_method(vm, module_UI, "_knob_set", c_ui_knob_set);
    mrbc_define_method(vm, module_UI, "_knob_clear", c_ui_knob_clear);
    mrbc_define_method(vm, module_UI, "_knob_clear_all", c_ui_knob_clear_all);
    mrbc_define_method(vm, module_UI, "_knob_value", c_ui_knob_value);
    mrbc_define_method(vm, module_UI, "_knob_set_value", c_ui_knob_set_value);
    mrbc_define_method(vm, module_UI, "_knob_reset", c_ui_knob_reset);
    mrbc_define_method(vm, module_UI, "_knob_set_label", c_ui_knob_set_label);
    mrbc_define_method(vm, module_UI, "_knob_set_color", c_ui_knob_set_color);
    mrbc_define_method(vm, module_UI, "_knob_assigned", c_ui_knob_assigned);
    mrbc_define_method(vm, module_UI, "_knob_count", c_ui_knob_count);
    mrbc_define_method(vm, module_UI, "_knob_banks", c_ui_knob_banks);
    mrbc_define_method(vm, module_UI, "_knob_get_bank", c_ui_knob_get_bank);
    mrbc_define_method(vm, module_UI, "_knob_set_bank", c_ui_knob_set_bank);

    /* Tombola methods */
    mrbc_define_method(vm, module_UI, "_tombola_set_f", c_ui_tombola_set_f);
    mrbc_define_method(vm, module_UI, "_tombola_set_i", c_ui_tombola_set_i);
    mrbc_define_method(vm, module_UI, "_tombola_get_f", c_ui_tombola_get_f);
    mrbc_define_method(vm, module_UI, "_tombola_get_i", c_ui_tombola_get_i);
    mrbc_define_method(vm, module_UI, "_tombola_set_scale", c_ui_tombola_set_scale);
    mrbc_define_method(vm, module_UI, "_tombola_add_ball", c_ui_tombola_add_ball);
    mrbc_define_method(vm, module_UI, "_tombola_remove_ball", c_ui_tombola_remove_ball);
    mrbc_define_method(vm, module_UI, "_tombola_clear_balls", c_ui_tombola_clear_balls);
    mrbc_define_method(vm, module_UI, "_tombola_ball_count", c_ui_tombola_ball_count);
    mrbc_define_method(vm, module_UI, "_tombola_start", c_ui_tombola_start);
    mrbc_define_method(vm, module_UI, "_tombola_stop", c_ui_tombola_stop);
    mrbc_define_method(vm, module_UI, "_tombola_reset", c_ui_tombola_reset);
    mrbc_define_method(vm, module_UI, "_tombola_running", c_ui_tombola_running);
}
