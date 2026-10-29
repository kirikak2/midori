/*
 * PicoRuby UI - mruby bindings
 *
 * Feature parity with src/mrubyc/ui.c: same method names, same argument
 * handling (numbers may be Integer or Float, parameter names Symbol or
 * String, bad arguments return a neutral value rather than raising).
 */

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>

#include "../../include/ui.h"

/* ------------------------------------------------------------------------
 * Argument helpers
 * ------------------------------------------------------------------------ */

static float
val_to_f(mrb_value v)
{
    if (mrb_float_p(v))   return (float)mrb_float(v);
    if (mrb_integer_p(v)) return (float)mrb_integer(v);
    return 0.0f;
}

static int
val_to_i(mrb_value v)
{
    if (mrb_integer_p(v)) return (int)mrb_integer(v);
    if (mrb_float_p(v))   return (int)mrb_float(v);
    return 0;
}

/* Integer for the _set_i family: true/false/nil count as 1/0. Returns false
 * for anything else. */
static mrb_bool
val_to_flag_i(mrb_value v, int *out)
{
    if (mrb_integer_p(v)) { *out = (int)mrb_integer(v); return TRUE; }
    if (mrb_float_p(v))   { *out = (int)mrb_float(v);   return TRUE; }
    if (mrb_true_p(v))    { *out = 1; return TRUE; }
    if (mrb_false_p(v) || mrb_nil_p(v)) { *out = 0; return TRUE; }
    return FALSE;
}

/* Parameter names may be given as a Symbol or a String. */
static const char *
param_name(mrb_state *mrb, mrb_value v)
{
    if (mrb_symbol_p(v)) return mrb_sym_name(mrb, mrb_symbol(v));
    if (mrb_string_p(v)) return RSTRING_CSTR(mrb, v);
    return NULL;
}

static const char *
str_arg(mrb_state *mrb, mrb_value v)
{
    if (mrb_string_p(v)) return RSTRING_CSTR(mrb, v);
    return "";
}

/* Copy up to `cap` Integer elements of `ary` (clamped to 0..127) into
 * `notes`; non-Integer elements are skipped. */
static int
collect_notes(mrb_value ary, uint8_t *notes, int cap)
{
    int len = (int)RARRAY_LEN(ary);
    if (len > cap) len = cap;
    int count = 0;
    for (int i = 0; i < len; i++) {
        mrb_value item = RARRAY_PTR(ary)[i];
        if (!mrb_integer_p(item)) continue;
        mrb_int note = mrb_integer(item);
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        notes[count++] = (uint8_t)note;
    }
    return count;
}

#define HSET(key, val) mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(key)), (val))

/* ------------------------------------------------------------------------
 * Events / BPM / log / screen
 * ------------------------------------------------------------------------ */

/* UI._pop_event -> Hash or nil */
static mrb_value
mrb_ui_pop_event(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_event_t event;
    if (!picoruby_ui_pop_event(&event)) {
        return mrb_nil_value();
    }

    mrb_sym type;
    switch (event.type) {
        case PICORUBY_UI_EVENT_BPM_CHANGE:    type = MRB_SYM(bpm_change); break;
        case PICORUBY_UI_EVENT_PAD_PRESS:     type = MRB_SYM(pad_press); break;
        case PICORUBY_UI_EVENT_PAD_RELEASE:   type = MRB_SYM(pad_release); break;
        case PICORUBY_UI_EVENT_SYNC_MODE:     type = MRB_SYM(sync_mode); break;
        case PICORUBY_UI_EVENT_SCREEN_CHANGE: type = MRB_SYM(screen_change); break;
        case PICORUBY_UI_EVENT_TOMBOLA_HIT:   type = MRB_SYM(tombola_hit); break;
        case PICORUBY_UI_EVENT_KNOB_CHANGE:   type = MRB_SYM(knob_change); break;
        case PICORUBY_UI_EVENT_KNOB_BANK:     type = MRB_SYM(knob_bank); break;
        case PICORUBY_UI_EVENT_XYPAD_TOUCH:   type = MRB_SYM(xypad_touch); break;
        case PICORUBY_UI_EVENT_WIDGET:        type = MRB_SYM(widget); break;
        case PICORUBY_UI_EVENT_KEY:           type = MRB_SYM(key); break;
        case PICORUBY_UI_EVENT_MIDI_DEVICES:  type = MRB_SYM(midi_devices); break;
        case PICORUBY_UI_EVENT_SEQUENCER:     type = MRB_SYM(sequencer); break;
        default:                              type = MRB_SYM(unknown); break;
    }

    mrb_value hash = mrb_hash_new_capa(mrb, 4);
    HSET(type, mrb_symbol_value(type));

    switch (event.type) {
        case PICORUBY_UI_EVENT_BPM_CHANGE:
            HSET(bpm, mrb_float_value(mrb, event.bpm));
            break;
        case PICORUBY_UI_EVENT_PAD_PRESS:
        case PICORUBY_UI_EVENT_PAD_RELEASE:
            HSET(index, mrb_fixnum_value(event.pad_index));
            HSET(state, mrb_bool_value(event.pad_state));
            break;
        case PICORUBY_UI_EVENT_SYNC_MODE:
            HSET(enabled, mrb_bool_value(event.sync_mode));
            break;
        case PICORUBY_UI_EVENT_SCREEN_CHANGE: {
            /* Screen numbers here; UI.pop_event turns them into Symbols. */
            mrb_sym source;
            switch (event.screen_source) {
                case 0:  source = MRB_SYM(touch);  break;
                case 1:  source = MRB_SYM(script); break;
                default: source = MRB_SYM(system); break;
            }
            HSET(screen, mrb_fixnum_value(event.screen));
            HSET(from, mrb_fixnum_value(event.screen_from));
            HSET(source, mrb_symbol_value(source));
            HSET(layout, event.screen_layout >= 0 ? mrb_fixnum_value(event.screen_layout)
                                                  : mrb_nil_value());
            break;
        }
        case PICORUBY_UI_EVENT_TOMBOLA_HIT:
            HSET(ball, mrb_fixnum_value(event.hit_ball));
            HSET(side, mrb_fixnum_value(event.hit_side));
            HSET(note, mrb_fixnum_value(event.hit_note));
            HSET(velocity, mrb_fixnum_value(event.hit_velocity));
            break;
        case PICORUBY_UI_EVENT_KNOB_CHANGE:
            /* 1-based, to match the index UI.knob was called with. */
            HSET(bank, mrb_fixnum_value(event.knob_bank + 1));
            HSET(index, mrb_fixnum_value(event.knob_index + 1));
            HSET(value, mrb_float_value(mrb, event.knob_value));
            HSET(final, mrb_bool_value(event.knob_final));
            break;
        case PICORUBY_UI_EVENT_KNOB_BANK:
            HSET(bank, mrb_fixnum_value(event.knob_bank + 1));
            break;
        case PICORUBY_UI_EVENT_XYPAD_TOUCH: {
            /* 1-based, to match the index pad.slot() was called with. */
            mrb_sym phase;
            switch (event.xypad_phase) {
                case 0:  phase = MRB_SYM(down); break;
                case 1:  phase = MRB_SYM(move); break;
                default: phase = MRB_SYM(up);   break;
            }
            HSET(slot, mrb_fixnum_value(event.xypad_slot + 1));
            HSET(phase, mrb_symbol_value(phase));
            HSET(channel, mrb_fixnum_value(event.xypad_channel));
            HSET(note, mrb_fixnum_value(event.xypad_note));
            HSET(bend_semitones, mrb_float_value(mrb, event.xypad_bend));
            HSET(x, mrb_float_value(mrb, event.xypad_x));
            HSET(y, mrb_float_value(mrb, event.xypad_y));
            break;
        }
        case PICORUBY_UI_EVENT_WIDGET: {
            /* layout is the slot and id the widget index, both 0-based: they
             * are keys into the Ruby-side registry, not numbers a script sees. */
            mrb_sym phase;
            switch (event.widget_phase) {
                case 0:  phase = MRB_SYM(press);   break;
                case 1:  phase = MRB_SYM(release); break;
                default: phase = MRB_SYM(change);  break;
            }
            HSET(layout, mrb_fixnum_value(event.widget_layout));
            HSET(id, mrb_fixnum_value(event.widget_id));
            mrb_sym kind;
            switch (event.widget_kind) {
                case 0:  kind = MRB_SYM(pad);      break;
                case 1:  kind = MRB_SYM(knob);     break;
                case 7:  kind = MRB_SYM(envelope); break;
                default: kind = MRB_SYM(unknown);  break;
            }
            HSET(kind, mrb_symbol_value(kind));
            HSET(phase, mrb_symbol_value(phase));
            HSET(state, mrb_bool_value(event.widget_state));
            HSET(value, mrb_float_value(mrb, event.widget_value));
            HSET(final, mrb_bool_value(event.widget_final));
            break;
        }
        case PICORUBY_UI_EVENT_KEY:
            HSET(note, mrb_fixnum_value(event.key_note));
            HSET(velocity, mrb_fixnum_value(event.key_velocity));
            HSET(channel, mrb_fixnum_value(event.key_channel));
            HSET(state, mrb_bool_value(event.key_on));
            break;
        case PICORUBY_UI_EVENT_MIDI_DEVICES: {
            /* Bits; UI::MIDIDevices turns them into devices */
            mrb_sym kind;
            switch (event.mididev_kind) {
                case 0:  kind = MRB_SYM(select_in);  break;
                case 1:  kind = MRB_SYM(select_out); break;
                case 2:  kind = MRB_SYM(patch);      break;
                case 3:  kind = MRB_SYM(unpatch);    break;
                case 4:  kind = MRB_SYM(connect);    break;
                default: kind = MRB_SYM(disconnect); break;
            }
            HSET(kind, mrb_symbol_value(kind));
            HSET(from, mrb_fixnum_value(event.mididev_from));
            HSET(to, mrb_fixnum_value(event.mididev_to));
            /* 0..15, -1 all (nil), -2 the clock alone (:clock), -3 several
             * ([0, 9, :clock]) */
            mrb_value ch;
            if (event.mididev_channel == -3) {
                ch = mrb_ary_new(mrb);
                for (int c = 0; c < 16; c++) {
                    if (event.mididev_sel & (1u << c)) mrb_ary_push(mrb, ch, mrb_fixnum_value(c));
                }
                if (event.mididev_sel & 0x10000u) mrb_ary_push(mrb, ch, mrb_symbol_value(MRB_SYM(clock)));
            } else if (event.mididev_channel == -2) {
                ch = mrb_symbol_value(MRB_SYM(clock));
            } else if (event.mididev_channel < 0) {
                ch = mrb_nil_value();
            } else {
                ch = mrb_fixnum_value(event.mididev_channel);
            }
            HSET(channel, ch);
            break;
        }
        case PICORUBY_UI_EVENT_SEQUENCER: {
            /* UI::Sequencer hands these to on_step / on_pattern / on_edit */
            mrb_sym kind;
            switch (event.seq_kind) {
                case 0:  kind = MRB_SYM(step);    break;
                case 1:  kind = MRB_SYM(pattern); break;
                default: kind = MRB_SYM(edit);    break;
            }
            HSET(kind, mrb_symbol_value(kind));
            HSET(pattern, event.seq_pattern >= 0 ? mrb_fixnum_value(event.seq_pattern) : mrb_nil_value());
            HSET(from, event.seq_from >= 0 ? mrb_fixnum_value(event.seq_from) : mrb_nil_value());
            HSET(line, event.seq_line >= 0 ? mrb_fixnum_value(event.seq_line) : mrb_nil_value());
            HSET(step, mrb_fixnum_value(event.seq_step));
            break;
        }
        default:
            break;
    }

    return hash;
}

