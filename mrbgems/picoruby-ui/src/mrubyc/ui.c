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
}
