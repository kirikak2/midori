// On-screen keyboard models and their picoruby-midi transports (docs/KEYS.md).
// Drawing and touch geometry live in screen_keys.cpp (KeysView).
//
// Up to UI_KEYS_MAX keyboards, each in a slot: a model, a transport bit and
// the dirty bits the view showing it repaints from. Slot 0 is the default
// keyboard; the others are opened by scripts (UI::Keys.new) and closed by
// them or when they end. A slot's model is allocated the first time the slot
// is used and never freed: the UI task may still be drawing (or letting go
// of the fingers on) a keyboard while another task closes it, so a closed
// model stays readable and is only reset when the slot is opened again.

#include "ui_keys.h"
#include "ui_manager.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

extern "C" {
#include "midi_transport.h"
#include "midi_route.h"
}

static const char* TAG = "UI_KEYS";

static constexpr int DEFAULT_OCTAVES = 2;
static constexpr int DEFAULT_LOW = 48;          // C3
static constexpr int DEFAULT_VELOCITY = 100;
static constexpr uint16_t DEFAULT_WHITE = UI_COLOR_WHITE;
static constexpr uint16_t DEFAULT_BLACK = UI_COLOR_BLACK;
static constexpr uint16_t DEFAULT_PRESS = UI_COLOR_CYAN;
static constexpr uint16_t DEFAULT_LIGHT = UI_COLOR_ORANGE;

struct KeysModel {
    // --- Slot (under s_mux) ---
    volatile bool alive;             // Open: settable, playable, a transport
    volatile midi_tmask_t bit;       // Transport registry bit, 0 if none
    volatile uint32_t gen;           // Bumped by reset / close: views let go of the fingers
    volatile uint32_t dirty[4];
    volatile bool dirtyAll;

    // --- Configuration (any task) ---
    volatile uint8_t octaves;
    volatile uint8_t low;
    volatile uint8_t channel;
    volatile uint8_t velocity;
    volatile uint8_t velocityMode;   // keys_velocity_mode_t
    volatile bool    slide;          // A dragged finger moves on to the next key
    volatile bool    labels;         // Note names on the C keys
    volatile bool    notify;         // Queue key events for Ruby (a block is attached)
    volatile midi_tmask_t outMask;   // Destinations of its own, besides the routes
    volatile uint16_t white, black, press, light;

    uint16_t keyColor[128];
    volatile uint32_t keyColorSet[4];
    uint16_t channelColor[16];
    volatile uint16_t channelColorSet;

    // --- Lights: one bit per channel that has the note on (any task) ---
    volatile uint16_t lit[128];

    // --- Fingers (UI task) ---
    uint8_t holdCount[128];          // Fingers on each note
    midi_tmask_t heldMask[128];      // Where its note on went (device:), for the note off
    midi_tmask_t heldRoute[128];     // ... and which MIDI.route destinations
    midi_tmask_t heldBit[128];       // ... from which bit (it may be closed by the note off)
    uint8_t heldChannel[128];
    int16_t touchNote[MAX_TOUCH_POINTS];   // -1: over no key
    bool    touchActive[MAX_TOUCH_POINTS];
};

static KeysModel* s_models[UI_KEYS_MAX];
static bool s_claimed[UI_KEYS_MAX];          // Being opened, or open (under s_mux)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_shown = UI_KEYS_DEFAULT;

// --- Transport ---------------------------------------------------------------

static int keys_tx_send_packet(void* ctx, uint8_t cable, uint8_t cin,
                               uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)cable;
    ui_keys_receive((int)(intptr_t)ctx, cin, b1, b2, b3);
    return 0;
}

static bool keys_tx_is_connected(void* ctx)
{
    return ui_keys_alive((int)(intptr_t)ctx);
}

static const midi_transport_ops_t s_transport_ops = {
    .send_packet     = keys_tx_send_packet,
    .read_bytes      = nullptr,     // Its output is pushed, not read (see touch_*)
    .bytes_available = nullptr,
    .is_connected    = keys_tx_is_connected,
    .transport_id    = MIDI_TRANSPORT_ID_NONE,   // Not a wire transport
};