/* UI._events_available */
static mrb_value
mrb_ui_events_available(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_events_available());
}

/* UI._bpm */
static mrb_value
mrb_ui_bpm(mrb_state *mrb, mrb_value self)
{
    return mrb_float_value(mrb, picoruby_ui_get_bpm());
}

/* UI._set_bpm(value) */
static mrb_value
mrb_ui_set_bpm(mrb_state *mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    if (mrb_float_p(v) || mrb_integer_p(v)) {
        picoruby_ui_set_bpm(val_to_f(v));
    }
    return mrb_nil_value();
}

/* UI._log(text) - output text to the Screen Log */
static mrb_value
mrb_ui_log(mrb_state *mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    mrb_value str = mrb_obj_as_string(mrb, v);
    picoruby_ui_add_log(RSTRING_CSTR(mrb, str));
    return mrb_nil_value();
}

/* UI._set_screen(index) / UI._current_screen */
static mrb_value
mrb_ui_set_screen(mrb_state *mrb, mrb_value self)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    if (mrb_integer_p(v)) {
        picoruby_ui_set_screen((int)mrb_integer(v));
    }
    return mrb_nil_value();
}

static mrb_value
mrb_ui_current_screen(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_current_screen());
}

/* UI._request_screen(index, arg) -> true / false */
static mrb_value
mrb_ui_request_screen(mrb_state *mrb, mrb_value self)
{
    mrb_value index, arg;
    mrb_get_args(mrb, "oo", &index, &arg);
    if (!mrb_integer_p(index)) return mrb_false_value();
    int a = mrb_integer_p(arg) ? (int)mrb_integer(arg) : -1;
    return mrb_bool_value(picoruby_ui_request_screen((int)mrb_integer(index), a));
}

/* UI._nav(:back | :next | :prev) -> true / false */
static mrb_value
mrb_ui_nav(mrb_state *mrb, mrb_value self)
{
    mrb_sym dir;
    mrb_get_args(mrb, "n", &dir);
    if (dir == MRB_SYM(back)) return mrb_bool_value(picoruby_ui_request_back());
    if (dir == MRB_SYM(next)) return mrb_bool_value(picoruby_ui_request_step(1));
    if (dir == MRB_SYM(prev)) return mrb_bool_value(picoruby_ui_request_step(-1));
    return mrb_false_value();
}

/* UI._requested_screen -> [index, arg] (a pending switch's target, else the
 * current screen; arg is the layout slot or -1) */
static mrb_value
mrb_ui_requested_screen(mrb_state *mrb, mrb_value self)
{
    int arg = -1;
    int index = picoruby_ui_requested_screen(&arg);
    mrb_value pair[2] = { mrb_fixnum_value(index), mrb_fixnum_value(arg) };
    return mrb_ary_new_from_values(mrb, 2, pair);
}

/* UI._menu_set_hidden(mask) / UI._menu_hidden */
static mrb_value
mrb_ui_menu_set_hidden(mrb_state *mrb, mrb_value self)
{
    mrb_int mask;
    mrb_get_args(mrb, "i", &mask);
    picoruby_ui_menu_set_hidden((uint32_t)mask);
    return mrb_nil_value();
}

static mrb_value
mrb_ui_menu_hidden(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value((mrb_int)picoruby_ui_menu_get_hidden());
}

