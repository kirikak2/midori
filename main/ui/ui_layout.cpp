#include "ui_layout.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* TAG = "UI_LAYOUT";

// Registry of layouts (docs/UI_LAYOUT_DSL.md). Layouts are heap-allocated on
// commit (~2.5KB each) rather than kept in .bss: large static buffers in this
// UI have broken the display before (docs/MEMORY_ALLOCATION.md). The table is
// indexed by layout id and grows as ids get larger; there is no fixed number
// of layouts.

static LayoutDef** s_layouts = nullptr;
static int s_capacity = 0;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lockBuf;

// Bumped when a script ends. A layout from an older generation is dead even
// before collect() frees it: its Ruby callbacks went with the script's VM.
// Bumping is all ui_layout_cleanup() does, so it is safe from any task,
// including the esp_timer one the MIDI clock runs the cleanup hook on.
static volatile uint32_t s_gen = 1;
static portMUX_TYPE s_genMux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_versionCounter = 0;
static int s_lastShown = -1;

// The layout being defined (PicoRuby task only), and the slot it is for
static LayoutDef* s_building = nullptr;
static int s_buildingSlot = -1;

namespace ui_layout {

void init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lockBuf);
    }
}

void lock(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

LayoutDef* get(int slot)
{
    if (slot < 0 || slot >= s_capacity) return nullptr;
    LayoutDef* def = s_layouts[slot];
    if (!def || def->gen != s_gen) return nullptr;
    return def;
}

uint32_t take_dirty(LayoutDef* def)
{
    uint32_t d = def->dirty;
    def->dirty = 0;
    return d;
}

void set_last_shown(int slot)
{
    s_lastShown = slot;
}

void collect(void)
{
    if (!s_lock) return;
    lock();
    for (int i = 0; i < s_capacity; i++) {
        if (s_layouts[i] && s_layouts[i]->gen != s_gen) {
            free(s_layouts[i]);
            s_layouts[i] = nullptr;
        }
    }
    unlock();
}

}  // namespace ui_layout

using namespace ui_layout;

// --- Queries ----------------------------------------------------------------

bool ui_layout_exists(int slot)
{
    if (!s_lock) return false;
    lock();
    bool exists = get(slot) != nullptr;
    unlock();
    return exists;
}

int ui_layout_last_shown(void)
{
    if (ui_layout_exists(s_lastShown)) return s_lastShown;
    return ui_layout_next(-1);
}

int ui_layout_next(int after)
{
    if (!s_lock) return -1;
    int found = -1;
    lock();
    for (int i = after < 0 ? 0 : after + 1; i < s_capacity; i++) {
        if (get(i)) {
            found = i;
            break;
        }
    }
    unlock();
    return found;
}

int ui_layout_count(void)
{
    if (!s_lock) return 0;
    int n = 0;
    lock();
    for (int i = 0; i < s_capacity; i++) {
        if (get(i)) n++;
    }
    unlock();
    return n;
}

void ui_layout_title(int slot, char* buf, int len)
{
    if (len <= 0) return;
    buf[0] = '\0';
    if (!s_lock) return;
    lock();
    LayoutDef* def = get(slot);
    if (def) snprintf(buf, len, "%s", def->title);
    unlock();
}

void ui_layout_cleanup(void)
{
    portENTER_CRITICAL(&s_genMux);
    s_gen = s_gen + 1;
    portEXIT_CRITICAL(&s_genMux);
}

// --- Definition -------------------------------------------------------------

bool ui_layout_begin(int slot, const char* title)
{
    if (slot < 0 || slot > UI_LAYOUT_MAX_ID) return false;

    free(s_building);
    s_building = (LayoutDef*)calloc(1, sizeof(LayoutDef));
    if (!s_building) {
        ESP_LOGE(TAG, "Failed to allocate a layout");
        s_buildingSlot = -1;
        return false;
    }
    snprintf(s_building->title, sizeof(s_building->title), "%s", title ? title : "");
    s_buildingSlot = slot;
    return true;
}

static ui_widget_t* add_widget(uint8_t kind, int x, int y, int w, int h, int* id)
{
    *id = -1;
    if (!s_building || s_building->count >= UI_LAYOUT_MAX_WIDGETS) return nullptr;
    if (w <= 0 || h <= 0) return nullptr;
    ui_widget_t* wd = &s_building->widgets[s_building->count];
    memset(wd, 0, sizeof(*wd));
    wd->kind = kind;
    wd->x = (int16_t)x;
    wd->y = (int16_t)y;
    wd->w = (int16_t)w;
    wd->h = (int16_t)h;
    *id = s_building->count++;
    return wd;
}

int ui_layout_add_pad(int x, int y, int w, int h, const char* label,
                      uint16_t color, uint8_t type)
{
    int id;
    ui_widget_t* wd = add_widget(UI_WIDGET_PAD, x, y, w, h, &id);
    if (!wd) return -1;
    wd->pad.assigned = true;
    snprintf(wd->pad.label, sizeof(wd->pad.label), "%s", label ? label : "");
    wd->pad.color = (pad_color_t)color;
    wd->pad.type = (pad_type_t)type;
    wd->pad.state = false;
    return id;
}

int ui_layout_add_knob(int x, int y, int w, int h, const char* label,
                       uint16_t color, float min, float max, float step,
                       float value, uint8_t origin, float sensitivity,
                       bool notify)
{
    int id;
    ui_widget_t* wd = add_widget(UI_WIDGET_KNOB, x, y, w, h, &id);
    if (!wd) return -1;
    knob_config_t& k = wd->knob;
    k.assigned = true;
    snprintf(k.label, sizeof(k.label), "%s", label ? label : "");
    k.color = color;
    k.min = min;
    k.max = max;
    k.step = step;
    k.origin = origin;
    k.sensitivity = sensitivity;
    k.notify = notify;
    k.value = ui_knob_quantize(&k, value);
    k.initial = k.value;
    return id;
}

