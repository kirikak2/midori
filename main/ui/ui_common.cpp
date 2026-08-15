#include "ui_common.h"
#include "ui_manager.h"
#include "screen_log.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include "esp_log.h"

// Global pad configuration
pad_config_t g_pads[UI_PAD_COUNT] = {0};

// MIDI indicator state
static bool s_midi_connected = false;

// Mutex for thread safety
static portMUX_TYPE s_ui_mutex = portMUX_INITIALIZER_UNLOCKED;

void ui_draw_status_bar(const char* title)
{
    portENTER_CRITICAL(&s_ui_mutex);

    // Draw status bar background
    M5.Lcd.fillRect(0, 0, UI_SCREEN_WIDTH, UI_STATUS_BAR_HEIGHT, UI_COLOR_NAVY);

    // Draw title
    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_NAVY);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(4, 4);
    M5.Lcd.print(title);

    // Draw MIDI indicator
    uint16_t indicator_color = s_midi_connected ? UI_COLOR_GREEN : UI_COLOR_DARKGRAY;
    M5.Lcd.fillCircle(UI_SCREEN_WIDTH - 14, UI_STATUS_BAR_HEIGHT / 2, 6, indicator_color);

    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_draw_nav_bar(const char* center_label, bool show_arrows)
{
    portENTER_CRITICAL(&s_ui_mutex);

    int nav_y = UI_SCREEN_HEIGHT - UI_NAV_BAR_HEIGHT;

    // Draw navigation bar background
    M5.Lcd.fillRect(0, nav_y, UI_SCREEN_WIDTH, UI_NAV_BAR_HEIGHT, UI_COLOR_DARKGRAY);

    // Draw border at top
    M5.Lcd.drawLine(0, nav_y, UI_SCREEN_WIDTH, nav_y, UI_COLOR_GRAY);

    M5.Lcd.setTextColor(UI_COLOR_WHITE, UI_COLOR_DARKGRAY);
    M5.Lcd.setTextSize(2);

    if (show_arrows) {
        // Draw left arrow
        M5.Lcd.setCursor(20, nav_y + 12);
        M5.Lcd.print("<");

        // Draw right arrow
        M5.Lcd.setCursor(UI_SCREEN_WIDTH - 30, nav_y + 12);
        M5.Lcd.print(">");
    }

    // Draw center label
    int label_width = strlen(center_label) * 12;  // Approximate width at text size 2
    int label_x = (UI_SCREEN_WIDTH - label_width) / 2;
    M5.Lcd.setCursor(label_x, nav_y + 12);
    M5.Lcd.print(center_label);

    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_draw_button(int x, int y, int w, int h, const char* label, uint16_t bg_color, uint16_t text_color, bool pressed)
{
    portENTER_CRITICAL(&s_ui_mutex);
    M5.Lcd.startWrite();

    // Adjust color if pressed
    uint16_t draw_color = pressed ? ui_lighten_color(bg_color) : bg_color;

    // Draw button background
    M5.Lcd.fillRoundRect(x, y, w, h, 4, draw_color);

    // Draw border
    M5.Lcd.drawRoundRect(x, y, w, h, 4, UI_COLOR_WHITE);

    // Draw label centered
    M5.Lcd.setTextColor(text_color, draw_color);
    M5.Lcd.setTextSize(1);

    int text_width = strlen(label) * 6;  // Approximate width at text size 1
    int text_x = x + (w - text_width) / 2;
    int text_y = y + (h - 8) / 2;

    M5.Lcd.setCursor(text_x, text_y);
    M5.Lcd.print(label);

    M5.Lcd.endWrite();
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_clear_content_area(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    M5.Lcd.fillRect(0, UI_CONTENT_Y, UI_SCREEN_WIDTH, UI_CONTENT_HEIGHT, UI_COLOR_BLACK);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_set_midi_indicator(bool connected)
{
    if (s_midi_connected != connected) {
        s_midi_connected = connected;
        // Redraw just the indicator
        portENTER_CRITICAL(&s_ui_mutex);
        uint16_t color = connected ? UI_COLOR_GREEN : UI_COLOR_DARKGRAY;
        M5.Lcd.fillCircle(UI_SCREEN_WIDTH - 14, UI_STATUS_BAR_HEIGHT / 2, 6, color);
        portEXIT_CRITICAL(&s_ui_mutex);
    }
}

bool ui_get_midi_indicator(void)
{
    return s_midi_connected;
}

uint16_t ui_lighten_color(uint16_t color)
{
    // Extract RGB565 components
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;

    // Increase brightness (cap at max)
    r = (r + 8 > 0x1F) ? 0x1F : r + 8;
    g = (g + 16 > 0x3F) ? 0x3F : g + 16;
    b = (b + 8 > 0x1F) ? 0x1F : b + 8;

    return (r << 11) | (g << 5) | b;
}

uint16_t ui_darken_color(uint16_t color)
{
    // Extract RGB565 components
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;

    // Decrease brightness
    r = (r > 8) ? r - 8 : 0;
    g = (g > 16) ? g - 16 : 0;
    b = (b > 8) ? b - 8 : 0;

    return (r << 11) | (g << 5) | b;
}

// Pads whose appearance changed and still need to be repainted (bit N = pad N).
// Set from whichever task calls ui_pad_set*/ui_pad_clear* (typically the
// PicoRuby task via UI.pad / UI.pad_color / UI.pad_label) and consumed by the
// UI task in ScreenPads::update(). Drawing must not happen in the setters --
// only the UI task may touch the LCD.
static uint32_t s_pad_dirty = 0;

#define UI_PAD_DIRTY_ALL ((1u << UI_PAD_COUNT) - 1u)

void ui_pad_mark_dirty(uint8_t index)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    s_pad_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

uint32_t ui_pad_take_dirty(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    uint32_t mask = s_pad_dirty;
    s_pad_dirty = 0;
    portEXIT_CRITICAL(&s_ui_mutex);
    return mask;
}

// Pad functions
void ui_pad_set(uint8_t index, const char* label, pad_color_t color, pad_type_t type)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    g_pads[index].assigned = true;
    strncpy(g_pads[index].label, label, sizeof(g_pads[index].label) - 1);
    g_pads[index].label[sizeof(g_pads[index].label) - 1] = '\0';
    g_pads[index].color = color;
    g_pads[index].type = type;
    g_pads[index].state = false;
    s_pad_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_pad_clear(uint8_t index)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    g_pads[index].assigned = false;
    g_pads[index].label[0] = '\0';
    g_pads[index].state = false;
    s_pad_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_pad_clear_all(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    for (int i = 0; i < UI_PAD_COUNT; i++) {
        g_pads[i].assigned = false;
        g_pads[i].label[0] = '\0';
        g_pads[i].state = false;
    }
    s_pad_dirty = UI_PAD_DIRTY_ALL;
    portEXIT_CRITICAL(&s_ui_mutex);
}

bool ui_pad_get_state(uint8_t index)
{
    if (index >= UI_PAD_COUNT) return false;
    return g_pads[index].state;
}

void ui_pad_set_state(uint8_t index, bool state)
{
    if (index >= UI_PAD_COUNT) return;
    g_pads[index].state = state;
}

void ui_pad_set_label(uint8_t index, const char* label)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    strncpy(g_pads[index].label, label, sizeof(g_pads[index].label) - 1);
    g_pads[index].label[sizeof(g_pads[index].label) - 1] = '\0';
    s_pad_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_pad_set_color(uint8_t index, pad_color_t color)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    g_pads[index].color = color;
    s_pad_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

const pad_config_t* ui_pad_get_config(uint8_t index)
{
    if (index >= UI_PAD_COUNT) return NULL;
    return &g_pads[index];
}

// UI Event queue implementation
#define UI_EVENT_QUEUE_SIZE 32
static ui_event_t s_event_queue[UI_EVENT_QUEUE_SIZE];
static volatile int s_event_head = 0;
static volatile int s_event_tail = 0;
static portMUX_TYPE s_event_mutex = portMUX_INITIALIZER_UNLOCKED;

void ui_event_init(void)
{
    portENTER_CRITICAL(&s_event_mutex);
    s_event_head = 0;
    s_event_tail = 0;
    portEXIT_CRITICAL(&s_event_mutex);
}

void ui_event_push(const ui_event_t* event)
{
    if (event == NULL) return;

    portENTER_CRITICAL(&s_event_mutex);
    int next_head = (s_event_head + 1) % UI_EVENT_QUEUE_SIZE;
    if (next_head != s_event_tail) {
        s_event_queue[s_event_head] = *event;
        s_event_head = next_head;
    }
    // else: queue full, drop event
    portEXIT_CRITICAL(&s_event_mutex);
}

bool ui_event_pop(ui_event_t* event)
{
    if (event == NULL) return false;

    portENTER_CRITICAL(&s_event_mutex);
    if (s_event_tail == s_event_head) {
        portEXIT_CRITICAL(&s_event_mutex);
        return false;
    }
    *event = s_event_queue[s_event_tail];
    s_event_tail = (s_event_tail + 1) % UI_EVENT_QUEUE_SIZE;
    portEXIT_CRITICAL(&s_event_mutex);
    return true;
}

int ui_event_available(void)
{
    portENTER_CRITICAL(&s_event_mutex);
    int count = (s_event_head - s_event_tail + UI_EVENT_QUEUE_SIZE) % UI_EVENT_QUEUE_SIZE;
    portEXIT_CRITICAL(&s_event_mutex);
    return count;
}

void ui_knob_event_post(uint8_t bank, uint8_t index, float value, bool final)
{
    portENTER_CRITICAL(&s_event_mutex);

    // Coalesce: a change already waiting for this knob is stale the moment a
    // newer one arrives, so overwrite it in place. The queue therefore holds at
    // most one entry per knob no matter how hard the knob is being turned, and
    // a script that polls slowly simply sees fewer, later values.
    for (int i = s_event_tail; i != s_event_head; i = (i + 1) % UI_EVENT_QUEUE_SIZE) {
        if (s_event_queue[i].type == UI_EVENT_KNOB_CHANGE
         && s_event_queue[i].data.knob.bank == bank
         && s_event_queue[i].data.knob.index == index) {
            s_event_queue[i].data.knob.value = value;
            // Never downgrade: the release must still read as final even if a
            // move event was the one sitting in the queue.
            if (final) s_event_queue[i].data.knob.final = true;
            portEXIT_CRITICAL(&s_event_mutex);
            return;
        }
    }

    int next_head = (s_event_head + 1) % UI_EVENT_QUEUE_SIZE;
    if (next_head != s_event_tail) {
        s_event_queue[s_event_head].type = UI_EVENT_KNOB_CHANGE;
        s_event_queue[s_event_head].data.knob.bank = bank;
        s_event_queue[s_event_head].data.knob.index = index;
        s_event_queue[s_event_head].data.knob.value = value;
        s_event_queue[s_event_head].data.knob.final = final;
        s_event_head = next_head;
    }
    portEXIT_CRITICAL(&s_event_mutex);
}

// --- Knobs ----------------------------------------------------------------

knob_config_t  g_knob_bank0[UI_KNOB_COUNT] = {};
knob_config_t *g_knob_banks[UI_KNOB_BANKS] = { g_knob_bank0, NULL, NULL, NULL };

static uint8_t  s_knob_bank = 0;
static uint32_t s_knob_dirty = 0;     // Value moved: repaint the gauge delta
static uint32_t s_knob_repaint = 0;   // Label/colour/range changed: repaint all

#define UI_KNOB_DIRTY_ALL ((1u << UI_KNOB_COUNT) - 1u)

// Returns the bank's table, allocating it on first write. Must not be called
// from inside a critical section: calloc() can block, and interrupts are off in
// there. Only the writers (PicoRuby) ever allocate, so publishing the pointer
// afterwards is the only part that needs the lock.
static knob_config_t* knob_bank_ensure(uint8_t bank)
{
    if (bank >= UI_KNOB_BANKS) return NULL;
    if (g_knob_banks[bank]) return g_knob_banks[bank];

    knob_config_t* table = (knob_config_t*)calloc(UI_KNOB_COUNT, sizeof(knob_config_t));
    if (!table) {
        ESP_LOGE("UI_KNOB", "Failed to allocate knob bank %d", bank + 1);
        return NULL;
    }

    portENTER_CRITICAL(&s_ui_mutex);
    if (g_knob_banks[bank] == NULL) {
        g_knob_banks[bank] = table;
        table = NULL;
    }
    portEXIT_CRITICAL(&s_ui_mutex);

    free(table);   // Lost a race with another writer; keep the published one
    return g_knob_banks[bank];
}

static knob_config_t* knob_at(uint8_t bank, uint8_t index)
{
    if (bank >= UI_KNOB_BANKS || index >= UI_KNOB_COUNT) return NULL;
    knob_config_t* table = g_knob_banks[bank];
    return table ? &table[index] : NULL;
}

// Clamp to the knob's range, then snap to its step. Both are applied here and
// nowhere else, so "the value changed" means the same thing to the display, to
// the event queue and to the Ruby block.
static float knob_quantize(const knob_config_t* k, float value)
{
    float lo = k->min;
    float hi = k->max;
    if (hi < lo) { float t = lo; lo = hi; hi = t; }

    if (k->step > 0.0f) {
        value = lo + roundf((value - lo) / k->step) * k->step;
    }
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    return value;
}

void ui_knob_mark_dirty(uint8_t index)
{
    if (index >= UI_KNOB_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    s_knob_dirty |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

uint32_t ui_knob_take_dirty(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    uint32_t mask = s_knob_dirty;
    s_knob_dirty = 0;
    portEXIT_CRITICAL(&s_ui_mutex);
    return mask;
}

void ui_knob_mark_repaint(uint8_t index)
{
    if (index >= UI_KNOB_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    s_knob_repaint |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

uint32_t ui_knob_take_repaint(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    uint32_t mask = s_knob_repaint;
    s_knob_repaint = 0;
    portEXIT_CRITICAL(&s_ui_mutex);
    return mask;
}

void ui_knob_set_config(uint8_t bank, uint8_t index, const char* label,
                        uint16_t color, float min, float max, float step,
                        float value, uint8_t origin, float sensitivity,
                        bool notify)
{
    if (index >= UI_KNOB_COUNT) return;
    knob_config_t* table = knob_bank_ensure(bank);
    if (!table) return;

    // Built outside the lock so no floating point runs with interrupts off.
    knob_config_t fresh = {};
    fresh.assigned = true;
    if (label) {
        strncpy(fresh.label, label, sizeof(fresh.label) - 1);
    }
    fresh.color = color;
    // Normalized here so nothing downstream has to think about it: the gauge
    // divides by the span, and the drag clamps against both ends.
    if (max < min) { float t = min; min = max; max = t; }
    fresh.min = min;
    fresh.max = (max == min) ? min + 1.0f : max;   // Keep the sweep divisible
    fresh.step = (step < 0.0f) ? 0.0f : step;
    fresh.origin = (origin == KNOB_ORIGIN_CENTER) ? KNOB_ORIGIN_CENTER : KNOB_ORIGIN_MIN;
    fresh.sensitivity = (sensitivity > 0.0f) ? sensitivity : 1.0f;
    fresh.notify = notify;
    fresh.value = knob_quantize(&fresh, value);
    fresh.initial = fresh.value;

    portENTER_CRITICAL(&s_ui_mutex);
    table[index] = fresh;
    if (bank == s_knob_bank) s_knob_repaint |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_knob_clear(uint8_t bank, uint8_t index)
{
    knob_config_t* k = knob_at(bank, index);
    if (!k) return;

    portENTER_CRITICAL(&s_ui_mutex);
    memset(k, 0, sizeof(*k));
    if (bank == s_knob_bank) s_knob_repaint |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_knob_clear_all(void)
{
    // Collect the allocated tables under the lock, free them outside it.
    knob_config_t* freeing[UI_KNOB_BANKS] = {};

    portENTER_CRITICAL(&s_ui_mutex);
    memset(g_knob_bank0, 0, sizeof(g_knob_bank0));
    for (int b = 1; b < UI_KNOB_BANKS; b++) {
        freeing[b] = g_knob_banks[b];
        g_knob_banks[b] = NULL;
    }
    s_knob_bank = 0;
    s_knob_repaint = UI_KNOB_DIRTY_ALL;
    portEXIT_CRITICAL(&s_ui_mutex);

    for (int b = 1; b < UI_KNOB_BANKS; b++) {
        free(freeing[b]);
    }
}

float ui_knob_get_value(uint8_t bank, uint8_t index)
{
    const knob_config_t* k = knob_at(bank, index);
    return k ? k->value : 0.0f;
}

bool ui_knob_set_value(uint8_t bank, uint8_t index, float value, bool notify)
{
    knob_config_t* k = knob_at(bank, index);
    if (!k || !k->assigned) return false;

    float quantized = knob_quantize(k, value);

    portENTER_CRITICAL(&s_ui_mutex);
    bool changed = (quantized != k->value);
    if (changed) {
        k->value = quantized;
        if (bank == s_knob_bank) s_knob_dirty |= (1u << index);
    }
    bool wants_event = changed && notify && k->notify;
    portEXIT_CRITICAL(&s_ui_mutex);

    if (wants_event) {
        ui_knob_event_post(bank, index, quantized, false);
    }
    return changed;
}

bool ui_knob_reset(uint8_t bank, uint8_t index)
{
    const knob_config_t* k = knob_at(bank, index);
    if (!k) return false;
    return ui_knob_set_value(bank, index, k->initial, true);
}

void ui_knob_notify_all(uint8_t bank)
{
    for (uint8_t i = 0; i < UI_KNOB_COUNT; i++) {
        const knob_config_t* k = knob_at(bank, i);
        if (k && k->assigned && k->notify) {
            ui_knob_event_post(bank, i, k->value, true);
        }
    }
}

void ui_knob_set_label(uint8_t bank, uint8_t index, const char* label)
{
    knob_config_t* k = knob_at(bank, index);
    if (!k || !label) return;

    portENTER_CRITICAL(&s_ui_mutex);
    strncpy(k->label, label, sizeof(k->label) - 1);
    k->label[sizeof(k->label) - 1] = '\0';
    if (bank == s_knob_bank) s_knob_repaint |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_knob_set_color(uint8_t bank, uint8_t index, uint16_t color)
{
    knob_config_t* k = knob_at(bank, index);
    if (!k) return;

    portENTER_CRITICAL(&s_ui_mutex);
    k->color = color;
    if (bank == s_knob_bank) s_knob_repaint |= (1u << index);
    portEXIT_CRITICAL(&s_ui_mutex);
}

const knob_config_t* ui_knob_get_config(uint8_t bank, uint8_t index)
{
    return knob_at(bank, index);
}

bool ui_knob_bank_in_use(uint8_t bank)
{
    if (bank >= UI_KNOB_BANKS || !g_knob_banks[bank]) return false;
    for (int i = 0; i < UI_KNOB_COUNT; i++) {
        if (g_knob_banks[bank][i].assigned) return true;
    }
    return false;
}

uint8_t ui_knob_get_bank(void)
{
    return s_knob_bank;
}

void ui_knob_set_bank(uint8_t bank)
{
    if (bank >= UI_KNOB_BANKS || bank == s_knob_bank) return;

    portENTER_CRITICAL(&s_ui_mutex);
    s_knob_bank = bank;
    s_knob_repaint = UI_KNOB_DIRTY_ALL;
    portEXIT_CRITICAL(&s_ui_mutex);

    ui_event_t event;
    event.type = UI_EVENT_KNOB_BANK;
    event.data.knob_bank = bank;
    ui_event_push(&event);

    // The bank name is part of the title and the nav label, so the frame around
    // the content has to be repainted too -- but only if it is on screen.
    if (ui_get_current_screen() == UI_SCREEN_KNOBS) {
        ui_request_redraw();
    }
}

// --- XYPad ------------------------------------------------------------------
// Model lives here (not in a screen_xypad.cpp state block) for the same reason
// knobs do: nothing here draws or sends MIDI, so it does not need M5Unified or
// the Ruby VM, and can be exercised on its own. ScreenXYPad only turns touches
// into calls here and paints whatever the slots report back.

static const uint8_t XYPAD_DEFAULT_SCALE[] = { 36, 38, 40, 41, 43, 45, 47, 48 };

static xypad_slot_t s_xypad_slots[UI_XYPAD_MAX_TOUCHES];
static uint8_t      s_xypad_max_touches = UI_XYPAD_MAX_TOUCHES;

static void xypad_slot_defaults(xypad_slot_t* s, uint8_t index)
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

void ui_xypad_reset(void)
{
    portENTER_CRITICAL(&s_ui_mutex);
    for (uint8_t i = 0; i < UI_XYPAD_MAX_TOUCHES; i++) {
        xypad_slot_defaults(&s_xypad_slots[i], i);
    }
    s_xypad_max_touches = UI_XYPAD_MAX_TOUCHES;
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_xypad_set_max_touches(uint8_t n)
{
    if (n < 1) n = 1;
    if (n > UI_XYPAD_MAX_TOUCHES) n = UI_XYPAD_MAX_TOUCHES;
    portENTER_CRITICAL(&s_ui_mutex);
    s_xypad_max_touches = n;
    portEXIT_CRITICAL(&s_ui_mutex);
}

uint8_t ui_xypad_get_max_touches(void)
{
    return s_xypad_max_touches;
}

static xypad_slot_t* xypad_slot_at(uint8_t index)
{
    if (index >= UI_XYPAD_MAX_TOUCHES) return NULL;
    return &s_xypad_slots[index];
}

bool ui_xypad_set_f(uint8_t index, const char* name, float value)
{
    xypad_slot_t* s = xypad_slot_at(index);
    if (!s || !name) return false;

    portENTER_CRITICAL(&s_ui_mutex);
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
    portEXIT_CRITICAL(&s_ui_mutex);
    return ok;
}

bool ui_xypad_set_i(uint8_t index, const char* name, int value)
{
    xypad_slot_t* s = xypad_slot_at(index);
    if (!s || !name) return false;

    // Hold needs special handling: a slot latched by Hold has nobody left to
    // release it once the script turns hold off, so that transition has to
    // force the release right here. Ruby broadcasts (pad.hold = false) call
    // this once per slot, which is what makes "release every latched slot at
    // once" fall out of a per-slot rule rather than needing a separate
    // all-slots API.
    if (!strcmp(name, "hold")) {
        bool hold = (value != 0);
        portENTER_CRITICAL(&s_ui_mutex);
        bool was_hold = s->hold;
        s->hold = hold;
        bool release = (!hold && was_hold && s->latched && !s->active);
        if (release) s->latched = false;
        uint8_t channel = s->channel;
        uint8_t note = s->note;
        float   bend = s->bend_semitones;
        float   xv = s->x;
        float   yv = s->y;
        portEXIT_CRITICAL(&s_ui_mutex);
        if (release) {
            ui_xypad_event_post(index, XYPAD_PHASE_UP, channel, note, bend, xv, yv);
        }
        return true;
    }

    portENTER_CRITICAL(&s_ui_mutex);
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
    portEXIT_CRITICAL(&s_ui_mutex);
    return ok;
}

float ui_xypad_get_f(uint8_t index, const char* name)
{
    const xypad_slot_t* s = xypad_slot_at(index);
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

int ui_xypad_get_i(uint8_t index, const char* name)
{
    const xypad_slot_t* s = xypad_slot_at(index);
    if (!s || !name) return 0;
    if (!strcmp(name, "x_mode"))    return s->x_mode;
    if (!strcmp(name, "y_invert")) return s->y_invert ? 1 : 0;
    if (!strcmp(name, "gate_note")) return s->gate_note;
    if (!strcmp(name, "channel"))  return s->channel;
    if (!strcmp(name, "hold"))     return s->hold ? 1 : 0;
    return 0;
}

void ui_xypad_set_scale(uint8_t index, const uint8_t* notes, uint8_t len)
{
    xypad_slot_t* s = xypad_slot_at(index);
    if (!s) return;
    if (len > UI_XYPAD_MAX_SCALE) len = UI_XYPAD_MAX_SCALE;

    portENTER_CRITICAL(&s_ui_mutex);
    if (notes && len > 0) {
        memcpy(s->scale, notes, len);
    }
    s->scale_len = len;
    portEXIT_CRITICAL(&s_ui_mutex);
}

uint8_t ui_xypad_get_scale(uint8_t index, uint8_t* out, uint8_t max_len)
{
    const xypad_slot_t* s = xypad_slot_at(index);
    if (!s || !out) return 0;
    uint8_t n = s->scale_len;
    if (n > max_len) n = max_len;
    memcpy(out, s->scale, n);
    return n;
}

const xypad_slot_t* ui_xypad_get_slot(uint8_t index)
{
    return xypad_slot_at(index);
}

static int xypad_find_free_slot(void)
{
    for (uint8_t i = 0; i < s_xypad_max_touches; i++) {
        if (!s_xypad_slots[i].active && !s_xypad_slots[i].latched) return (int)i;
    }
    return -1;
}

static int xypad_find_slot_by_touch(int touch_id)
{
    for (uint8_t i = 0; i < UI_XYPAD_MAX_TOUCHES; i++) {
        if (s_xypad_slots[i].active && s_xypad_slots[i].touch_id == touch_id) return (int)i;
    }
    return -1;
}

static uint8_t xypad_snap_note(const xypad_slot_t* s, int x, int content_x, int content_w)
{
    if (s->scale_len == 0 || content_w <= 0) return s->gate_note;
    int rel = x - content_x;
    if (rel < 0) rel = 0;
    if (rel >= content_w) rel = content_w - 1;
    int cell = (rel * s->scale_len) / content_w;
    if (cell >= s->scale_len) cell = s->scale_len - 1;
    return s->scale[cell];
}

static float xypad_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float xypad_map_x(const xypad_slot_t* s, int x, int content_x, int content_w)
{
    if (content_w <= 0) return s->x_min;
    float t = (float)(x - content_x) / (float)content_w;
    t = xypad_clampf(t, 0.0f, 1.0f);
    return s->x_min + (s->x_max - s->x_min) * t;
}

// Screen y grows downward; the top reads as the larger value by default
// (matches how effects hardware usually lays these pads out), y_invert flips it.
static float xypad_map_y(const xypad_slot_t* s, int y, int content_y, int content_h)
{
    if (content_h <= 0) return s->y_min;
    float t = (float)(y - content_y) / (float)content_h;
    t = xypad_clampf(t, 0.0f, 1.0f);
    float t2 = s->y_invert ? t : (1.0f - t);
    return s->y_min + (s->y_max - s->y_min) * t2;
}

int ui_xypad_touch_down(int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    portENTER_CRITICAL(&s_ui_mutex);

    int idx = xypad_find_free_slot();
    if (idx < 0) {
        portEXIT_CRITICAL(&s_ui_mutex);
        return -1;
    }

    xypad_slot_t* s = &s_xypad_slots[idx];
    s->active = true;
    s->latched = false;
    s->touch_id = touch_id;
    s->touch_x_mode = s->x_mode;
    s->touch_glide_range = s->glide_range;
    s->touchdown_x = (int16_t)x;

    if (s->touch_x_mode == XYPAD_XMODE_NOTE) {
        s->note = xypad_snap_note(s, x, content_x, content_w);
        s->bend_semitones = 0.0f;
        s->x = 0.0f;
    } else {
        s->note = s->gate_note;
        s->x = xypad_map_x(s, x, content_x, content_w);
        s->bend_semitones = 0.0f;
    }
    s->y = xypad_map_y(s, y, content_y, content_h);

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_ui_mutex);

    ui_xypad_event_post((uint8_t)idx, XYPAD_PHASE_DOWN, channel, note, bend, xv, yv);
    return idx;
}

int ui_xypad_touch_move(int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    portENTER_CRITICAL(&s_ui_mutex);

    int idx = xypad_find_slot_by_touch(touch_id);
    if (idx < 0) {
        portEXIT_CRITICAL(&s_ui_mutex);
        return -1;
    }
    xypad_slot_t* s = &s_xypad_slots[idx];

    if (s->touch_x_mode == XYPAD_XMODE_NOTE) {
        float range = s->touch_glide_range;
        float dx = (float)(x - s->touchdown_x);
        float bend = (content_w > 0) ? (dx / (float)content_w) * range : 0.0f;
        s->bend_semitones = xypad_clampf(bend, -range, range);
    } else {
        s->x = xypad_map_x(s, x, content_x, content_w);
    }
    s->y = xypad_map_y(s, y, content_y, content_h);

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_ui_mutex);

    ui_xypad_event_post((uint8_t)idx, XYPAD_PHASE_MOVE, channel, note, bend, xv, yv);
    return idx;
}

int ui_xypad_touch_up(int touch_id)
{
    portENTER_CRITICAL(&s_ui_mutex);

    int idx = xypad_find_slot_by_touch(touch_id);
    if (idx < 0) {
        portEXIT_CRITICAL(&s_ui_mutex);
        return -1;
    }
    xypad_slot_t* s = &s_xypad_slots[idx];

    bool hold = s->hold;
    s->active = false;
    s->touch_id = -1;
    if (hold) s->latched = true;

    uint8_t channel = s->channel;
    uint8_t note = s->note;
    float   bend = s->bend_semitones;
    float   xv = s->x;
    float   yv = s->y;

    portEXIT_CRITICAL(&s_ui_mutex);

    ui_xypad_event_post((uint8_t)idx, XYPAD_PHASE_UP, channel, note, bend, xv, yv);
    return idx;
}

void ui_xypad_event_post(uint8_t slot, xypad_phase_t phase, uint8_t channel,
                         uint8_t note, float bend_semitones, float x, float y)
{
    portENTER_CRITICAL(&s_event_mutex);

    // Only a pending MOVE for this slot is worth overwriting -- DOWN and UP
    // are gate transitions and must each reach Ruby on their own (see the
    // comment on ui_xypad_event_post in ui_common.h).
    if (phase == XYPAD_PHASE_MOVE) {
        for (int i = s_event_tail; i != s_event_head; i = (i + 1) % UI_EVENT_QUEUE_SIZE) {
            if (s_event_queue[i].type == UI_EVENT_XYPAD_TOUCH
             && s_event_queue[i].data.xypad.slot == slot
             && s_event_queue[i].data.xypad.phase == XYPAD_PHASE_MOVE) {
                s_event_queue[i].data.xypad.channel = channel;
                s_event_queue[i].data.xypad.note = note;
                s_event_queue[i].data.xypad.bend_semitones = bend_semitones;
                s_event_queue[i].data.xypad.x = x;
                s_event_queue[i].data.xypad.y = y;
                portEXIT_CRITICAL(&s_event_mutex);
                return;
            }
        }
    }

    int next_head = (s_event_head + 1) % UI_EVENT_QUEUE_SIZE;
    if (next_head != s_event_tail) {
        s_event_queue[s_event_head].type = UI_EVENT_XYPAD_TOUCH;
        s_event_queue[s_event_head].data.xypad.slot = slot;
        s_event_queue[s_event_head].data.xypad.phase = (uint8_t)phase;
        s_event_queue[s_event_head].data.xypad.channel = channel;
        s_event_queue[s_event_head].data.xypad.note = note;
        s_event_queue[s_event_head].data.xypad.bend_semitones = bend_semitones;
        s_event_queue[s_event_head].data.xypad.x = x;
        s_event_queue[s_event_head].data.xypad.y = y;
        s_event_head = next_head;
    }
    portEXIT_CRITICAL(&s_event_mutex);
}

// ESP-IDF Log hook for Screen Log
static vprintf_like_t s_serial_vprintf = nullptr;  // Real serial output function
static volatile int s_log_hook_active = 0;

static int ui_log_vprintf(const char* fmt, va_list args)
{
    // Prevent recursive calls
    if (s_log_hook_active) {
        // During recursion, just output to serial
        if (s_serial_vprintf) {
            return s_serial_vprintf(fmt, args);
        }
        return 0;
    }
    s_log_hook_active = 1;

    // Output to serial first
    int ret = 0;
    if (s_serial_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = s_serial_vprintf(fmt, args_copy);
        va_end(args_copy);
    }

    // Format the log message for screen log
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);

    // Remove trailing newline for screen log
    if (len > 0 && len < (int)sizeof(buf) && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    // Add to screen log (skip empty lines)
    if (buf[0] != '\0') {
        getScreenLog().addLog(buf);
    }

    s_log_hook_active = 0;

    return ret;
}

void ui_log_hook_init_with_serial(vprintf_like_t serial_vprintf)
{
    s_serial_vprintf = serial_vprintf;
    esp_log_set_vprintf(ui_log_vprintf);
}