/* UI._loading(label, done, total): the loading lock; label nil ends it */
static mrb_value
mrb_ui_loading(mrb_state *mrb, mrb_value self)
{
    mrb_value label, done, total;
    mrb_get_args(mrb, "ooo", &label, &done, &total);
    picoruby_ui_set_loading(mrb_nil_p(label) ? NULL : str_arg(mrb, label),
                            val_to_i(done), val_to_i(total));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------------
 * Pads
 * ------------------------------------------------------------------------ */

/* UI._pad_set(index, label, color, type) */
static mrb_value
mrb_ui_pad_set(mrb_state *mrb, mrb_value self)
{
    mrb_value index, label, color, type;
    mrb_get_args(mrb, "oooo", &index, &label, &color, &type);
    picoruby_ui_pad_set(val_to_i(index), str_arg(mrb, label), val_to_i(color), val_to_i(type));
    return mrb_nil_value();
}

/* UI._pad_clear(index) */
static mrb_value
mrb_ui_pad_clear(mrb_state *mrb, mrb_value self)
{
    mrb_value index;
    mrb_get_args(mrb, "o", &index);
    picoruby_ui_pad_clear(val_to_i(index));
    return mrb_nil_value();
}

/* UI._pad_clear_all */
static mrb_value
mrb_ui_pad_clear_all(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_pad_clear_all();
    return mrb_nil_value();
}

/* UI._pad_get_state(index) */
static mrb_value
mrb_ui_pad_get_state(mrb_state *mrb, mrb_value self)
{
    mrb_value index;
    mrb_get_args(mrb, "o", &index);
    return mrb_bool_value(picoruby_ui_pad_get_state(val_to_i(index)));
}

/* UI._pad_set_label(index, label) */
static mrb_value
mrb_ui_pad_set_label(mrb_state *mrb, mrb_value self)
{
    mrb_value index, label;
    mrb_get_args(mrb, "oo", &index, &label);
    picoruby_ui_pad_set_label(val_to_i(index), str_arg(mrb, label));
    return mrb_nil_value();
}

/* UI._pad_set_color(index, color) */
static mrb_value
mrb_ui_pad_set_color(mrb_state *mrb, mrb_value self)
{
    mrb_value index, color;
    mrb_get_args(mrb, "oo", &index, &color);
    picoruby_ui_pad_set_color(val_to_i(index), val_to_i(color));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------------
 * Tombola sequencer
 *
 * The Ruby-visible object is UI::Tombola (defined in mrblib/ui.rb); these are
 * the raw module functions it drives. Parameters go through _tombola_set_f /
 * _tombola_set_i keyed by name so adding a knob never touches this file.
 * ------------------------------------------------------------------------ */

/* UI._tombola_set_f(name, value) / UI._tombola_set_i(name, value) */
static mrb_value
mrb_ui_tombola_set_f(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v, value;
    mrb_get_args(mrb, "oo", &name_v, &value);
    const char *name = param_name(mrb, name_v);
    if (name == NULL) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_tombola_set_f(name, val_to_f(value)));
}

static mrb_value
mrb_ui_tombola_set_i(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v, value;
    int i;
    mrb_get_args(mrb, "oo", &name_v, &value);
    const char *name = param_name(mrb, name_v);
    if (name == NULL || !val_to_flag_i(value, &i)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_tombola_set_i(name, i));
}

/* UI._tombola_get_f(name) / UI._tombola_get_i(name) */
static mrb_value
mrb_ui_tombola_get_f(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v;
    mrb_get_args(mrb, "o", &name_v);
    const char *name = param_name(mrb, name_v);
    return mrb_float_value(mrb, name ? picoruby_ui_tombola_get_f(name) : 0.0f);
}

static mrb_value
mrb_ui_tombola_get_i(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v;
    mrb_get_args(mrb, "o", &name_v);
    const char *name = param_name(mrb, name_v);
    return mrb_fixnum_value(name ? picoruby_ui_tombola_get_i(name) : 0);
}

/* UI._tombola_set_scale(array_of_note_numbers) */
static mrb_value
mrb_ui_tombola_set_scale(mrb_state *mrb, mrb_value self)
{
    mrb_value ary;
    mrb_get_args(mrb, "o", &ary);
    if (!mrb_array_p(ary)) return mrb_nil_value();

    uint8_t notes[16];
    int count = collect_notes(ary, notes, (int)sizeof(notes));
    if (count > 0) {
        picoruby_ui_tombola_set_scale(notes, count);
    }
    return mrb_nil_value();
}

/*
 * UI._tombola_add_ball(note, channel, color, velocity_scale)
 * note / channel may be nil to inherit the sequencer's defaults.
 * Returns the ball index, or -1 when all slots are taken.
 */
static mrb_value
mrb_ui_tombola_add_ball(mrb_state *mrb, mrb_value self)
{
    mrb_value note_v, channel_v, color_v, scale_v;
    mrb_get_args(mrb, "oooo", &note_v, &channel_v, &color_v, &scale_v);

    int note    = mrb_integer_p(note_v)    ? (int)mrb_integer(note_v)    : -1;
    int channel = mrb_integer_p(channel_v) ? (int)mrb_integer(channel_v) : -1;
    int color   = mrb_integer_p(color_v)   ? (int)mrb_integer(color_v)   : 0xFFFF;
    float scale = val_to_f(scale_v);
    if (scale <= 0.0f) scale = 1.0f;

    return mrb_fixnum_value(picoruby_ui_tombola_add_ball(note, channel, color, scale));
}

static mrb_value
mrb_ui_tombola_remove_ball(mrb_state *mrb, mrb_value self)
{
    mrb_value index;
    mrb_get_args(mrb, "o", &index);
    if (!mrb_integer_p(index)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_tombola_remove_ball((int)mrb_integer(index)));
}

static mrb_value
mrb_ui_tombola_clear_balls(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_tombola_clear_balls();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_tombola_ball_count(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_tombola_ball_count());
}

static mrb_value
mrb_ui_tombola_start(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_tombola_start();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_tombola_stop(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_tombola_stop();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_tombola_reset(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_tombola_reset();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_tombola_running(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_tombola_running());
}

/* ------------------------------------------------------------------------
 * Knobs
 *
 * The Ruby-visible API is UI.knob and friends (mrblib/ui.rb); these are the
 * raw module functions behind them. Values cross as floats and indices are
 * 0-based here, 1-based in Ruby.
 * ------------------------------------------------------------------------ */

/*
 * UI._knob_set(bank, index, label, color, min, max, step, value,
 *              origin, sensitivity, notify)
 */
static mrb_value
mrb_ui_knob_set(mrb_state *mrb, mrb_value self)
{
    mrb_value a[11];
    mrb_get_args(mrb, "ooooooooooo", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5],
                 &a[6], &a[7], &a[8], &a[9], &a[10]);
    picoruby_ui_knob_set(val_to_i(a[0]), val_to_i(a[1]), str_arg(mrb, a[2]),
                         val_to_i(a[3]),
                         val_to_f(a[4]), val_to_f(a[5]), val_to_f(a[6]),
                         val_to_f(a[7]), val_to_i(a[8]), val_to_f(a[9]),
                         mrb_true_p(a[10]));
    return mrb_nil_value();
}

/* UI._knob_clear(bank, index) / UI._knob_clear_all */
static mrb_value
mrb_ui_knob_clear(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index;
    mrb_get_args(mrb, "oo", &bank, &index);
    picoruby_ui_knob_clear(val_to_i(bank), val_to_i(index));
    return mrb_nil_value();
}

static mrb_value
mrb_ui_knob_clear_all(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_knob_clear_all();
    return mrb_nil_value();
}

/* UI._knob_value(bank, index) */
static mrb_value
mrb_ui_knob_value(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index;
    mrb_get_args(mrb, "oo", &bank, &index);
    return mrb_float_value(mrb, picoruby_ui_knob_get_value(val_to_i(bank), val_to_i(index)));
}

/*
 * UI._knob_set_value(bank, index, value)
 * Returns true when the stored value actually moved, which is what tells Ruby
 * whether the block is due to be called.
 */
static mrb_value
mrb_ui_knob_set_value(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index, value;
    mrb_get_args(mrb, "ooo", &bank, &index, &value);
    return mrb_bool_value(picoruby_ui_knob_set_value(val_to_i(bank), val_to_i(index),
                                                     val_to_f(value)));
}

static mrb_value
mrb_ui_knob_reset(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index;
    mrb_get_args(mrb, "oo", &bank, &index);
    return mrb_bool_value(picoruby_ui_knob_reset(val_to_i(bank), val_to_i(index)));
}

static mrb_value
mrb_ui_knob_set_label(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index, label;
    mrb_get_args(mrb, "ooo", &bank, &index, &label);
    picoruby_ui_knob_set_label(val_to_i(bank), val_to_i(index), str_arg(mrb, label));
    return mrb_nil_value();
}

static mrb_value
mrb_ui_knob_set_color(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index, color;
    mrb_get_args(mrb, "ooo", &bank, &index, &color);
    picoruby_ui_knob_set_color(val_to_i(bank), val_to_i(index), val_to_i(color));
    return mrb_nil_value();
}

static mrb_value
mrb_ui_knob_assigned(mrb_state *mrb, mrb_value self)
{
    mrb_value bank, index;
    mrb_get_args(mrb, "oo", &bank, &index);
    return mrb_bool_value(picoruby_ui_knob_assigned(val_to_i(bank), val_to_i(index)));
}

static mrb_value
mrb_ui_knob_count(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_knob_count());
}

static mrb_value
mrb_ui_knob_banks(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_knob_banks());
}

static mrb_value
mrb_ui_knob_get_bank(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_knob_get_bank());
}

static mrb_value
mrb_ui_knob_set_bank(mrb_state *mrb, mrb_value self)
{
    mrb_value bank;
    mrb_get_args(mrb, "o", &bank);
    picoruby_ui_knob_set_bank(val_to_i(bank));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------------
 * XYPad
 *
 * The Ruby-visible object is UI::XYPad (mrblib/ui.rb); these are the raw
 * module functions it drives. Slot parameters go through _xypad_set_f /
 * _xypad_set_i keyed by name, the same shape as Tombola. index is 0-based
 * here (Ruby's slot numbers are 1-based and converted in mrblib).
 * ------------------------------------------------------------------------ */

static mrb_value
mrb_ui_xypad_reset(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_xypad_reset();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_xypad_set_max_touches(mrb_state *mrb, mrb_value self)
{
    mrb_value n;
    mrb_get_args(mrb, "o", &n);
    if (mrb_integer_p(n)) {
        picoruby_ui_xypad_set_max_touches((int)mrb_integer(n));
    }
    return mrb_nil_value();
}

static mrb_value
mrb_ui_xypad_get_max_touches(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_xypad_get_max_touches());
}

/* UI._xypad_set_f(index, name, value) / UI._xypad_set_i(index, name, value) */
static mrb_value
mrb_ui_xypad_set_f(mrb_state *mrb, mrb_value self)
{
    mrb_value index, name_v, value;
    mrb_get_args(mrb, "ooo", &index, &name_v, &value);
    const char *name = param_name(mrb, name_v);
    if (!mrb_integer_p(index) || name == NULL) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_xypad_set_f((int)mrb_integer(index), name,
                                                  val_to_f(value)));
}

static mrb_value
mrb_ui_xypad_set_i(mrb_state *mrb, mrb_value self)
{
    mrb_value index, name_v, value;
    int i;
    mrb_get_args(mrb, "ooo", &index, &name_v, &value);
    const char *name = param_name(mrb, name_v);
    if (!mrb_integer_p(index) || name == NULL || !val_to_flag_i(value, &i)) {
        return mrb_false_value();
    }
    return mrb_bool_value(picoruby_ui_xypad_set_i((int)mrb_integer(index), name, i));
}

/* UI._xypad_get_f(index, name) / UI._xypad_get_i(index, name) */
static mrb_value
mrb_ui_xypad_get_f(mrb_state *mrb, mrb_value self)
{
    mrb_value index, name_v;
    mrb_get_args(mrb, "oo", &index, &name_v);
    const char *name = param_name(mrb, name_v);
    if (!mrb_integer_p(index) || name == NULL) return mrb_float_value(mrb, 0.0);
    return mrb_float_value(mrb, picoruby_ui_xypad_get_f((int)mrb_integer(index), name));
}

static mrb_value
mrb_ui_xypad_get_i(mrb_state *mrb, mrb_value self)
{
    mrb_value index, name_v;
    mrb_get_args(mrb, "oo", &index, &name_v);
    const char *name = param_name(mrb, name_v);
    if (!mrb_integer_p(index) || name == NULL) return mrb_fixnum_value(0);
    return mrb_fixnum_value(picoruby_ui_xypad_get_i((int)mrb_integer(index), name));
}

/* UI._xypad_set_scale(index, array_of_note_numbers) */
static mrb_value
mrb_ui_xypad_set_scale(mrb_state *mrb, mrb_value self)
{
    mrb_value index, ary;
    mrb_get_args(mrb, "oo", &index, &ary);
    if (!mrb_integer_p(index) || !mrb_array_p(ary)) return mrb_nil_value();

    uint8_t notes[16];
    int count = collect_notes(ary, notes, (int)sizeof(notes));
    picoruby_ui_xypad_set_scale((int)mrb_integer(index), notes, count);
    return mrb_nil_value();
}

/* UI._xypad_get_scale(index) -> Array */
static mrb_value
mrb_ui_xypad_get_scale(mrb_state *mrb, mrb_value self)
{
    mrb_value index;
    mrb_get_args(mrb, "o", &index);
    if (!mrb_integer_p(index)) return mrb_ary_new(mrb);

    uint8_t notes[16];
    int count = picoruby_ui_xypad_get_scale((int)mrb_integer(index), notes, (int)sizeof(notes));
    mrb_value ary = mrb_ary_new_capa(mrb, count);
    for (int i = 0; i < count; i++) {
        mrb_ary_push(mrb, ary, mrb_fixnum_value(notes[i]));
    }
    return ary;
}

/* ------------------------------------------------------------------------
 * Layouts
 *
 * The Ruby-visible API is UI.layout and UI::Layout (mrblib/ui_layout.rb),
 * which resolves the grid to pixel rectangles before anything gets here.
 * slot and id are 0-based.
 * ------------------------------------------------------------------------ */

/* UI._display -> [width, height, content_x, content_y, content_w, content_h,
 *                 large?, gap] */
static mrb_value
mrb_ui_display(mrb_state *mrb, mrb_value self)
{
    int d[8];
    picoruby_ui_display(d);
    mrb_value v[8];
    for (int i = 0; i < 8; i++) v[i] = mrb_fixnum_value(d[i]);
    v[6] = mrb_bool_value(d[6] != 0);
    return mrb_ary_new_from_values(mrb, 8, v);
}

/* UI._layout_begin(slot, title) -> true / false (false: no display) */
static mrb_value
mrb_ui_layout_begin(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, title;
    mrb_get_args(mrb, "oo", &slot, &title);
    return mrb_bool_value(picoruby_ui_layout_begin(val_to_i(slot), str_arg(mrb, title)));
}

/* UI._layout_add_pad(x, y, w, h, label, color, type) -> id or -1 */
static mrb_value
mrb_ui_layout_add_pad(mrb_state *mrb, mrb_value self)
{
    mrb_value a[7];
    mrb_get_args(mrb, "ooooooo", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]);
    return mrb_fixnum_value(picoruby_ui_layout_add_pad(
        val_to_i(a[0]), val_to_i(a[1]), val_to_i(a[2]), val_to_i(a[3]),
        str_arg(mrb, a[4]), val_to_i(a[5]), val_to_i(a[6])));
}

