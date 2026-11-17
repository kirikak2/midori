// XYPad models and their picoruby-midi transports (docs/XYPAD.md). Drawing
// and touch geometry live in screen_xypad.cpp (XYPadView).
//
// Up to UI_XYPAD_MAX_PADS pads, each with up to UI_XYPAD_MAX_TOUCHES slots (one
// per finger) and a transport bit of its own. Pad 0 is the default pad; the
// others are opened by scripts (UI::XYPad.new) and closed by them or when
// they end. Nothing here draws or sends MIDI: Ruby makes the MIDI from the
// touch events (UI::XYPad), and hands each message back to ui_xypad_dispatch
// to go along the routes from the pad's bit.
//
// A pad's model is allocated the first time its id is used and never freed:
// the UI task may still be drawing (or letting go of) a pad while another
// task closes it, so a closed model stays readable and is reset when the id
// is opened again.

#include "ui_common.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

extern "C" {
#include "midi_transport.h"
#include "midi_route.h"
}

static const char* TAG = "UI_XYPAD";

static const uint8_t XYPAD_DEFAULT_SCALE[] = { 36, 38, 40, 41, 43, 45, 47, 48 };

struct XYPadModel {
    volatile bool alive;
    volatile midi_tmask_t bit;       // Transport registry bit, 0 if none
    volatile uint32_t gen;           // Bumped by reset / close: views drop their touches
    xypad_slot_t slots[UI_XYPAD_MAX_TOUCHES];
    uint8_t maxTouches;
};

static XYPadModel* s_pads[UI_XYPAD_MAX_PADS];
static bool s_claimed[UI_XYPAD_MAX_PADS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_shown = UI_XYPAD_DEFAULT;

// --- Transport ---------------------------------------------------------------

static int xypad_tx_send(void* ctx, uint8_t cable, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)ctx; (void)cable; (void)cin; (void)b1; (void)b2; (void)b3;
    return 0;   // Nothing is sent to a pad
}

static bool xypad_tx_connected(void* ctx)
{
    return ui_xypad_alive((int)(intptr_t)ctx);
}

static const midi_transport_ops_t s_xypad_ops = {
    .send_packet     = xypad_tx_send,
    .read_bytes      = nullptr,
    .bytes_available = nullptr,
    .is_connected    = xypad_tx_connected,
    .transport_id    = MIDI_TRANSPORT_ID_NONE,   // Not a wire transport
};

// One per pad: the registry keeps the pointer, ctx is the id
static midi_transport_t s_transports[UI_XYPAD_MAX_PADS];

// --- Model -------------------------------------------------------------------

static XYPadModel* get(int pad)
{
    if (pad < 0 || pad >= UI_XYPAD_MAX_PADS) return nullptr;
    XYPadModel* m = s_pads[pad];
    return (m && m->alive) ? m : nullptr;
}

// Even closed (a view may still read it), or nullptr if never used
static XYPadModel* raw(int pad)
{
    if (pad < 0 || pad >= UI_XYPAD_MAX_PADS) return nullptr;
    return s_pads[pad];
}

static void slot_defaults(xypad_slot_t* s, uint8_t index)
{
    memset(s, 0, sizeof(*s));
    s->x_mode = XYPAD_XMODE_NOTE;
    memcpy(s->scale, XYPAD_DEFAULT_SCALE, sizeof(XYPAD_DEFAULT_SCALE));
    s->scale_len = (uint8_t)sizeof(XYPAD_DEFAULT_SCALE);
    s->glide_range = 2.0f;
    s->x_min = 0.0f; s->x_max = 127.0f;
    s->y_min = 0.0f; s->y_max = 127.0f;
    s->y_invert = false;
    s->gate_note = 60;
    s->channel = index;   // channel_base + index is Ruby's job; C only needs a default
    s->hold = false;
    s->touch_id = -1;
}

static void model_defaults(XYPadModel* m)
{
    for (uint8_t i = 0; i < UI_XYPAD_MAX_TOUCHES; i++) slot_defaults(&m->slots[i], i);
    m->maxTouches = UI_XYPAD_MAX_TOUCHES;
}