// One per slot: the registry keeps the pointer, ctx is the id
static midi_transport_t s_transports[UI_KEYS_MAX];

// --- Model -------------------------------------------------------------------

// Open model, or nullptr
static KeysModel* get(int id)
{
    if (id < 0 || id >= UI_KEYS_MAX) return nullptr;
    KeysModel* m = s_models[id];
    return (m && m->alive) ? m : nullptr;
}

// The slot's model even when closed (the fingers' notes still need their
// note offs), or nullptr if the slot was never used
static KeysModel* raw(int id)
{
    if (id < 0 || id >= UI_KEYS_MAX) return nullptr;
    return s_models[id];
}

static void mark_dirty(KeysModel* m, int note)
{
    if (note < 0 || note > 127) return;
    portENTER_CRITICAL_SAFE(&s_mux);
    m->dirty[note >> 5] |= 1u << (note & 31);
    portEXIT_CRITICAL_SAFE(&s_mux);
}

static void mark_all_dirty(KeysModel* m)
{
    m->dirtyAll = true;
}

static void set_defaults(KeysModel* m)
{
    m->octaves = DEFAULT_OCTAVES;
    m->low = DEFAULT_LOW;
    m->channel = 0;
    m->velocity = DEFAULT_VELOCITY;
    m->velocityMode = KEYS_VELOCITY_FIXED;
    m->slide = true;
    m->labels = true;
    m->notify = false;
    m->outMask = 0;
    m->white = DEFAULT_WHITE;
    m->black = DEFAULT_BLACK;
    m->press = DEFAULT_PRESS;
    m->light = DEFAULT_LIGHT;
    for (int i = 0; i < 4; i++) m->keyColorSet[i] = 0;
    m->channelColorSet = 0;
    for (int n = 0; n < 128; n++) m->lit[n] = 0;
}

// Opens slot `id`, which the caller has claimed: a fresh model (allocated
// the first time), and a transport bit that is a route source
static bool open_slot(int id)
{
    KeysModel* m = s_models[id];
    if (!m) {
        m = (KeysModel*)calloc(1, sizeof(KeysModel));
        if (!m) {
            ESP_LOGE(TAG, "out of memory for keyboard %d", id);
            return false;
        }
        s_models[id] = m;
    }
    // Whatever the view still held on the previous keyboard in this slot went
    // off when it was closed (gen); start with no fingers
    memset(m->holdCount, 0, sizeof(m->holdCount));
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m->touchNote[i] = -1;
        m->touchActive[i] = false;
    }
    set_defaults(m);
    m->gen = m->gen + 1;
    m->dirtyAll = true;

    s_transports[id].ops = &s_transport_ops;
    s_transports[id].ctx = (void*)(intptr_t)id;
    midi_tmask_t bit = MIDI_transport_register(&s_transports[id]);
    if (bit == 0) {
        ESP_LOGW(TAG, "MIDI transport registry full: keyboard %d is no transport", id);
    } else {
        MIDI_route_set_source(bit, true);
    }
    m->bit = bit;
    m->alive = true;
    return true;
}

// White key at or below note
static int white_floor(int note)
{
    while (note > 0 && ui_keys::is_black(note)) note--;
    return note;
}

static void clamp_range(KeysModel* m)
{
    int oct = m->octaves;
    if (oct < UI_KEYS_MIN_OCTAVES) oct = UI_KEYS_MIN_OCTAVES;
    if (oct > UI_KEYS_MAX_OCTAVES) oct = UI_KEYS_MAX_OCTAVES;
    int low = m->low;
    if (low + oct * 12 > 127) low = 127 - oct * 12;
    if (low < 0) low = 0;
    m->octaves = (uint8_t)oct;
    m->low = (uint8_t)white_floor(low);
}

static uint8_t clamp7(int v)
{
    if (v < 0) return 0;
    if (v > 127) return 127;
    return (uint8_t)v;
}

// --- Fingers -----------------------------------------------------------------