/* UI._layout_add_knob(x, y, w, h, label, color, min, max, step, value,
 *                     origin, sensitivity, notify) -> id or -1 */
static mrb_value
mrb_ui_layout_add_knob(mrb_state *mrb, mrb_value self)
{
    mrb_value a[13];
    mrb_get_args(mrb, "ooooooooooooo", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5],
                 &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12]);
    return mrb_fixnum_value(picoruby_ui_layout_add_knob(
        val_to_i(a[0]), val_to_i(a[1]), val_to_i(a[2]), val_to_i(a[3]),
        str_arg(mrb, a[4]), val_to_i(a[5]),
        val_to_f(a[6]), val_to_f(a[7]), val_to_f(a[8]), val_to_f(a[9]),
        val_to_i(a[10]), val_to_f(a[11]), mrb_true_p(a[12])));
}

/* UI._layout_add_envelope(x, y, w, h, label, color, attack, decay, sustain,
 *                         release, max_time, notify) -> id or -1 */
static mrb_value
mrb_ui_layout_add_envelope(mrb_state *mrb, mrb_value self)
{
    mrb_value a[12];
    mrb_get_args(mrb, "oooooooooooo", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5],
                 &a[6], &a[7], &a[8], &a[9], &a[10], &a[11]);
    return mrb_fixnum_value(picoruby_ui_layout_add_envelope(
        val_to_i(a[0]), val_to_i(a[1]), val_to_i(a[2]), val_to_i(a[3]),
        str_arg(mrb, a[4]), val_to_i(a[5]),
        val_to_f(a[6]), val_to_f(a[7]), val_to_f(a[8]), val_to_f(a[9]),
        val_to_f(a[10]), mrb_true_p(a[11])));
}

/* UI._layout_add_view(kind, x, y, w, h) -> id or -1 */
static mrb_value
mrb_ui_layout_add_view(mrb_state *mrb, mrb_value self)
{
    mrb_value a[5];
    mrb_get_args(mrb, "ooooo", &a[0], &a[1], &a[2], &a[3], &a[4]);
    return mrb_fixnum_value(picoruby_ui_layout_add_view(
        val_to_i(a[0]), val_to_i(a[1]), val_to_i(a[2]), val_to_i(a[3]), val_to_i(a[4])));
}

static mrb_value
mrb_ui_layout_commit(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_layout_commit());
}

static mrb_value
mrb_ui_layout_delete(mrb_state *mrb, mrb_value self)
{
    mrb_value slot;
    mrb_get_args(mrb, "o", &slot);
    picoruby_ui_layout_delete(val_to_i(slot));
    return mrb_nil_value();
}

/* UI._lw_pad_state(slot, id) */
static mrb_value
mrb_ui_lw_pad_state(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id;
    mrb_get_args(mrb, "oo", &slot, &id);
    return mrb_bool_value(picoruby_ui_layout_pad_state(val_to_i(slot), val_to_i(id)));
}

/* UI._lw_set_label(slot, id, label) / UI._lw_set_color(slot, id, color) */
static mrb_value
mrb_ui_lw_set_label(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id, label;
    mrb_get_args(mrb, "ooo", &slot, &id, &label);
    picoruby_ui_layout_set_label(val_to_i(slot), val_to_i(id), str_arg(mrb, label));
    return mrb_nil_value();
}

static mrb_value
mrb_ui_lw_set_color(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id, color;
    mrb_get_args(mrb, "ooo", &slot, &id, &color);
    picoruby_ui_layout_set_color(val_to_i(slot), val_to_i(id), val_to_i(color));
    return mrb_nil_value();
}

/* UI._lw_knob_value(slot, id) */
static mrb_value
mrb_ui_lw_knob_value(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id;
    mrb_get_args(mrb, "oo", &slot, &id);
    return mrb_float_value(mrb, picoruby_ui_layout_knob_value(val_to_i(slot), val_to_i(id)));
}

/* UI._lw_knob_set_value(slot, id, value) -> whether the value moved */
static mrb_value
mrb_ui_lw_knob_set_value(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id, value;
    mrb_get_args(mrb, "ooo", &slot, &id, &value);
    return mrb_bool_value(picoruby_ui_layout_knob_set_value(val_to_i(slot), val_to_i(id),
                                                            val_to_f(value)));
}

static mrb_value
mrb_ui_lw_knob_reset(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id;
    mrb_get_args(mrb, "oo", &slot, &id);
    return mrb_bool_value(picoruby_ui_layout_knob_reset(val_to_i(slot), val_to_i(id)));
}

/* UI._lw_env_values(slot, id) -> [attack, decay, sustain, release] or nil */
static mrb_value
mrb_ui_lw_env_values(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id;
    mrb_get_args(mrb, "oo", &slot, &id);
    float v[4];
    if (!picoruby_ui_layout_envelope_get(val_to_i(slot), val_to_i(id), v)) {
        return mrb_nil_value();
    }
    mrb_value a[4];
    for (int i = 0; i < 4; i++) a[i] = mrb_float_value(mrb, v[i]);
    return mrb_ary_new_from_values(mrb, 4, a);
}

/* UI._lw_env_set(slot, id, attack, decay, sustain, release) -> whether any
 * moved. nil leaves that value alone. */
static mrb_value
mrb_ui_lw_env_set(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id, a[4];
    mrb_get_args(mrb, "oooooo", &slot, &id, &a[0], &a[1], &a[2], &a[3]);
    float v[4];
    for (int i = 0; i < 4; i++) v[i] = mrb_nil_p(a[i]) ? -1.0f : val_to_f(a[i]);
    return mrb_bool_value(picoruby_ui_layout_envelope_set(val_to_i(slot), val_to_i(id),
                                                          v[0], v[1], v[2], v[3]));
}

static mrb_value
mrb_ui_lw_env_reset(mrb_state *mrb, mrb_value self)
{
    mrb_value slot, id;
    mrb_get_args(mrb, "oo", &slot, &id);
    return mrb_bool_value(picoruby_ui_layout_envelope_reset(val_to_i(slot), val_to_i(id)));
}

/* ------------------------------------------------------------------------
 * Keys
 *
 * The Ruby-visible object is UI::Keys (mrblib/ui.rb). Parameters go through
 * _keys_set_i / _keys_get_i keyed by name, the same shape as Tombola.
 * ------------------------------------------------------------------------ */

static mrb_value
mrb_ui_keys_reset(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_keys_reset();
    return mrb_nil_value();
}

/* UI._keys_set_i(name, value) -> true / false (unknown name) */
static mrb_value
mrb_ui_keys_set_i(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v, value;
    int i;
    mrb_get_args(mrb, "oo", &name_v, &value);
    const char *name = param_name(mrb, name_v);
    if (name == NULL || !val_to_flag_i(value, &i)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_keys_set_i(name, i));
}

/* UI._keys_get_i(name) */
static mrb_value
mrb_ui_keys_get_i(mrb_state *mrb, mrb_value self)
{
    mrb_value name_v;
    mrb_get_args(mrb, "o", &name_v);
    const char *name = param_name(mrb, name_v);
    if (name == NULL) return mrb_fixnum_value(0);
    return mrb_fixnum_value(picoruby_ui_keys_get_i(name));
}

/* UI._keys_set_key_color(note, rgb565 or -1) */
static mrb_value
mrb_ui_keys_set_key_color(mrb_state *mrb, mrb_value self)
{
    mrb_value note, color;
    mrb_get_args(mrb, "oo", &note, &color);
    picoruby_ui_keys_set_key_color(val_to_i(note), val_to_i(color));
    return mrb_nil_value();
}

/* UI._keys_key_color(note) -> rgb565, or -1 when not set */
static mrb_value
mrb_ui_keys_key_color(mrb_state *mrb, mrb_value self)
{
    mrb_value note;
    mrb_get_args(mrb, "o", &note);
    return mrb_fixnum_value(picoruby_ui_keys_get_key_color(val_to_i(note)));
}

static mrb_value
mrb_ui_keys_clear_key_colors(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_keys_clear_key_colors();
    return mrb_nil_value();
}

/* UI._keys_set_channel_color(channel, rgb565 or -1) */
static mrb_value
mrb_ui_keys_set_channel_color(mrb_state *mrb, mrb_value self)
{
    mrb_value channel, color;
    mrb_get_args(mrb, "oo", &channel, &color);
    picoruby_ui_keys_set_channel_color(val_to_i(channel), val_to_i(color));
    return mrb_nil_value();
}

/* UI._keys_receive(cin, b1, b2, b3): one USB-MIDI packet sent to the keyboard */
static mrb_value
mrb_ui_keys_receive(mrb_state *mrb, mrb_value self)
{
    mrb_value cin, b1, b2, b3;
    mrb_get_args(mrb, "oooo", &cin, &b1, &b2, &b3);
    picoruby_ui_keys_receive(val_to_i(cin), val_to_i(b1), val_to_i(b2), val_to_i(b3));
    return mrb_fixnum_value(0);
}

