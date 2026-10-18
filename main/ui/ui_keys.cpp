// On-screen keyboard model and its picoruby-midi transport (docs/KEYS.md).
// Drawing and touch geometry live in screen_keys.cpp.

#include "ui_keys.h"
#include "ui_manager.h"
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
    // --- Configuration (any task) ---
    volatile uint8_t octaves;
    volatile uint8_t low;
    volatile uint8_t channel;
    volatile uint8_t velocity;
    volatile uint8_t velocityMode;   // keys_velocity_mode_t
    volatile bool    slide;          // A dragged finger moves on to the next key
    volatile bool    labels;         // Note names on the C keys
    volatile bool    notify;         // Queue key events for Ruby (a block is attached)
    volatile uint8_t outMask;        // Destinations of its own, besides the routes
    volatile uint16_t white, black, press, light;

    uint16_t keyColor[128];
    volatile uint32_t keyColorSet[4];
    uint16_t channelColor[16];
    volatile uint16_t channelColorSet;

    // --- Lights: one bit per channel that has the note on (any task) ---
    volatile uint16_t lit[128];

    // --- Fingers (UI task) ---
    uint8_t holdCount[128];          // Fingers on each note
    uint8_t heldMask[128];           // Where its note on went, for the note off
    uint8_t heldChannel[128];
    int16_t touchNote[MAX_TOUCH_POINTS];   // -1: over no key
    bool    touchActive[MAX_TOUCH_POINTS];
};

static KeysModel* volatile s_model = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_dirty[4];
static volatile bool s_dirtyAll = true;
static volatile uint32_t s_gen = 1;
static uint8_t s_bit = 0;            // Transport registry bit, 0 if none

// --- Transport ---------------------------------------------------------------

static int keys_tx_send_packet(void* ctx, uint8_t cable, uint8_t cin,
                               uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)ctx; (void)cable;
    ui_keys_receive(cin, b1, b2, b3);
    return 0;
}

static bool keys_tx_is_connected(void* ctx)
{
    (void)ctx;
    return true;
}

static const midi_transport_ops_t s_transport_ops = {
    .send_packet     = keys_tx_send_packet,
    .read_bytes      = nullptr,     // Its output is pushed, not read (see touch_*)
    .bytes_available = nullptr,
    .is_connected    = keys_tx_is_connected,
    .transport_id    = MIDI_TRANSPORT_ID_NONE,   // Not a wire transport
};

static const midi_transport_t s_transport = {
    .ops = &s_transport_ops,
    .ctx = nullptr,
};

// --- Model -------------------------------------------------------------------

static void mark_dirty(int note)
{
    if (note < 0 || note > 127) return;
    portENTER_CRITICAL_SAFE(&s_mux);
    s_dirty[note >> 5] |= 1u << (note & 31);
    portEXIT_CRITICAL_SAFE(&s_mux);
}