static void post_event(int id, int note, int velocity, int channel, bool on)
{
    ui_event_t event;
    event.type = UI_EVENT_KEY;
    event.data.key.note = (uint8_t)note;
    event.data.key.velocity = (uint8_t)velocity;
    event.data.key.channel = (uint8_t)channel;
    event.data.key.on = on;
    event.data.key.keys = (uint8_t)id;
    ui_event_push(&event);
}

static void key_on(int id, KeysModel* m, int note, int velocity)
{
    if (note < 0 || note > 127) return;
    if (m->holdCount[note]++ > 0) return;   // Another finger already sounds it

    uint8_t ch = m->channel & 0x0F;
    uint8_t status = 0x90 | ch;
    uint8_t vel = velocity < 1 ? 1 : clamp7(velocity);
    midi_tmask_t bit = m->bit;
    // Route destinations go through MIDI_route_dispatch so their filters
    // (MIDI.filter) and listeners (MIDI.on) see the key; device: is direct.
    midi_tmask_t route = 0;
    if (bit) route = MIDI_route_targets(bit, 0x9, status) & (midi_tmask_t)~bit;  // Never back into itself
    midi_tmask_t mask = m->outMask & (midi_tmask_t)~route & (midi_tmask_t)~bit;
    m->heldMask[note] = mask;
    m->heldRoute[note] = route;
    m->heldBit[note] = bit;
    m->heldChannel[note] = ch;
    if (mask) MIDI_transport_send(mask, 0, 0x9, status, (uint8_t)note, vel);
    if (bit) MIDI_route_dispatch(bit, route, 0x9, status, (uint8_t)note, vel);

    mark_dirty(m, note);
    if (m->notify) post_event(id, note, vel, ch, true);
}

static void key_off(int id, KeysModel* m, int note)
{
    if (note < 0 || note > 127 || m->holdCount[note] == 0) return;
    if (--m->holdCount[note] > 0) return;

    uint8_t ch = m->heldChannel[note];
    midi_tmask_t mask = m->heldMask[note];
    midi_tmask_t bit = m->heldBit[note];
    if (mask) MIDI_transport_send(mask, 0, 0x8, 0x80 | ch, (uint8_t)note, 0);
    if (bit && m->heldRoute[note]) {
        MIDI_route_dispatch(bit, m->heldRoute[note], 0x8, 0x80 | ch, (uint8_t)note, 0);
    }

    mark_dirty(m, note);
    if (m->notify && m->alive) post_event(id, note, 0, ch, false);
}