static mrb_value
mrb_ui_keys_clear_lights(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_keys_clear_lights();
    return mrb_nil_value();
}

static mrb_value
mrb_ui_keys_is_lit(mrb_state *mrb, mrb_value self)
{
    mrb_value note;
    mrb_get_args(mrb, "o", &note);
    return mrb_bool_value(picoruby_ui_keys_is_lit(val_to_i(note)));
}

static mrb_value
mrb_ui_keys_is_pressed(mrb_state *mrb, mrb_value self)
{
    mrb_value note;
    mrb_get_args(mrb, "o", &note);
    return mrb_bool_value(picoruby_ui_keys_is_pressed(val_to_i(note)));
}

/* ------------------------------------------------------------------------
 * Sequencer
 *
 * The Ruby-visible object is UI::Sequencer (mrblib/ui_sequencer.rb).
 * Parameters go through _seq_set_i / _seq_get_i keyed by name; pattern and
 * line say which pattern / line a name applies to (see ui_common.h).
 * ------------------------------------------------------------------------ */

#define SEQ_ARRAY_MAX 64

/* Pairs of Integers from two Arrays of the same positions (b may be nil:
 * `dflt` for each). Positions where either is not an Integer are skipped. */
static int
collect_pairs(mrb_value a, mrb_value b, uint8_t *out_a, uint8_t *out_b, int cap, int dflt)
{
    if (!mrb_array_p(a)) return 0;
    int len = (int)RARRAY_LEN(a);
    if (len > cap) len = cap;
    int count = 0;
    for (int i = 0; i < len; i++) {
        mrb_value x = RARRAY_PTR(a)[i];
        mrb_value y = (mrb_array_p(b) && i < RARRAY_LEN(b)) ? RARRAY_PTR(b)[i] : mrb_fixnum_value(dflt);
        if (!mrb_integer_p(x) || !mrb_integer_p(y)) continue;
        mrb_int vx = mrb_integer(x), vy = mrb_integer(y);
        out_a[count] = (uint8_t)(vx < 0 ? 0 : (vx > 255 ? 255 : vx));
        out_b[count] = (uint8_t)(vy < 0 ? 0 : (vy > 255 ? 255 : vy));
        count++;
    }
    return count;
}

static mrb_value
pairs_to_ary(mrb_state *mrb, const uint8_t *a, const uint8_t *b, int n)
{
    mrb_value ary = mrb_ary_new_capa(mrb, n);
    for (int i = 0; i < n; i++) {
        mrb_value pair = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, pair, mrb_fixnum_value(a[i]));
        mrb_ary_push(mrb, pair, mrb_fixnum_value(b[i]));
        mrb_ary_push(mrb, ary, pair);
    }
    return ary;
}

/* UI._seq_reset(lines) */
static mrb_value
mrb_ui_seq_reset(mrb_state *mrb, mrb_value self)
{
    mrb_value lines;
    mrb_get_args(mrb, "o", &lines);
    picoruby_ui_seq_reset(val_to_i(lines));
    return mrb_nil_value();
}

/* UI._seq_set_i(pattern, line, name, value) -> true / false */
static mrb_value
mrb_ui_seq_set_i(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, name_v, value;
    mrb_get_args(mrb, "oooo", &pattern, &line, &name_v, &value);
    const char *name = param_name(mrb, name_v);
    int i;
    if (!name || !val_to_flag_i(value, &i)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_seq_set_i(val_to_i(pattern), val_to_i(line), name, i));
}

/* UI._seq_get_i(pattern, line, name) */
static mrb_value
mrb_ui_seq_get_i(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, name_v;
    mrb_get_args(mrb, "ooo", &pattern, &line, &name_v);
    const char *name = param_name(mrb, name_v);
    if (!name) return mrb_fixnum_value(0);
    return mrb_fixnum_value(picoruby_ui_seq_get_i(val_to_i(pattern), val_to_i(line), name));
}

/* UI._seq_set_name(line, name) -- line < 0: the song's */
static mrb_value
mrb_ui_seq_set_name(mrb_state *mrb, mrb_value self)
{
    mrb_value line, name;
    mrb_get_args(mrb, "oo", &line, &name);
    if (!mrb_string_p(name)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_seq_set_name(val_to_i(line), RSTRING_CSTR(mrb, name)));
}

/* UI._seq_name(line) -> String */
static mrb_value
mrb_ui_seq_name(mrb_state *mrb, mrb_value self)
{
    mrb_value line;
    mrb_get_args(mrb, "o", &line);
    char buf[32];
    int n = picoruby_ui_seq_get_name(val_to_i(line), buf, (int)sizeof(buf));
    return mrb_str_new(mrb, buf, n);
}

/* UI._seq_add_line -> index, or -1 */
static mrb_value
mrb_ui_seq_add_line(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_seq_add_line());
}

static mrb_value
mrb_ui_seq_remove_line(mrb_state *mrb, mrb_value self)
{
    mrb_value line;
    mrb_get_args(mrb, "o", &line);
    return mrb_bool_value(picoruby_ui_seq_remove_line(val_to_i(line)));
}

static mrb_value
mrb_ui_seq_clear_pattern(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern;
    mrb_get_args(mrb, "o", &pattern);
    return mrb_bool_value(picoruby_ui_seq_clear_pattern(val_to_i(pattern)));
}

static mrb_value
mrb_ui_seq_copy_pattern(mrb_state *mrb, mrb_value self)
{
    mrb_value src, dst;
    mrb_get_args(mrb, "oo", &src, &dst);
    return mrb_bool_value(picoruby_ui_seq_copy_pattern(val_to_i(src), val_to_i(dst)));
}

/* UI._seq_copy_line(src_pattern, src_line, dst_pattern, dst_line) */
static mrb_value
mrb_ui_seq_copy_line(mrb_state *mrb, mrb_value self)
{
    mrb_value sp, sl, dp, dl;
    mrb_get_args(mrb, "oooo", &sp, &sl, &dp, &dl);
    return mrb_bool_value(picoruby_ui_seq_copy_line(val_to_i(sp), val_to_i(sl),
                                                    val_to_i(dp), val_to_i(dl)));
}

/* UI._seq_place(pattern, line, step, len, notes, velocities) -> placed, or -1 */
static mrb_value
mrb_ui_seq_place(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, step, len, notes_v, vels_v;
    mrb_get_args(mrb, "oooooo", &pattern, &line, &step, &len, &notes_v, &vels_v);
    uint8_t notes[SEQ_ARRAY_MAX], vels[SEQ_ARRAY_MAX];
    int n = collect_pairs(notes_v, vels_v, notes, vels, SEQ_ARRAY_MAX, 100);
    return mrb_fixnum_value(picoruby_ui_seq_place(val_to_i(pattern), val_to_i(line), val_to_i(step),
                                                  val_to_i(len), notes, vels, n));
}

/* UI._seq_remove(pattern, line, step, note) -> removed (note < 0: all at step) */
static mrb_value
mrb_ui_seq_remove(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, step, note;
    mrb_get_args(mrb, "oooo", &pattern, &line, &step, &note);
    return mrb_fixnum_value(picoruby_ui_seq_remove(val_to_i(pattern), val_to_i(line),
                                                   val_to_i(step), val_to_i(note)));
}

static mrb_value
mrb_ui_seq_set_length(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, step, len;
    mrb_get_args(mrb, "oooo", &pattern, &line, &step, &len);
    return mrb_bool_value(picoruby_ui_seq_set_length(val_to_i(pattern), val_to_i(line),
                                                     val_to_i(step), val_to_i(len)));
}

/* UI._seq_add_event(pattern, line, pos, len, note, velocity) */
static mrb_value
mrb_ui_seq_add_event(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, pos, len, note, vel;
    mrb_get_args(mrb, "oooooo", &pattern, &line, &pos, &len, &note, &vel);
    return mrb_bool_value(picoruby_ui_seq_add_event(val_to_i(pattern), val_to_i(line), val_to_i(pos),
                                                    val_to_i(len), val_to_i(note), val_to_i(vel)));
}

/* UI._seq_clear(pattern, line, mode) -- line / mode < 0: all */
static mrb_value
mrb_ui_seq_clear(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, mode;
    mrb_get_args(mrb, "ooo", &pattern, &line, &mode);
    picoruby_ui_seq_clear(val_to_i(pattern), val_to_i(line), val_to_i(mode));
    return mrb_nil_value();
}

/* UI._seq_notes(pattern, line, mode) -> [[pos, len, note, velocity], ...] */
static mrb_value
mrb_ui_seq_notes(mrb_state *mrb, mrb_value self)
{
    mrb_value pattern, line, mode;
    mrb_get_args(mrb, "ooo", &pattern, &line, &mode);
    int p = val_to_i(pattern), l = val_to_i(line), m = val_to_i(mode);
    mrb_value ary = mrb_ary_new(mrb);
    int buf[32 * 4];
    int start = 0;
    for (;;) {
        int n = picoruby_ui_seq_notes_read(p, l, m, start, buf, 32);
        for (int i = 0; i < n; i++) {
            mrb_value nt = mrb_ary_new_capa(mrb, 4);
            for (int k = 0; k < 4; k++) mrb_ary_push(mrb, nt, mrb_fixnum_value(buf[i * 4 + k]));
            mrb_ary_push(mrb, ary, nt);
        }
        if (n < 32) break;
        start += n;
    }
    return ary;
}

/* UI._seq_chord -> [[note, velocity], ...] */
static mrb_value
mrb_ui_seq_chord(mrb_state *mrb, mrb_value self)
{
    uint8_t notes[SEQ_ARRAY_MAX], vels[SEQ_ARRAY_MAX];
    int n = picoruby_ui_seq_get_chord(notes, vels, SEQ_ARRAY_MAX);
    return pairs_to_ary(mrb, notes, vels, n);
}