static void mark_all_dirty()
{
    s_dirtyAll = true;
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

// The model, created on first use: by a script's UI::Keys, or by the screen.
// Also where the keyboard joins the transport registry, so its bit is valid
// as soon as Ruby can ask for it.
static KeysModel* model()
{
    KeysModel* m = s_model;
    if (m) return m;

    KeysModel* fresh = (KeysModel*)calloc(1, sizeof(KeysModel));
    if (!fresh) {
        ESP_LOGE(TAG, "out of memory for the keyboard model");
        return nullptr;
    }
    set_defaults(fresh);
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) fresh->touchNote[i] = -1;

    bool won = false;
    portENTER_CRITICAL_SAFE(&s_mux);
    if (!s_model) {
        s_model = fresh;
        won = true;
    }
    portEXIT_CRITICAL_SAFE(&s_mux);
    if (!won) {
        free(fresh);
        return s_model;
    }

    s_bit = MIDI_transport_register(&s_transport);
    if (s_bit == 0) {
        ESP_LOGW(TAG, "MIDI transport registry full: the keyboard is no transport");
    } else {
        MIDI_route_set_source(s_bit, true);
    }
    return fresh;
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

static void post_event(int note, int velocity, int channel, bool on)
{
    ui_event_t event;
    event.type = UI_EVENT_KEY;
    event.data.key.note = (uint8_t)note;
    event.data.key.velocity = (uint8_t)velocity;
    event.data.key.channel = (uint8_t)channel;
    event.data.key.on = on;
    ui_event_push(&event);
}

static void key_on(KeysModel* m, int note, int velocity)
{
    if (note < 0 || note > 127) return;
    if (m->holdCount[note]++ > 0) return;   // Another finger already sounds it

    uint8_t ch = m->channel & 0x0F;
    uint8_t status = 0x90 | ch;
    uint8_t vel = velocity < 1 ? 1 : clamp7(velocity);
    uint8_t mask = m->outMask;
    if (s_bit) {
        mask |= MIDI_route_targets(s_bit, 0x9, status);
        mask &= (uint8_t)~s_bit;            // Never back into itself
    }
    m->heldMask[note] = mask;
    m->heldChannel[note] = ch;
    if (mask) MIDI_transport_send(mask, 0, 0x9, status, (uint8_t)note, vel);

    mark_dirty(note);
    if (m->notify) post_event(note, vel, ch, true);
}

static void key_off(KeysModel* m, int note)
{
    if (note < 0 || note > 127 || m->holdCount[note] == 0) return;
    if (--m->holdCount[note] > 0) return;

    uint8_t ch = m->heldChannel[note];
    uint8_t mask = m->heldMask[note];
    if (mask) MIDI_transport_send(mask, 0, 0x8, 0x80 | ch, (uint8_t)note, 0);

    mark_dirty(note);
    if (m->notify) post_event(note, 0, ch, false);
}

namespace ui_keys {

int low()      { KeysModel* m = model(); return m ? m->low : DEFAULT_LOW; }
int octaves()  { KeysModel* m = model(); return m ? m->octaves : DEFAULT_OCTAVES; }
int top()      { return low() + 12 * octaves(); }
bool labels()  { KeysModel* m = model(); return m ? m->labels : true; }
int velocity() { KeysModel* m = model(); return m ? m->velocity : DEFAULT_VELOCITY; }

bool velocity_by_position()
{
    KeysModel* m = model();
    return m && m->velocityMode == KEYS_VELOCITY_POSITION;
}

uint16_t key_fill(int note)
{
    KeysModel* m = model();
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

uint32_t generation()
{
    return s_gen;
}

void take_dirty(uint32_t out[4], bool& full)
{
    portENTER_CRITICAL_SAFE(&s_mux);
    for (int i = 0; i < 4; i++) {
        out[i] = s_dirty[i];
        s_dirty[i] = 0;
    }
    full = s_dirtyAll;
    s_dirtyAll = false;
    portEXIT_CRITICAL_SAFE(&s_mux);
}

void touch_down(int touchId, int note, int velocity)
{
    KeysModel* m = model();
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (m->touchActive[touchId]) touch_up(touchId);
    m->touchActive[touchId] = true;
    m->touchNote[touchId] = (int16_t)note;
    if (note >= 0) key_on(m, note, velocity);
}

void touch_move(int touchId, int note, int velocity)
{
    KeysModel* m = model();
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (!m->touchActive[touchId]) return;
    int current = m->touchNote[touchId];
    if (note == current) return;
    // Without slide a finger keeps the key it landed on, wherever it goes
    if (!m->slide && current >= 0) return;

    if (current >= 0) key_off(m, current);
    m->touchNote[touchId] = (int16_t)note;
    if (note >= 0) key_on(m, note, velocity);
}

void touch_up(int touchId)
{
    KeysModel* m = model();
    if (!m || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    if (!m->touchActive[touchId]) return;
    m->touchActive[touchId] = false;
    int note = m->touchNote[touchId];
    m->touchNote[touchId] = -1;
    if (note >= 0) key_off(m, note);
}

void release_all()
{
    for (int t = 0; t < MAX_TOUCH_POINTS; t++) touch_up(t);
}

}  // namespace ui_keys

// --- C API -------------------------------------------------------------------

extern "C" {

void ui_keys_reset(void)
{
    KeysModel* m = model();
    if (!m) return;
    set_defaults(m);
    s_gen = s_gen + 1;
    mark_all_dirty();
}

void ui_keys_cleanup(void)
{
    // Nothing to do for a keyboard no script or screen ever touched
    if (s_model) ui_keys_reset();
}

static bool name_is(const char* a, const char* b)
{
    return a && strcmp(a, b) == 0;
}

bool ui_keys_set_i(const char* name, int value)
{
    KeysModel* m = model();
    if (!m || !name) return false;

    if (name_is(name, "octaves")) {
        m->octaves = (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
        clamp_range(m);
        mark_all_dirty();
    } else if (name_is(name, "low")) {
        m->low = clamp7(value);
        clamp_range(m);
        mark_all_dirty();
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
        mark_all_dirty();
    } else if (name_is(name, "notify")) {
        m->notify = value != 0;
    } else if (name_is(name, "out_mask")) {
        m->outMask = (uint8_t)(value & 0xFF) & (uint8_t)~s_bit;
    } else if (name_is(name, "white_color")) {
        m->white = (uint16_t)value;
        mark_all_dirty();
    } else if (name_is(name, "black_color")) {
        m->black = (uint16_t)value;
        mark_all_dirty();
    } else if (name_is(name, "press_color")) {
        m->press = (uint16_t)value;
        mark_all_dirty();
    } else if (name_is(name, "light_color")) {
        m->light = (uint16_t)value;
        mark_all_dirty();
    } else {
        return false;
    }
    return true;
}

int ui_keys_get_i(const char* name)
{
    KeysModel* m = model();
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
    if (name_is(name, "transport_bit")) return s_bit;
    return 0;
}

void ui_keys_set_key_color(int note, int color)
{
    KeysModel* m = model();
    if (!m || note < 0 || note > 127) return;
    uint32_t bit = 1u << (note & 31);
    if (color < 0) {
        m->keyColorSet[note >> 5] &= ~bit;
    } else {
        m->keyColor[note] = (uint16_t)color;
        m->keyColorSet[note >> 5] |= bit;
    }
    mark_dirty(note);
}

int ui_keys_get_key_color(int note)
{
    KeysModel* m = model();
    if (!m || note < 0 || note > 127) return -1;
    if (!(m->keyColorSet[note >> 5] & (1u << (note & 31)))) return -1;
    return m->keyColor[note];
}

void ui_keys_clear_key_colors(void)
{
    KeysModel* m = model();
    if (!m) return;
    for (int i = 0; i < 4; i++) m->keyColorSet[i] = 0;
    mark_all_dirty();
}

void ui_keys_set_channel_color(int channel, int color)
{
    KeysModel* m = model();
    if (!m || channel < 0 || channel > 15) return;
    if (color < 0) {
        m->channelColorSet &= (uint16_t)~(1u << channel);
    } else {
        m->channelColor[channel] = (uint16_t)color;
        m->channelColorSet |= (uint16_t)(1u << channel);
    }
    mark_all_dirty();
}

void ui_keys_receive(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    KeysModel* m = model();
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
                mark_dirty(b2);
                return;
            }
            // Velocity 0 is a note off
            [[fallthrough]];
        case 0x8:
            if ((b1 & 0xE0) != 0x80 || b2 > 127) return;
            portENTER_CRITICAL_SAFE(&s_mux);
            m->lit[b2] &= (uint16_t)~chBit;
            portEXIT_CRITICAL_SAFE(&s_mux);
            mark_dirty(b2);
            return;
        case 0xB:
            // All Sound Off / All Notes Off: that channel's lights go out
            if ((b1 & 0xF0) != 0xB0 || (b2 != 120 && b2 != 123)) return;
            portENTER_CRITICAL_SAFE(&s_mux);
            for (int n = 0; n < 128; n++) m->lit[n] &= (uint16_t)~chBit;
            portEXIT_CRITICAL_SAFE(&s_mux);
            mark_all_dirty();
            return;
        case 0xF:
            if (b1 == 0xFF) ui_keys_clear_lights();   // System Reset
            return;
        default:
            return;
    }
}

void ui_keys_clear_lights(void)
{
    KeysModel* m = model();
    if (!m) return;
    portENTER_CRITICAL_SAFE(&s_mux);
    for (int n = 0; n < 128; n++) m->lit[n] = 0;
    portEXIT_CRITICAL_SAFE(&s_mux);
    mark_all_dirty();
}

bool ui_keys_is_lit(int note)
{
    KeysModel* m = model();
    return m && note >= 0 && note <= 127 && m->lit[note] != 0;
}

bool ui_keys_is_pressed(int note)
{
    KeysModel* m = model();
    return m && note >= 0 && note <= 127 && m->holdCount[note] != 0;
}

}  // extern "C"