namespace ui_keys {

int low(int id)      { KeysModel* m = get(id); return m ? m->low : DEFAULT_LOW; }
int octaves(int id)  { KeysModel* m = get(id); return m ? m->octaves : DEFAULT_OCTAVES; }
int top(int id)      { return low(id) + 12 * octaves(id); }
bool labels(int id)  { KeysModel* m = get(id); return m ? m->labels : true; }
int velocity(int id) { KeysModel* m = get(id); return m ? m->velocity : DEFAULT_VELOCITY; }

bool velocity_by_position(int id)
{
    KeysModel* m = get(id);
    return m && m->velocityMode == KEYS_VELOCITY_POSITION;
}

uint16_t key_fill(int id, int note)
{
    KeysModel* m = get(id);
    bool black = is_black(note);
    if (!m) return black ? DEFAULT_BLACK : DEFAULT_WHITE;
    if (note < 0 || note > 127) return black ? m->black : m->white;

    if (m->holdCount[note]) return m->press;

    uint16_t lit = m->lit[note];
    if (lit) {
        // The lowest lit channel decides, so the colour does not flicker
        // between channels sounding the same note.
        for (int ch = 0; ch < 16; ch++) {
            if (!(lit & (1u << ch))) continue;
            return (m->channelColorSet & (1u << ch)) ? m->channelColor[ch] : m->light;
        }
    }
    if (m->keyColorSet[note >> 5] & (1u << (note & 31))) return m->keyColor[note];
    return black ? m->black : m->white;
}

uint32_t generation(int id)
{
    KeysModel* m = raw(id);
    return m ? m->gen : 0;
}

void take_dirty(int id, uint32_t out[4], bool& full)
{
    KeysModel* m = raw(id);
    if (!m) {
        for (int i = 0; i < 4; i++) out[i] = 0;
        full = true;
        return;
    }
    portENTER_CRITICAL_SAFE(&s_mux);
    for (int i = 0; i < 4; i++) {
        out[i] = m->dirty[i];
        m->dirty[i] = 0;
    }
    full = m->dirtyAll;
    m->dirtyAll = false;
    portEXIT_CRITICAL_SAFE(&s_mux);
}

void touch_down(int id, int touchId, int note, int velocity)
{
    KeysModel* m = get(id);
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (m->touchActive[touchId]) touch_up(id, touchId);
    m->touchActive[touchId] = true;
    m->touchNote[touchId] = (int16_t)note;
    if (note >= 0) key_on(id, m, note, velocity);
}

void touch_move(int id, int touchId, int note, int velocity)
{
    KeysModel* m = get(id);
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (!m->touchActive[touchId]) return;
    int current = m->touchNote[touchId];
    if (note == current) return;
    // Without slide a finger keeps the key it landed on, wherever it goes
    if (!m->slide && current >= 0) return;

    if (current >= 0) key_off(id, m, current);
    m->touchNote[touchId] = (int16_t)note;
    if (note >= 0) key_on(id, m, note, velocity);
}

// Also on a closed keyboard: the note off goes where the note on went
void touch_up(int id, int touchId)
{
    KeysModel* m = raw(id);
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (!m->touchActive[touchId]) return;
    m->touchActive[touchId] = false;
    int note = m->touchNote[touchId];
    m->touchNote[touchId] = -1;
    if (note >= 0) key_off(id, m, note);
}

void release_all(int id)
{
    for (int t = 0; t < MAX_TOUCH_POINTS; t++) touch_up(id, t);
}

}  // namespace ui_keys

// --- C API -------------------------------------------------------------------