/* UI._seq_set_chord(notes, velocities) */
static mrb_value
mrb_ui_seq_set_chord(mrb_state *mrb, mrb_value self)
{
    mrb_value notes_v, vels_v;
    mrb_get_args(mrb, "oo", &notes_v, &vels_v);
    uint8_t notes[SEQ_ARRAY_MAX], vels[SEQ_ARRAY_MAX];
    int n = collect_pairs(notes_v, vels_v, notes, vels, SEQ_ARRAY_MAX, 100);
    picoruby_ui_seq_set_chord(notes, vels, n);
    return mrb_nil_value();
}

/* UI._seq_chain -> [[pattern, repeat], ...] */
static mrb_value
mrb_ui_seq_chain(mrb_state *mrb, mrb_value self)
{
    uint8_t pats[SEQ_ARRAY_MAX], reps[SEQ_ARRAY_MAX];
    int n = picoruby_ui_seq_get_chain(pats, reps, SEQ_ARRAY_MAX);
    return pairs_to_ary(mrb, pats, reps, n);
}

/* UI._seq_set_chain(patterns, repeats) -> true / false */
static mrb_value
mrb_ui_seq_set_chain(mrb_state *mrb, mrb_value self)
{
    mrb_value pats_v, reps_v;
    mrb_get_args(mrb, "oo", &pats_v, &reps_v);
    uint8_t pats[SEQ_ARRAY_MAX], reps[SEQ_ARRAY_MAX];
    int n = collect_pairs(pats_v, reps_v, pats, reps, SEQ_ARRAY_MAX, 1);
    if (mrb_array_p(pats_v) && n != RARRAY_LEN(pats_v)) return mrb_false_value();
    return mrb_bool_value(picoruby_ui_seq_set_chain(pats, reps, n));
}

/* UI._seq_dump_begin(dirty_flag) -> true / false (out of memory) */
static mrb_value
mrb_ui_seq_dump_begin(mrb_state *mrb, mrb_value self)
{
    mrb_value dirty;
    mrb_get_args(mrb, "o", &dirty);
    return mrb_bool_value(picoruby_ui_seq_dump_begin(mrb_test(dirty)));
}

/* UI._seq_dump_next -> the next piece of the file (String), or nil when done */
static mrb_value
mrb_ui_seq_dump_next(mrb_state *mrb, mrb_value self)
{
    uint8_t *buf = (uint8_t *)mrb_malloc(mrb, PICORUBY_UI_SEQ_DUMP_MAX);
    int n = picoruby_ui_seq_dump_next(buf, PICORUBY_UI_SEQ_DUMP_MAX);
    mrb_value piece = n > 0 ? mrb_str_new(mrb, (const char *)buf, n) : mrb_nil_value();
    mrb_free(mrb, buf);
    return piece;
}

/* UI._seq_dump_end(saved, autosave) */
static mrb_value
mrb_ui_seq_dump_end(mrb_state *mrb, mrb_value self)
{
    mrb_value saved, autosave;
    mrb_get_args(mrb, "oo", &saved, &autosave);
    picoruby_ui_seq_dump_end(mrb_test(saved), mrb_test(autosave));
    return mrb_nil_value();
}

static mrb_value
mrb_ui_seq_load_begin(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_seq_load_begin());
}

/* UI._seq_load_feed(string) -> 0, or a negative error */
static mrb_value
mrb_ui_seq_load_feed(mrb_state *mrb, mrb_value self)
{
    mrb_value data;
    mrb_get_args(mrb, "o", &data);
    if (!mrb_string_p(data)) return mrb_fixnum_value(-3);
    return mrb_fixnum_value(picoruby_ui_seq_load_feed((const uint8_t *)RSTRING_PTR(data),
                                                      (int)RSTRING_LEN(data)));
}

/* UI._seq_load_commit(autosave) -> 0, or a negative error */
static mrb_value
mrb_ui_seq_load_commit(mrb_state *mrb, mrb_value self)
{
    mrb_value autosave;
    mrb_get_args(mrb, "o", &autosave);
    return mrb_fixnum_value(picoruby_ui_seq_load_commit(mrb_test(autosave)));
}

static mrb_value
mrb_ui_seq_load_abort(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_seq_load_abort();
    return mrb_nil_value();
}

/* UI._seq_load_error(code) -> String */
static mrb_value
mrb_ui_seq_load_error(mrb_state *mrb, mrb_value self)
{
    mrb_value code;
    mrb_get_args(mrb, "o", &code);
    return mrb_str_new_cstr(mrb, picoruby_ui_seq_load_error_name(val_to_i(code)));
}

/* UI._seq_take_request -> [op, name], or nil */
static mrb_value
mrb_ui_seq_take_request(mrb_state *mrb, mrb_value self)
{
    char name[32];
    int op = picoruby_ui_seq_take_request(name, (int)sizeof(name));
    if (op == 0) return mrb_nil_value();
    mrb_value pair = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, pair, mrb_fixnum_value(op));
    mrb_ary_push(mrb, pair, mrb_str_new_cstr(mrb, name));
    return pair;
}

/* UI._seq_set_files(newline-separated names) */
static mrb_value
mrb_ui_seq_set_files(mrb_state *mrb, mrb_value self)
{
    mrb_value names;
    mrb_get_args(mrb, "o", &names);
    if (mrb_string_p(names)) picoruby_ui_seq_set_files(RSTRING_CSTR(mrb, names));
    return mrb_nil_value();
}

/* UI._seq_status(message) */
static mrb_value
mrb_ui_seq_status(mrb_state *mrb, mrb_value self)
{
    mrb_value message;
    mrb_get_args(mrb, "o", &message);
    if (mrb_string_p(message)) picoruby_ui_seq_set_status(RSTRING_CSTR(mrb, message));
    return mrb_nil_value();
}

/* UI._seq_receive(cin, b1, b2, b3): one USB-MIDI packet sent to the sequencer */
static mrb_value
mrb_ui_seq_receive(mrb_state *mrb, mrb_value self)
{
    mrb_value cin, b1, b2, b3;
    mrb_get_args(mrb, "oooo", &cin, &b1, &b2, &b3);
    picoruby_ui_seq_receive(val_to_i(cin), val_to_i(b1), val_to_i(b2), val_to_i(b3));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------------
 * Gem initialization
 * ------------------------------------------------------------------------ */

#define DEF(name, func, aspec) \
    mrb_define_module_function_id(mrb, ui, MRB_SYM(name), func, aspec)

/* ------------------------------------------------------------------------
 * Tempo (docs/TEMPO.md)
 * ------------------------------------------------------------------------ */

/* UI._tempo_bit -> transport bit (0: none) */
static mrb_value
mrb_ui_tempo_bit(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_tempo_bit());
}

/* UI._tempo_set_running(bool): Start / Stop */
static mrb_value
mrb_ui_tempo_set_running(mrb_state *mrb, mrb_value self)
{
    mrb_value on;
    mrb_get_args(mrb, "o", &on);
    picoruby_ui_tempo_set_running(mrb_test(on));
    return mrb_nil_value();
}

/* UI._tempo_running -> bool */
static mrb_value
mrb_ui_tempo_running(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_tempo_running());
}

/* UI._tempo_tap */
static mrb_value
mrb_ui_tempo_tap(mrb_state *mrb, mrb_value self)
{
    picoruby_ui_tempo_tap();
    return mrb_nil_value();
}

/* UI._tempo_sync -> bool */
static mrb_value
mrb_ui_tempo_sync(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_tempo_sync());
}

/* UI._tempo_set_sync(bool) */
static mrb_value
mrb_ui_tempo_set_sync(mrb_state *mrb, mrb_value self)
{
    mrb_value on;
    mrb_get_args(mrb, "o", &on);
    picoruby_ui_tempo_set_sync(mrb_test(on));
    return mrb_nil_value();
}

/* ------------------------------------------------------------------------
 * MIDI Devices (docs/UI_MIDI_DEVICES.md)
 * ------------------------------------------------------------------------ */

/* UI._mididev_register(key, name, bit, color, builtin) -> index or -1 */
static mrb_value
mrb_ui_mididev_register(mrb_state *mrb, mrb_value self)
{
    mrb_value key, name, bit, color, builtin;
    mrb_get_args(mrb, "ooooo", &key, &name, &bit, &color, &builtin);
    return mrb_fixnum_value(picoruby_ui_mididev_register(str_arg(mrb, key), str_arg(mrb, name),
                                                         val_to_i(bit), val_to_i(color),
                                                         mrb_test(builtin)));
}

/* UI._mididev_unregister(bit) -> bool */
static mrb_value
mrb_ui_mididev_unregister(mrb_state *mrb, mrb_value self)
{
    mrb_value bit;
    mrb_get_args(mrb, "o", &bit);
    return mrb_bool_value(picoruby_ui_mididev_unregister(val_to_i(bit)));
}

/* UI._mididev_activate(key, bit) -> bool */
static mrb_value
mrb_ui_mididev_activate(mrb_state *mrb, mrb_value self)
{
    mrb_value key, bit;
    mrb_get_args(mrb, "oo", &key, &bit);
    return mrb_bool_value(picoruby_ui_mididev_activate(str_arg(mrb, key), val_to_i(bit)));
}

/* UI._mididev_count -> Integer */
static mrb_value
mrb_ui_mididev_count(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_mididev_count());
}

/* UI._mididev_get(index) -> [bit, flags, key, name] or nil */
static mrb_value
mrb_ui_mididev_get(mrb_state *mrb, mrb_value self)
{
    mrb_value index;
    mrb_get_args(mrb, "o", &index);
    int bit, flags;
    char key[16], name[24];
    if (!picoruby_ui_mididev_get(val_to_i(index), &bit, &flags, key, sizeof(key),
                                 name, sizeof(name))) {
        return mrb_nil_value();
    }
    mrb_value a = mrb_ary_new_capa(mrb, 4);
    mrb_ary_push(mrb, a, mrb_fixnum_value(bit));
    mrb_ary_push(mrb, a, mrb_fixnum_value(flags));
    mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, key));
    mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, name));
    return a;
}