int ui_layout_add_view(uint8_t kind, int x, int y, int w, int h)
{
    if (kind != UI_WIDGET_XYPAD && kind != UI_WIDGET_TOMBOLA && kind != UI_WIDGET_KEYS
        && kind != UI_WIDGET_TEMPO && kind != UI_WIDGET_SEQUENCER) return -1;
    // One of each per layout: there is one model behind each view
    if (s_building) {
        for (int i = 0; i < s_building->count; i++) {
            if (s_building->widgets[i].kind == kind) return -1;
        }
    }
    int id;
    return add_widget(kind, x, y, w, h, &id) ? id : -1;
}

bool ui_layout_commit(void)
{
    if (!s_building || s_buildingSlot < 0 || !s_lock) return false;

    LayoutDef* def = s_building;
    int slot = s_buildingSlot;
    s_building = nullptr;
    s_buildingSlot = -1;

    lock();
    if (slot >= s_capacity) {
        int cap = s_capacity ? s_capacity : 8;
        while (cap <= slot) cap *= 2;
        LayoutDef** grown = (LayoutDef**)realloc(s_layouts, sizeof(LayoutDef*) * cap);
        if (!grown) {
            unlock();
            ESP_LOGE(TAG, "Failed to grow the layout table to %d", cap);
            free(def);
            return false;
        }
        memset(grown + s_capacity, 0, sizeof(LayoutDef*) * (cap - s_capacity));
        s_layouts = grown;
        s_capacity = cap;
    }
    LayoutDef* old = s_layouts[slot];
    def->gen = s_gen;
    def->version = ++s_versionCounter;
    s_layouts[slot] = def;
    unlock();

    // Nobody can be using the old one: every user holds the lock
    free(old);
    return true;
}

void ui_layout_delete(int slot)
{
    if (slot < 0 || slot >= s_capacity || !s_lock) return;
    lock();
    LayoutDef* old = s_layouts[slot];
    s_layouts[slot] = nullptr;
    unlock();
    free(old);
}

// --- Widgets ----------------------------------------------------------------

// The widget, if it exists and is of that kind. Lock held.
static ui_widget_t* widget_at(int slot, int id, int kind, LayoutDef** defOut)
{
    LayoutDef* def = get(slot);
    if (!def || id < 0 || id >= def->count) return nullptr;
    ui_widget_t* wd = &def->widgets[id];
    if (kind >= 0 && wd->kind != kind) return nullptr;
    if (defOut) *defOut = def;
    return wd;
}

bool ui_layout_pad_state(int slot, int id)
{
    if (!s_lock) return false;
    lock();
    ui_widget_t* wd = widget_at(slot, id, UI_WIDGET_PAD, nullptr);
    bool state = wd ? wd->pad.state : false;
    unlock();
    return state;
}

void ui_layout_set_label(int slot, int id, const char* label)
{
    if (!s_lock) return;
    lock();
    LayoutDef* def;
    ui_widget_t* wd = widget_at(slot, id, -1, &def);
    if (wd && wd->kind == UI_WIDGET_PAD) {
        snprintf(wd->pad.label, sizeof(wd->pad.label), "%s", label ? label : "");
        def->dirty |= (1u << id);
    } else if (wd && wd->kind == UI_WIDGET_KNOB) {
        snprintf(wd->knob.label, sizeof(wd->knob.label), "%s", label ? label : "");
        def->dirty |= (1u << id);
    }
    unlock();
}

void ui_layout_set_color(int slot, int id, uint16_t color)
{
    if (!s_lock) return;
    lock();
    LayoutDef* def;
    ui_widget_t* wd = widget_at(slot, id, -1, &def);
    if (wd && wd->kind == UI_WIDGET_PAD) {
        wd->pad.color = (pad_color_t)color;
        def->dirty |= (1u << id);
    } else if (wd && wd->kind == UI_WIDGET_KNOB) {
        wd->knob.color = color;
        def->dirty |= (1u << id);
    }
    unlock();
}

float ui_layout_knob_value(int slot, int id)
{
    if (!s_lock) return 0.0f;
    lock();
    ui_widget_t* wd = widget_at(slot, id, UI_WIDGET_KNOB, nullptr);
    float v = wd ? wd->knob.value : 0.0f;
    unlock();
    return v;
}

bool ui_layout_knob_set_value(int slot, int id, float value)
{
    if (!s_lock) return false;
    lock();
    LayoutDef* def;
    ui_widget_t* wd = widget_at(slot, id, UI_WIDGET_KNOB, &def);
    bool changed = false;
    if (wd) {
        float q = ui_knob_quantize(&wd->knob, value);
        if (q != wd->knob.value) {
            wd->knob.value = q;
            def->dirty |= (1u << id);
            changed = true;
        }
    }
    unlock();
    return changed;
}

bool ui_layout_knob_reset(int slot, int id)
{
    if (!s_lock) return false;
    lock();
    ui_widget_t* wd = widget_at(slot, id, UI_WIDGET_KNOB, nullptr);
    float initial = wd ? wd->knob.initial : 0.0f;
    bool changed = wd ? ui_layout_knob_set_value(slot, id, initial) : false;
    unlock();
    return changed;
}
