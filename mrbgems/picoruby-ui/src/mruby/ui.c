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
        case PICORUBY_UI_EVENT_SCREEN_CHANGE:
            HSET(screen, mrb_fixnum_value(event.screen));
            break;
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
 * Gem initialization
 * ------------------------------------------------------------------------ */

#define DEF(name, func, aspec) \
    mrb_define_module_function_id(mrb, ui, MRB_SYM(name), func, aspec)

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
}

void
mrb_picoruby_ui_gem_final(mrb_state *mrb)
{
}