/* UI._mididev_selected(out) -> index or -1 */
static mrb_value
mrb_ui_mididev_selected(mrb_state *mrb, mrb_value self)
{
    mrb_value out;
    mrb_get_args(mrb, "o", &out);
    return mrb_fixnum_value(picoruby_ui_mididev_selected(mrb_test(out)));
}

/* UI._mididev_select(out, index, save) */
static mrb_value
mrb_ui_mididev_select(mrb_state *mrb, mrb_value self)
{
    mrb_value out, index, save;
    mrb_get_args(mrb, "ooo", &out, &index, &save);
    picoruby_ui_mididev_select(mrb_test(out), val_to_i(index), mrb_test(save));
    return mrb_nil_value();
}

/* UI._mididev_restore -> patches routed */
static mrb_value
mrb_ui_mididev_restore(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_mididev_restore());
}

/* UI._mididev_save -> bool */
static mrb_value
mrb_ui_mididev_save(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(picoruby_ui_mididev_save());
}

/* UI._mididev_take_wanted -> key String or nil */
static mrb_value
mrb_ui_mididev_take_wanted(mrb_state *mrb, mrb_value self)
{
    char key[16];
    if (!picoruby_ui_mididev_take_wanted(key, sizeof(key))) return mrb_nil_value();
    return mrb_str_new_cstr(mrb, key);
}

/* UI._mididev_connected_mask -> Integer */
static mrb_value
mrb_ui_mididev_connected_mask(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_mididev_connected_mask());
}

/* UI._mididev_serial -> Integer */
static mrb_value
mrb_ui_mididev_serial(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_mididev_serial());
}

/* --- Named filters (docs/MIDI_FILTER_REGISTRY.md) --- */

/* UI._filter_define(name, label, params) -> bool */
static mrb_value
mrb_ui_filter_define(mrb_state *mrb, mrb_value self)
{
    mrb_value name, label, params;
    mrb_get_args(mrb, "ooo", &name, &label, &params);
    return mrb_bool_value(picoruby_ui_filter_define(str_arg(mrb, name), str_arg(mrb, label),
                                                    str_arg(mrb, params)));
}

/* UI._filter_undefine(name) -> bool */
static mrb_value
mrb_ui_filter_undefine(mrb_state *mrb, mrb_value self)
{
    mrb_value name;
    mrb_get_args(mrb, "o", &name);
    return mrb_bool_value(picoruby_ui_filter_undefine(str_arg(mrb, name)));
}

/* UI._filter_serial -> Integer */
static mrb_value
mrb_ui_filter_serial(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(picoruby_ui_filter_serial());
}

/* UI._filter_chains -> [[src, dst, rev, text], ...] (the live chains) */
static mrb_value
mrb_ui_filter_chains(mrb_state *mrb, mrb_value self)
{
    mrb_value list = mrb_ary_new(mrb);
    int n = picoruby_ui_filter_live_count();
    char *text = (char *)mrb_malloc(mrb, 512);
    for (int i = 0; i < n; i++) {
        int src, dst, rev;
        if (!picoruby_ui_filter_live_get(i, &src, &dst, &rev, text, 512)) continue;
        mrb_value e = mrb_ary_new_capa(mrb, 4);
        mrb_ary_push(mrb, e, mrb_fixnum_value(src));
        mrb_ary_push(mrb, e, mrb_fixnum_value(dst));
        mrb_ary_push(mrb, e, mrb_fixnum_value(rev));
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, text));
        mrb_ary_push(mrb, list, e);
    }
    mrb_free(mrb, text);
    return list;
}

/* UI._filter_chain_set(src, dst, text, save) -> revision (0: table full) */
static mrb_value
mrb_ui_filter_chain_set(mrb_state *mrb, mrb_value self)
{
    mrb_value src, dst, text, save;
    mrb_get_args(mrb, "oooo", &src, &dst, &text, &save);
    return mrb_fixnum_value(picoruby_ui_filter_chain_set(val_to_i(src), val_to_i(dst),
                                                         str_arg(mrb, text), mrb_test(save) ? 1 : 0));
}