extern "C" {

int ui_keys_default(void)
{
    if (get(UI_KEYS_DEFAULT)) return UI_KEYS_DEFAULT;
    bool mine = false;
    portENTER_CRITICAL_SAFE(&s_mux);
    if (!s_claimed[UI_KEYS_DEFAULT]) {
        s_claimed[UI_KEYS_DEFAULT] = true;
        mine = true;
    }
    portEXIT_CRITICAL_SAFE(&s_mux);
    if (mine && !open_slot(UI_KEYS_DEFAULT)) {
        portENTER_CRITICAL_SAFE(&s_mux);
        s_claimed[UI_KEYS_DEFAULT] = false;
        portEXIT_CRITICAL_SAFE(&s_mux);
        return -1;
    }
    // Another task is opening it right now: it will be there in a moment
    return UI_KEYS_DEFAULT;
}

int ui_keys_create(void)
{
    int id = -1;
    portENTER_CRITICAL_SAFE(&s_mux);
    for (int i = UI_KEYS_DEFAULT + 1; i < UI_KEYS_MAX; i++) {
        if (!s_claimed[i]) {
            s_claimed[i] = true;
            id = i;
            break;
        }
    }
    portEXIT_CRITICAL_SAFE(&s_mux);
    if (id < 0) {
        ESP_LOGW(TAG, "no free keyboard (max %d)", UI_KEYS_MAX);
        return -1;
    }
    if (!open_slot(id)) {
        portENTER_CRITICAL_SAFE(&s_mux);
        s_claimed[id] = false;
        portEXIT_CRITICAL_SAFE(&s_mux);
        return -1;
    }
    return id;
}

void ui_keys_destroy(int id)
{
    if (id == UI_KEYS_DEFAULT) {
        ui_keys_reset(id);
        return;
    }
    KeysModel* m = get(id);
    if (!m) return;

    midi_tmask_t bit;
    portENTER_CRITICAL_SAFE(&s_mux);
    m->alive = false;
    bit = m->bit;
    m->bit = 0;
    m->gen = m->gen + 1;     // The view lets go of the fingers (their note offs still go out)
    m->dirtyAll = true;
    if (s_shown == id) s_shown = UI_KEYS_DEFAULT;
    portEXIT_CRITICAL_SAFE(&s_mux);

    if (bit) {
        MIDI_route_set_source(bit, false);
        MIDI_transport_unregister(bit);
    }
    portENTER_CRITICAL_SAFE(&s_mux);
    s_claimed[id] = false;
    portEXIT_CRITICAL_SAFE(&s_mux);
}

bool ui_keys_alive(int id)
{
    return get(id) != nullptr;
}

void ui_keys_set_shown(int id)
{
    if (get(id)) s_shown = id;
}

int ui_keys_shown(void)
{
    int id = s_shown;
    if (get(id)) return id;
    return ui_keys_default();
}

void ui_keys_reset(int id)
{
    KeysModel* m = get(id);
    if (!m) return;
    set_defaults(m);
    m->gen = m->gen + 1;
    mark_all_dirty(m);
}

void ui_keys_cleanup(void)
{
    for (int id = UI_KEYS_DEFAULT + 1; id < UI_KEYS_MAX; id++) ui_keys_destroy(id);
    // Nothing to do for a default keyboard no script or screen ever touched
    ui_keys_reset(UI_KEYS_DEFAULT);
    s_shown = UI_KEYS_DEFAULT;
}

static bool name_is(const char* a, const char* b)
{
    return a && strcmp(a, b) == 0;
}

bool ui_keys_set_i(int id, const char* name, int value)
{
    KeysModel* m = get(id);
    if (!m || !name) return false;

    if (name_is(name, "octaves")) {
        m->octaves = (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
        clamp_range(m);
        mark_all_dirty(m);
    } else if (name_is(name, "low")) {
        m->low = clamp7(value);
        clamp_range(m);
        mark_all_dirty(m);
    } else if (name_is(name, "channel")) {
        m->channel = (uint8_t)(value & 0x0F);
    } else if (name_is(name, "velocity")) {
        m->velocity = clamp7(value < 1 ? 1 : value);
    } else if (name_is(name, "velocity_mode")) {
        m->velocityMode = (value == KEYS_VELOCITY_POSITION) ? KEYS_VELOCITY_POSITION
                                                            : KEYS_VELOCITY_FIXED;
    } else if (name_is(name, "slide")) {
        m->slide = value != 0;
    } else if (name_is(name, "labels")) {
        m->labels = value != 0;
        mark_all_dirty(m);
    } else if (name_is(name, "notify")) {
        m->notify = value != 0;
    } else if (name_is(name, "out_mask")) {
        m->outMask = (midi_tmask_t)value & (midi_tmask_t)~m->bit;
    } else if (name_is(name, "white_color")) {
        m->white = (uint16_t)value;
        mark_all_dirty(m);
    } else if (name_is(name, "black_color")) {
        m->black = (uint16_t)value;
        mark_all_dirty(m);
    } else if (name_is(name, "press_color")) {
        m->press = (uint16_t)value;
        mark_all_dirty(m);
    } else if (name_is(name, "light_color")) {
        m->light = (uint16_t)value;
        mark_all_dirty(m);
    } else {
        return false;
    }
    return true;
}

int ui_keys_get_i(int id, const char* name)
{
    KeysModel* m = get(id);
    if (!m || !name) return 0;
    if (name_is(name, "octaves"))       return m->octaves;
    if (name_is(name, "low"))           return m->low;
    if (name_is(name, "channel"))       return m->channel;
    if (name_is(name, "velocity"))      return m->velocity;
    if (name_is(name, "velocity_mode")) return m->velocityMode;
    if (name_is(name, "slide"))         return m->slide ? 1 : 0;
    if (name_is(name, "labels"))        return m->labels ? 1 : 0;
    if (name_is(name, "notify"))        return m->notify ? 1 : 0;
    if (name_is(name, "out_mask"))      return m->outMask;
    if (name_is(name, "white_color"))   return m->white;
    if (name_is(name, "black_color"))   return m->black;
    if (name_is(name, "press_color"))   return m->press;
    if (name_is(name, "light_color"))   return m->light;
    if (name_is(name, "transport_bit")) return m->bit;
    return 0;
}

void ui_keys_set_key_color(int id, int note, int color)
{
    KeysModel* m = get(id);
    if (!m || note < 0 || note > 127) return;
    uint32_t bit = 1u << (note & 31);
    if (color < 0) {
        m->keyColorSet[note >> 5] &= ~bit;
    } else {
        m->keyColor[note] = (uint16_t)color;
        m->keyColorSet[note >> 5] |= bit;
    }
    mark_dirty(m, note);
}

int ui_keys_get_key_color(int id, int note)
{
    KeysModel* m = get(id);
    if (!m || note < 0 || note > 127) return -1;
    if (!(m->keyColorSet[note >> 5] & (1u << (note & 31)))) return -1;
    return m->keyColor[note];
}

void ui_keys_clear_key_colors(int id)
{
    KeysModel* m = get(id);
    if (!m) return;
    for (int i = 0; i < 4; i++) m->keyColorSet[i] = 0;
    mark_all_dirty(m);
}

void ui_keys_set_channel_color(int id, int channel, int color)
{
    KeysModel* m = get(id);
    if (!m || channel < 0 || channel > 15) return;
    if (color < 0) {
        m->channelColorSet &= (uint16_t)~(1u << channel);
    } else {
        m->channelColor[channel] = (uint16_t)color;
        m->channelColorSet |= (uint16_t)(1u << channel);
    }
    mark_all_dirty(m);
}

void ui_keys_receive(int id, uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    KeysModel* m = get(id);
    if (!m) return;
    uint8_t ch = b1 & 0x0F;
    uint16_t chBit = (uint16_t)(1u << ch);

    switch (cin & 0x0F) {
        case 0x9:
            if ((b1 & 0xF0) != 0x90 || b2 > 127) return;
            if (b3 > 0) {
                portENTER_CRITICAL_SAFE(&s_mux);
                m->lit[b2] |= chBit;
                portEXIT_CRITICAL_SAFE(&s_mux);
                mark_dirty(m, b2);
                return;
            }
            // Velocity 0 is a note off
            [[fallthrough]];
        case 0x8:
            if ((b1 & 0xE0) != 0x80 || b2 > 127) return;
            portENTER_CRITICAL_SAFE(&s_mux);
            m->lit[b2] &= (uint16_t)~chBit;
            portEXIT_CRITICAL_SAFE(&s_mux);
            mark_dirty(m, b2);
            return;
        case 0xB:
            // All Sound Off / All Notes Off: that channel's lights go out
            if ((b1 & 0xF0) != 0xB0 || (b2 != 120 && b2 != 123)) return;
            portENTER_CRITICAL_SAFE(&s_mux);
            for (int n = 0; n < 128; n++) m->lit[n] &= (uint16_t)~chBit;
            portEXIT_CRITICAL_SAFE(&s_mux);
            mark_all_dirty(m);
            return;
        case 0xF:
            if (b1 == 0xFF) ui_keys_clear_lights(id);   // System Reset
            return;
        default:
            return;
    }
}

void ui_keys_clear_lights(int id)
{
    KeysModel* m = get(id);
    if (!m) return;
    portENTER_CRITICAL_SAFE(&s_mux);
    for (int n = 0; n < 128; n++) m->lit[n] = 0;
    portEXIT_CRITICAL_SAFE(&s_mux);
    mark_all_dirty(m);
}

bool ui_keys_is_lit(int id, int note)
{
    KeysModel* m = get(id);
    return m && note >= 0 && note <= 127 && m->lit[note] != 0;
}

bool ui_keys_is_pressed(int id, int note)
{
    KeysModel* m = get(id);
    return m && note >= 0 && note <= 127 && m->holdCount[note] != 0;
}

}  // extern "C"