// Opens pad `pad`, which the caller has claimed
static bool open_pad(int pad)
{
    XYPadModel* m = s_pads[pad];
    if (!m) {
        m = (XYPadModel*)calloc(1, sizeof(XYPadModel));
        if (!m) {
            ESP_LOGE(TAG, "out of memory for XYPad %d", pad);
            return false;
        }
        s_pads[pad] = m;
    }
    portENTER_CRITICAL(&s_mux);
    model_defaults(m);
    m->gen = m->gen + 1;
    portEXIT_CRITICAL(&s_mux);

    s_transports[pad].ops = &s_xypad_ops;
    s_transports[pad].ctx = (void*)(intptr_t)pad;
    midi_tmask_t bit = MIDI_transport_register(&s_transports[pad]);
    if (bit == 0) {
        ESP_LOGW(TAG, "MIDI transport registry full: XYPad %d is no transport", pad);
    } else {
        MIDI_route_set_source(bit, true);
    }
    m->bit = bit;
    m->alive = true;
    return true;
}

static xypad_slot_t* slot_at(int pad, uint8_t index)
{
    XYPadModel* m = get(pad);
    if (!m || index >= UI_XYPAD_MAX_TOUCHES) return nullptr;
    return &m->slots[index];
}

extern "C" {

// --- Pads --------------------------------------------------------------------

int ui_xypad_default(void)
{
    if (get(UI_XYPAD_DEFAULT)) return UI_XYPAD_DEFAULT;
    bool mine = false;
    portENTER_CRITICAL(&s_mux);
    if (!s_claimed[UI_XYPAD_DEFAULT]) {
        s_claimed[UI_XYPAD_DEFAULT] = true;
        mine = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (mine && !open_pad(UI_XYPAD_DEFAULT)) {
        portENTER_CRITICAL(&s_mux);
        s_claimed[UI_XYPAD_DEFAULT] = false;
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }
    return UI_XYPAD_DEFAULT;
}

int ui_xypad_create(void)
{
    int pad = -1;
    portENTER_CRITICAL(&s_mux);
    for (int i = UI_XYPAD_DEFAULT + 1; i < UI_XYPAD_MAX_PADS; i++) {
        if (!s_claimed[i]) {
            s_claimed[i] = true;
            pad = i;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    if (pad < 0) {
        ESP_LOGW(TAG, "no free XYPad (max %d)", UI_XYPAD_MAX_PADS);
        return -1;
    }
    if (!open_pad(pad)) {
        portENTER_CRITICAL(&s_mux);
        s_claimed[pad] = false;
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }
    return pad;
}

void ui_xypad_destroy(int pad)
{
    if (pad == UI_XYPAD_DEFAULT) {
        ui_xypad_reset(pad);
        return;
    }
    XYPadModel* m = get(pad);
    if (!m) return;

    midi_tmask_t bit;
    portENTER_CRITICAL(&s_mux);
    m->alive = false;
    bit = m->bit;
    m->bit = 0;
    m->gen = m->gen + 1;
    for (int i = 0; i < UI_XYPAD_MAX_TOUCHES; i++) {
        m->slots[i].active = false;
        m->slots[i].latched = false;
        m->slots[i].touch_id = -1;
    }
    if (s_shown == pad) s_shown = UI_XYPAD_DEFAULT;
    portEXIT_CRITICAL(&s_mux);

    if (bit) {
        MIDI_route_set_source(bit, false);
        MIDI_transport_unregister(bit);
    }
    portENTER_CRITICAL(&s_mux);
    s_claimed[pad] = false;
    portEXIT_CRITICAL(&s_mux);
}

bool ui_xypad_alive(int pad)
{
    return get(pad) != nullptr;
}

uint32_t ui_xypad_generation(int pad)
{
    XYPadModel* m = raw(pad);
    return m ? m->gen : 0;
}

void ui_xypad_set_shown(int pad)
{
    if (get(pad)) s_shown = pad;
}

int ui_xypad_shown(void)
{
    int pad = s_shown;
    if (get(pad)) return pad;
    return ui_xypad_default();
}

void ui_xypad_cleanup(void)
{
    // Latched notes are silenced by the All Notes Off sweep of the same stop
    // path (nothing here sends MIDI)
    for (int pad = UI_XYPAD_DEFAULT + 1; pad < UI_XYPAD_MAX_PADS; pad++) ui_xypad_destroy(pad);
    ui_xypad_reset(UI_XYPAD_DEFAULT);
    s_shown = UI_XYPAD_DEFAULT;
}

// --- Configuration -----------------------------------------------------------

void ui_xypad_reset(int pad)
{
    XYPadModel* m = get(pad);
    if (!m) return;
    portENTER_CRITICAL(&s_mux);
    model_defaults(m);
    m->gen = m->gen + 1;
    portEXIT_CRITICAL(&s_mux);
}

void ui_xypad_set_max_touches(int pad, uint8_t n)
{
    XYPadModel* m = get(pad);
    if (!m) return;
    if (n < 1) n = 1;
    if (n > UI_XYPAD_MAX_TOUCHES) n = UI_XYPAD_MAX_TOUCHES;
    portENTER_CRITICAL(&s_mux);
    m->maxTouches = n;
    portEXIT_CRITICAL(&s_mux);
}

uint8_t ui_xypad_get_max_touches(int pad)
{
    XYPadModel* m = get(pad);
    return m ? m->maxTouches : UI_XYPAD_MAX_TOUCHES;
}

bool ui_xypad_set_f(int pad, uint8_t index, const char* name, float value)
{
    xypad_slot_t* s = slot_at(pad, index);
    if (!s || !name) return false;

    portENTER_CRITICAL(&s_mux);
    bool ok = true;
    if (!strcmp(name, "glide_range")) {
        s->glide_range = (value > 0.0f) ? value : 0.01f;
    } else if (!strcmp(name, "x_min")) {
        s->x_min = value;
        if (s->x_max <= s->x_min) s->x_max = s->x_min + 1.0f;
    } else if (!strcmp(name, "x_max")) {
        s->x_max = value;
        if (s->x_max <= s->x_min) s->x_max = s->x_min + 1.0f;
    } else if (!strcmp(name, "y_min")) {
        s->y_min = value;
        if (s->y_max <= s->y_min) s->y_max = s->y_min + 1.0f;
    } else if (!strcmp(name, "y_max")) {
        s->y_max = value;
        if (s->y_max <= s->y_min) s->y_max = s->y_min + 1.0f;
    } else {
        ok = false;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

bool ui_xypad_set_i(int pad, uint8_t index, const char* name, int value)
{
    xypad_slot_t* s = slot_at(pad, index);
    if (!s || !name) return false;

    // Hold needs special handling: a slot latched by Hold has nobody left to
    // release it once the script turns hold off, so that transition has to
    // force the release right here. Ruby broadcasts (pad.hold = false) call
    // this once per slot, which is what makes "release every latched slot at
    // once" fall out of a per-slot rule rather than needing a separate
    // all-slots API.
    if (!strcmp(name, "hold")) {
        bool hold = (value != 0);
        portENTER_CRITICAL(&s_mux);
        bool was_hold = s->hold;
        s->hold = hold;
        bool release = (!hold && was_hold && s->latched && !s->active);
        if (release) s->latched = false;
        uint8_t channel = s->channel;
        uint8_t note = s->note;
        float   bend = s->bend_semitones;
        float   xv = s->x;
        float   yv = s->y;
        portEXIT_CRITICAL(&s_mux);
        if (release) {
            ui_xypad_event_post((uint8_t)pad, index, XYPAD_PHASE_UP, channel, note, bend, xv, yv);
        }
        return true;
    }

    portENTER_CRITICAL(&s_mux);
    bool ok = true;
    if (!strcmp(name, "x_mode")) {
        s->x_mode = (value == XYPAD_XMODE_CC) ? XYPAD_XMODE_CC : XYPAD_XMODE_NOTE;
    } else if (!strcmp(name, "y_invert")) {
        s->y_invert = (value != 0);
    } else if (!strcmp(name, "gate_note")) {
        s->gate_note = (uint8_t)value;
    } else if (!strcmp(name, "channel")) {
        s->channel = (uint8_t)(value & 0x0F);
    } else {
        ok = false;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

float ui_xypad_get_f(int pad, uint8_t index, const char* name)
{
    const xypad_slot_t* s = slot_at(pad, index);
    if (!s || !name) return 0.0f;
    if (!strcmp(name, "glide_range")) return s->glide_range;
    if (!strcmp(name, "x_min"))       return s->x_min;
    if (!strcmp(name, "x_max"))       return s->x_max;
    if (!strcmp(name, "y_min"))       return s->y_min;
    if (!strcmp(name, "y_max"))       return s->y_max;
    // Read-only: the glide_range that was actually in effect for the touch
    // currently occupying this slot (frozen at touchdown), which is what a
    // bend_semitones value from an in-progress touch has to be divided by to
    // recover the raw +-8192 pitch bend unit. Using the live glide_range
    // instead would be wrong the moment a script changes it mid-drag.
    if (!strcmp(name, "touch_glide_range")) return s->touch_glide_range;
    return 0.0f;
}

int ui_xypad_get_i(int pad, uint8_t index, const char* name)
{
    const xypad_slot_t* s = slot_at(pad, index);
    if (!s || !name) return 0;
    if (!strcmp(name, "x_mode"))    return s->x_mode;
    if (!strcmp(name, "y_invert")) return s->y_invert ? 1 : 0;
    if (!strcmp(name, "gate_note")) return s->gate_note;
    if (!strcmp(name, "channel"))  return s->channel;
    if (!strcmp(name, "hold"))     return s->hold ? 1 : 0;
    return 0;
}

void ui_xypad_set_scale(int pad, uint8_t index, const uint8_t* notes, uint8_t len)
{
    xypad_slot_t* s = slot_at(pad, index);
    if (!s) return;
    if (len > UI_XYPAD_MAX_SCALE) len = UI_XYPAD_MAX_SCALE;

    portENTER_CRITICAL(&s_mux);
    if (notes && len > 0) {
        memcpy(s->scale, notes, len);
    }
    s->scale_len = len;
    portEXIT_CRITICAL(&s_mux);
}

uint8_t ui_xypad_get_scale(int pad, uint8_t index, uint8_t* out, uint8_t max_len)
{
    const xypad_slot_t* s = slot_at(pad, index);
    if (!s || !out) return 0;
    uint8_t n = s->scale_len;
    if (n > max_len) n = max_len;
    memcpy(out, s->scale, n);
    return n;
}

// Closed pads still answer: a view may be fading out the last touches
const xypad_slot_t* ui_xypad_get_slot(int pad, uint8_t index)
{
    XYPadModel* m = raw(pad);
    if (!m || index >= UI_XYPAD_MAX_TOUCHES) return nullptr;
    return &m->slots[index];
}

// --- Transport ---------------------------------------------------------------

midi_tmask_t ui_xypad_transport_bit(int pad)
{
    XYPadModel* m = get(pad);
    return m ? m->bit : 0;
}

midi_tmask_t ui_xypad_targets(int pad, uint8_t cin, uint8_t b1)
{
    midi_tmask_t bit = ui_xypad_transport_bit(pad);
    if (!bit) return 0;
    return MIDI_route_targets(bit, cin, b1) & (midi_tmask_t)~bit;
}

void ui_xypad_dispatch(int pad, midi_tmask_t route, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    midi_tmask_t bit = ui_xypad_transport_bit(pad);
    if (!bit) return;
    MIDI_route_dispatch(bit, route & (midi_tmask_t)~bit, cin, b1, b2, b3);
}

// --- Touches -----------------------------------------------------------------

static int find_free_slot(XYPadModel* m)
{
    for (uint8_t i = 0; i < m->maxTouches; i++) {
        if (!m->slots[i].active && !m->slots[i].latched) return (int)i;
    }
    return -1;
}

static int find_slot_by_touch(XYPadModel* m, int touch_id)
{
    for (uint8_t i = 0; i < UI_XYPAD_MAX_TOUCHES; i++) {
        if (m->slots[i].active && m->slots[i].touch_id == touch_id) return (int)i;
    }
    return -1;
}

static uint8_t snap_note(const xypad_slot_t* s, int x, int content_x, int content_w)
{
    if (s->scale_len == 0 || content_w <= 0) return s->gate_note;
    int rel = x - content_x;
    if (rel < 0) rel = 0;
    if (rel >= content_w) rel = content_w - 1;
    int cell = (rel * s->scale_len) / content_w;
    if (cell >= s->scale_len) cell = s->scale_len - 1;
    return s->scale[cell];
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float map_x(const xypad_slot_t* s, int x, int content_x, int content_w)
{
    if (content_w <= 0) return s->x_min;
    float t = (float)(x - content_x) / (float)content_w;
    t = clampf(t, 0.0f, 1.0f);
    return s->x_min + (s->x_max - s->x_min) * t;
}

// Screen y grows downward; the top reads as the larger value by default
// (matches how effects hardware usually lays these pads out), y_invert flips it.
static float map_y(const xypad_slot_t* s, int y, int content_y, int content_h)
{
    if (content_h <= 0) return s->y_min;
    float t = (float)(y - content_y) / (float)content_h;
    t = clampf(t, 0.0f, 1.0f);
    float t2 = s->y_invert ? t : (1.0f - t);
    return s->y_min + (s->y_max - s->y_min) * t2;
}

int ui_xypad_touch_down(int pad, int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    XYPadModel* m = get(pad);
    if (!m) return -1;
    portENTER_CRITICAL(&s_mux);

    int idx = find_free_slot(m);
    if (idx < 0) {
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }

    xypad_slot_t* s = &m->slots[idx];
    s->active = true;
    s->latched = false;
    s->touch_id = touch_id;
    s->touch_x_mode = s->x_mode;
    s->touch_glide_range = s->glide_range;
    s->touchdown_x = (int16_t)x;

    if (s->touch_x_mode == XYPAD_XMODE_NOTE) {
        s->note = snap_note(s, x, content_x, content_w);
        s->bend_semitones = 0.0f;
        s->x = 0.0f;
    } else {
        s->note = s->gate_note;
        s->x = map_x(s, x, content_x, content_w);
        s->bend_semitones = 0.0f;
    }
    s->y = map_y(s, y, content_y, content_h);

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_mux);

    ui_xypad_event_post((uint8_t)pad, (uint8_t)idx, XYPAD_PHASE_DOWN, channel, note, bend, xv, yv);
    return idx;
}

int ui_xypad_touch_move(int pad, int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    XYPadModel* m = get(pad);
    if (!m) return -1;
    portENTER_CRITICAL(&s_mux);

    int idx = find_slot_by_touch(m, touch_id);
    if (idx < 0) {
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }
    xypad_slot_t* s = &m->slots[idx];

    if (s->touch_x_mode == XYPAD_XMODE_NOTE) {
        float range = s->touch_glide_range;
        float dx = (float)(x - s->touchdown_x);
        float bend = (content_w > 0) ? (dx / (float)content_w) * range : 0.0f;
        s->bend_semitones = clampf(bend, -range, range);
    } else {
        s->x = map_x(s, x, content_x, content_w);
    }
    s->y = map_y(s, y, content_y, content_h);

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_mux);

    ui_xypad_event_post((uint8_t)pad, (uint8_t)idx, XYPAD_PHASE_MOVE, channel, note, bend, xv, yv);
    return idx;
}

int ui_xypad_touch_up(int pad, int touch_id)
{
    XYPadModel* m = get(pad);
    if (!m) return -1;
    portENTER_CRITICAL(&s_mux);

    int idx = find_slot_by_touch(m, touch_id);
    if (idx < 0) {
        portEXIT_CRITICAL(&s_mux);
        return -1;
    }
    xypad_slot_t* s = &m->slots[idx];

    bool hold = s->hold;
    s->active = false;
    s->touch_id = -1;
    if (hold) s->latched = true;

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_mux);

    ui_xypad_event_post((uint8_t)pad, (uint8_t)idx, XYPAD_PHASE_UP, channel, note, bend, xv, yv);
    return idx;
}

}  // extern "C"