void
mrb_picoruby_ui_gem_init(mrb_state *mrb)
{
    struct RClass *ui = mrb_define_module_id(mrb, MRB_SYM(UI));

    DEF(_pop_event,        mrb_ui_pop_event,        MRB_ARGS_NONE());
    DEF(_events_available, mrb_ui_events_available, MRB_ARGS_NONE());
    DEF(_bpm,              mrb_ui_bpm,              MRB_ARGS_NONE());
    DEF(_set_bpm,          mrb_ui_set_bpm,          MRB_ARGS_REQ(1));
    DEF(_log,              mrb_ui_log,              MRB_ARGS_REQ(1));
    DEF(_set_screen,       mrb_ui_set_screen,       MRB_ARGS_REQ(1));
    DEF(_current_screen,   mrb_ui_current_screen,   MRB_ARGS_NONE());
    DEF(_request_screen,   mrb_ui_request_screen,   MRB_ARGS_REQ(2));
    DEF(_nav,              mrb_ui_nav,              MRB_ARGS_REQ(1));
    DEF(_requested_screen, mrb_ui_requested_screen, MRB_ARGS_NONE());
    DEF(_menu_set_hidden,  mrb_ui_menu_set_hidden,  MRB_ARGS_REQ(1));
    DEF(_menu_hidden,      mrb_ui_menu_hidden,      MRB_ARGS_NONE());
    DEF(_loading,          mrb_ui_loading,          MRB_ARGS_REQ(3));

    /* Pads */
    DEF(_pad_set,          mrb_ui_pad_set,          MRB_ARGS_REQ(4));
    DEF(_pad_clear,        mrb_ui_pad_clear,        MRB_ARGS_REQ(1));
    DEF(_pad_clear_all,    mrb_ui_pad_clear_all,    MRB_ARGS_NONE());
    DEF(_pad_get_state,    mrb_ui_pad_get_state,    MRB_ARGS_REQ(1));
    DEF(_pad_set_label,    mrb_ui_pad_set_label,    MRB_ARGS_REQ(2));
    DEF(_pad_set_color,    mrb_ui_pad_set_color,    MRB_ARGS_REQ(2));

    /* Knobs */
    DEF(_knob_set,         mrb_ui_knob_set,         MRB_ARGS_REQ(11));
    DEF(_knob_clear,       mrb_ui_knob_clear,       MRB_ARGS_REQ(2));
    DEF(_knob_clear_all,   mrb_ui_knob_clear_all,   MRB_ARGS_NONE());
    DEF(_knob_value,       mrb_ui_knob_value,       MRB_ARGS_REQ(2));
    DEF(_knob_set_value,   mrb_ui_knob_set_value,   MRB_ARGS_REQ(3));
    DEF(_knob_reset,       mrb_ui_knob_reset,       MRB_ARGS_REQ(2));
    DEF(_knob_set_label,   mrb_ui_knob_set_label,   MRB_ARGS_REQ(3));
    DEF(_knob_set_color,   mrb_ui_knob_set_color,   MRB_ARGS_REQ(3));
    DEF(_knob_assigned,    mrb_ui_knob_assigned,    MRB_ARGS_REQ(2));
    DEF(_knob_count,       mrb_ui_knob_count,       MRB_ARGS_NONE());
    DEF(_knob_banks,       mrb_ui_knob_banks,       MRB_ARGS_NONE());
    DEF(_knob_get_bank,    mrb_ui_knob_get_bank,    MRB_ARGS_NONE());
    DEF(_knob_set_bank,    mrb_ui_knob_set_bank,    MRB_ARGS_REQ(1));

    /* Tombola */
    DEF(_tombola_set_f,       mrb_ui_tombola_set_f,       MRB_ARGS_REQ(2));
    DEF(_tombola_set_i,       mrb_ui_tombola_set_i,       MRB_ARGS_REQ(2));
    DEF(_tombola_get_f,       mrb_ui_tombola_get_f,       MRB_ARGS_REQ(1));
    DEF(_tombola_get_i,       mrb_ui_tombola_get_i,       MRB_ARGS_REQ(1));
    DEF(_tombola_set_scale,   mrb_ui_tombola_set_scale,   MRB_ARGS_REQ(1));
    DEF(_tombola_add_ball,    mrb_ui_tombola_add_ball,    MRB_ARGS_REQ(4));
    DEF(_tombola_remove_ball, mrb_ui_tombola_remove_ball, MRB_ARGS_REQ(1));
    DEF(_tombola_clear_balls, mrb_ui_tombola_clear_balls, MRB_ARGS_NONE());
    DEF(_tombola_ball_count,  mrb_ui_tombola_ball_count,  MRB_ARGS_NONE());
    DEF(_tombola_start,       mrb_ui_tombola_start,       MRB_ARGS_NONE());
    DEF(_tombola_stop,        mrb_ui_tombola_stop,        MRB_ARGS_NONE());
    DEF(_tombola_reset,       mrb_ui_tombola_reset,       MRB_ARGS_NONE());
    DEF(_tombola_running,     mrb_ui_tombola_running,     MRB_ARGS_NONE());

    /* XYPad */
    DEF(_xypad_reset,           mrb_ui_xypad_reset,           MRB_ARGS_NONE());
    DEF(_xypad_set_max_touches, mrb_ui_xypad_set_max_touches, MRB_ARGS_REQ(1));
    DEF(_xypad_get_max_touches, mrb_ui_xypad_get_max_touches, MRB_ARGS_NONE());
    DEF(_xypad_set_f,           mrb_ui_xypad_set_f,           MRB_ARGS_REQ(3));
    DEF(_xypad_set_i,           mrb_ui_xypad_set_i,           MRB_ARGS_REQ(3));
    DEF(_xypad_get_f,           mrb_ui_xypad_get_f,           MRB_ARGS_REQ(2));
    DEF(_xypad_get_i,           mrb_ui_xypad_get_i,           MRB_ARGS_REQ(2));
    DEF(_xypad_set_scale,       mrb_ui_xypad_set_scale,       MRB_ARGS_REQ(2));
    DEF(_xypad_get_scale,       mrb_ui_xypad_get_scale,       MRB_ARGS_REQ(1));

    /* Keys */
    DEF(_keys_reset,             mrb_ui_keys_reset,             MRB_ARGS_NONE());
    DEF(_keys_set_i,             mrb_ui_keys_set_i,             MRB_ARGS_REQ(2));
    DEF(_keys_get_i,             mrb_ui_keys_get_i,             MRB_ARGS_REQ(1));
    DEF(_keys_set_key_color,     mrb_ui_keys_set_key_color,     MRB_ARGS_REQ(2));
    DEF(_keys_key_color,         mrb_ui_keys_key_color,         MRB_ARGS_REQ(1));
    DEF(_keys_clear_key_colors,  mrb_ui_keys_clear_key_colors,  MRB_ARGS_NONE());
    DEF(_keys_set_channel_color, mrb_ui_keys_set_channel_color, MRB_ARGS_REQ(2));
    DEF(_keys_receive,           mrb_ui_keys_receive,           MRB_ARGS_REQ(4));
    DEF(_keys_clear_lights,      mrb_ui_keys_clear_lights,      MRB_ARGS_NONE());
    DEF(_keys_lit,               mrb_ui_keys_is_lit,            MRB_ARGS_REQ(1));
    DEF(_keys_pressed,           mrb_ui_keys_is_pressed,        MRB_ARGS_REQ(1));

    /* Sequencer */
    DEF(_seq_reset,          mrb_ui_seq_reset,          MRB_ARGS_REQ(1));
    DEF(_seq_set_i,          mrb_ui_seq_set_i,          MRB_ARGS_REQ(4));
    DEF(_seq_get_i,          mrb_ui_seq_get_i,          MRB_ARGS_REQ(3));
    DEF(_seq_set_name,       mrb_ui_seq_set_name,       MRB_ARGS_REQ(2));
    DEF(_seq_name,           mrb_ui_seq_name,           MRB_ARGS_REQ(1));
    DEF(_seq_add_line,       mrb_ui_seq_add_line,       MRB_ARGS_NONE());
    DEF(_seq_remove_line,    mrb_ui_seq_remove_line,    MRB_ARGS_REQ(1));
    DEF(_seq_clear_pattern,  mrb_ui_seq_clear_pattern,  MRB_ARGS_REQ(1));
    DEF(_seq_copy_pattern,   mrb_ui_seq_copy_pattern,   MRB_ARGS_REQ(2));
    DEF(_seq_copy_line,      mrb_ui_seq_copy_line,      MRB_ARGS_REQ(4));
    DEF(_seq_place,          mrb_ui_seq_place,          MRB_ARGS_REQ(6));
    DEF(_seq_remove,         mrb_ui_seq_remove,         MRB_ARGS_REQ(4));
    DEF(_seq_set_length,     mrb_ui_seq_set_length,     MRB_ARGS_REQ(4));
    DEF(_seq_add_event,      mrb_ui_seq_add_event,      MRB_ARGS_REQ(6));
    DEF(_seq_clear,          mrb_ui_seq_clear,          MRB_ARGS_REQ(3));
    DEF(_seq_notes,          mrb_ui_seq_notes,          MRB_ARGS_REQ(3));
    DEF(_seq_chord,          mrb_ui_seq_chord,          MRB_ARGS_NONE());
    DEF(_seq_set_chord,      mrb_ui_seq_set_chord,      MRB_ARGS_REQ(2));
    DEF(_seq_chain,          mrb_ui_seq_chain,          MRB_ARGS_NONE());
    DEF(_seq_set_chain,      mrb_ui_seq_set_chain,      MRB_ARGS_REQ(2));
    DEF(_seq_receive,        mrb_ui_seq_receive,        MRB_ARGS_REQ(4));
    DEF(_seq_dump_begin,     mrb_ui_seq_dump_begin,     MRB_ARGS_REQ(1));
    DEF(_seq_dump_next,      mrb_ui_seq_dump_next,      MRB_ARGS_NONE());
    DEF(_seq_dump_end,       mrb_ui_seq_dump_end,       MRB_ARGS_REQ(2));
    DEF(_seq_load_begin,     mrb_ui_seq_load_begin,     MRB_ARGS_NONE());
    DEF(_seq_load_feed,      mrb_ui_seq_load_feed,      MRB_ARGS_REQ(1));
    DEF(_seq_load_commit,    mrb_ui_seq_load_commit,    MRB_ARGS_REQ(1));
    DEF(_seq_load_abort,     mrb_ui_seq_load_abort,     MRB_ARGS_NONE());
    DEF(_seq_load_error,     mrb_ui_seq_load_error,     MRB_ARGS_REQ(1));
    DEF(_seq_take_request,   mrb_ui_seq_take_request,   MRB_ARGS_NONE());
    DEF(_seq_set_files,      mrb_ui_seq_set_files,      MRB_ARGS_REQ(1));
    DEF(_seq_status,         mrb_ui_seq_status,         MRB_ARGS_REQ(1));

    /* Tempo */
    DEF(_tempo_bit,              mrb_ui_tempo_bit,              MRB_ARGS_NONE());
    DEF(_tempo_set_running,      mrb_ui_tempo_set_running,      MRB_ARGS_REQ(1));
    DEF(_tempo_running,          mrb_ui_tempo_running,          MRB_ARGS_NONE());
    DEF(_tempo_tap,              mrb_ui_tempo_tap,              MRB_ARGS_NONE());
    DEF(_tempo_sync,             mrb_ui_tempo_sync,             MRB_ARGS_NONE());
    DEF(_tempo_set_sync,         mrb_ui_tempo_set_sync,         MRB_ARGS_REQ(1));

    /* MIDI Devices */
    DEF(_mididev_register,       mrb_ui_mididev_register,       MRB_ARGS_REQ(5));
    DEF(_mididev_unregister,     mrb_ui_mididev_unregister,     MRB_ARGS_REQ(1));
    DEF(_mididev_activate,       mrb_ui_mididev_activate,       MRB_ARGS_REQ(2));
    DEF(_mididev_count,          mrb_ui_mididev_count,          MRB_ARGS_NONE());
    DEF(_mididev_get,            mrb_ui_mididev_get,            MRB_ARGS_REQ(1));
    DEF(_mididev_selected,       mrb_ui_mididev_selected,       MRB_ARGS_REQ(1));
    DEF(_mididev_select,         mrb_ui_mididev_select,         MRB_ARGS_REQ(3));
    DEF(_mididev_restore,        mrb_ui_mididev_restore,        MRB_ARGS_NONE());
    DEF(_mididev_save,           mrb_ui_mididev_save,           MRB_ARGS_NONE());
    DEF(_mididev_take_wanted,    mrb_ui_mididev_take_wanted,    MRB_ARGS_NONE());
    DEF(_mididev_connected_mask, mrb_ui_mididev_connected_mask, MRB_ARGS_NONE());
    DEF(_mididev_serial,         mrb_ui_mididev_serial,         MRB_ARGS_NONE());
    DEF(_filter_define,          mrb_ui_filter_define,          MRB_ARGS_REQ(3));
    DEF(_filter_undefine,        mrb_ui_filter_undefine,        MRB_ARGS_REQ(1));
    DEF(_filter_serial,          mrb_ui_filter_serial,          MRB_ARGS_NONE());
    DEF(_filter_chains,          mrb_ui_filter_chains,          MRB_ARGS_NONE());
    DEF(_filter_chain_set,       mrb_ui_filter_chain_set,       MRB_ARGS_REQ(4));

    /* Layouts */
    DEF(_display,          mrb_ui_display,          MRB_ARGS_NONE());
    DEF(_layout_begin,     mrb_ui_layout_begin,     MRB_ARGS_REQ(2));
    DEF(_layout_add_pad,   mrb_ui_layout_add_pad,   MRB_ARGS_REQ(7));
    DEF(_layout_add_knob,  mrb_ui_layout_add_knob,  MRB_ARGS_REQ(13));
    DEF(_layout_add_envelope, mrb_ui_layout_add_envelope, MRB_ARGS_REQ(12));
    DEF(_layout_add_view,  mrb_ui_layout_add_view,  MRB_ARGS_REQ(5));
    DEF(_layout_commit,    mrb_ui_layout_commit,    MRB_ARGS_NONE());
    DEF(_layout_delete,    mrb_ui_layout_delete,    MRB_ARGS_REQ(1));
    DEF(_lw_pad_state,     mrb_ui_lw_pad_state,     MRB_ARGS_REQ(2));
    DEF(_lw_set_label,     mrb_ui_lw_set_label,     MRB_ARGS_REQ(3));
    DEF(_lw_set_color,     mrb_ui_lw_set_color,     MRB_ARGS_REQ(3));
    DEF(_lw_knob_value,    mrb_ui_lw_knob_value,    MRB_ARGS_REQ(2));
    DEF(_lw_knob_set_value, mrb_ui_lw_knob_set_value, MRB_ARGS_REQ(3));
    DEF(_lw_knob_reset,    mrb_ui_lw_knob_reset,    MRB_ARGS_REQ(2));
    DEF(_lw_env_values,    mrb_ui_lw_env_values,    MRB_ARGS_REQ(2));
    DEF(_lw_env_set,       mrb_ui_lw_env_set,       MRB_ARGS_REQ(6));
    DEF(_lw_env_reset,     mrb_ui_lw_env_reset,     MRB_ARGS_REQ(2));
}

void
mrb_picoruby_ui_gem_final(mrb_state *mrb)
{
}
