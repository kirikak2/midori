// MIDI Devices model: the device list, the program's IN / OUT and the saved
// patches (docs/UI_MIDI_DEVICES.md).
//
// The patches themselves are picoruby-midi's route table (MIDI.route): the
// screen reads and edits it directly, so routes a script sets up show too.
// What lives here is what the route table does not know: which transport
// bits are devices worth listing, under what name, and -- for the built-ins
// -- the stable key NVS stores patches by (bits of transports registered at
// run time, such as AMY's, change from boot to boot).
//
// Written by the PicoRuby task (register / select / restore) and the UI task
// (taps on the screen, connection polling); the table is small and guarded
// by one spinlock, NVS is written outside it.

#include "ui_common.h"
#include "midi.h"
#include "midi_route.h"
#include "midi_transport.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char* TAG = "MIDI_DEVS";

extern "C" void initialize_nvs(void);   // midori-runtime (picoruby-esp32.c)

static constexpr const char* NVS_NAMESPACE = "midi_devs";
static constexpr int SAVED_MAX = 16;
// One saved patch as text: "from>to:0+9+clock" at its longest
static constexpr int ITEM_LEN = 2 * UI_MIDIDEV_KEY_LEN + 48;
static constexpr int PATCHES_LEN = SAVED_MAX * ITEM_LEN;
static constexpr uint32_t TICK_MS = 250;

namespace {

struct Device {
    ui_mididev_t pub;
    char defaultName[UI_MIDIDEV_NAME_LEN];   // Built-ins: the name a script may override
    bool wanted;                             // Standby and something needs it
};

struct SavedPatch {
    char from[UI_MIDIDEV_KEY_LEN];
    char to[UI_MIDIDEV_KEY_LEN];
    uint32_t sel;   // MIDI_ROUTE_SEL_*
};

// The program's IN / OUT: a built-in by key (survives its bit changing),
// anything else by bit
struct Selection {
    char key[UI_MIDIDEV_KEY_LEN];
    uint8_t bit;
};

}  // namespace

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Device s_devs[UI_MIDIDEV_MAX];
static int s_count;
static Selection s_sel[2];          // [0] IN, [1] OUT
static Selection s_savedSel[2];
static SavedPatch s_saved[SAVED_MAX];
static int s_savedCount;
static bool s_loaded;
static volatile uint32_t s_serial;
static uint8_t s_connected;
static uint32_t s_lastTickMs;

static void copy_str(char* dst, const char* src, size_t len)
{
    if (!src) src = "";
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static void bump()
{
    s_serial = s_serial + 1;
}

static void post(ui_mididev_event_t kind, uint8_t from, uint8_t to, uint32_t sel)
{
    ui_event_t ev;
    ev.type = UI_EVENT_MIDI_DEVICES;
    ev.data.mididev.kind = (uint8_t)kind;
    ev.data.mididev.from = from;
    ev.data.mididev.to = to;
    ev.data.mididev.channel = (int8_t)MIDI_route_channel(sel ? sel : MIDI_ROUTE_SEL_ALL);
    ev.data.mididev.sel = sel;
    ui_event_push(&ev);
}

static bool transport_up(uint8_t bit)
{
    const midi_transport_t* t = MIDI_transport_get(bit);
    return t && t->ops && t->ops->is_connected && t->ops->is_connected(t->ctx);
}

// --- Lookups (callers hold s_mux) ----------------------------------------------

static int find_key_locked(const char* key)
{
    if (!key || !key[0]) return -1;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_devs[i].pub.key, key) == 0) return i;
    }
    return -1;
}

static int find_bit_locked(uint8_t bit)
{
    if (bit == 0) return -1;
    for (int i = 0; i < s_count; i++) {
        if (s_devs[i].pub.bit == bit) return i;
    }
    return -1;
}

static int resolve_locked(const Selection& sel)
{
    return sel.key[0] ? find_key_locked(sel.key) : find_bit_locked(sel.bit);
}

static void set_selection_locked(Selection& sel, int index)
{
    if (index < 0 || index >= s_count) {
        sel.key[0] = '\0';
        sel.bit = 0;
        return;
    }
    const Device& d = s_devs[index];
    if (d.pub.flags & UI_MIDIDEV_BUILTIN) {
        copy_str(sel.key, d.pub.key, sizeof(sel.key));
        sel.bit = 0;
    } else {
        sel.key[0] = '\0';
        sel.bit = d.pub.bit;
    }
}

// --- NVS -----------------------------------------------------------------------
// "patches": "from>to[:sel],..." by key, sel being channels 0..15 and / or
// "clock" joined by '+' (none: everything); "in" / "out": the selection's key.

// "0+9+clock" -> its MIDI_ROUTE_SEL_* bits; everything when nothing valid
static uint32_t parse_selection(char* text)
{
    uint32_t sel = 0;
    char* save = nullptr;
    for (char* tok = strtok_r(text, "+", &save); tok; tok = strtok_r(nullptr, "+", &save)) {
        if (strcmp(tok, "clock") == 0) {
            sel |= MIDI_ROUTE_SEL_CLOCK;
        } else if (tok[0] >= '0' && tok[0] <= '9') {
            int ch = atoi(tok);
            if (ch >= 0 && ch <= 15) sel |= 1u << ch;
        }
    }
    return sel ? sel : MIDI_ROUTE_SEL_ALL;
}

static void format_selection(uint32_t sel, char* buf, size_t len)
{
    size_t pos = 0;
    buf[0] = '\0';
    for (int ch = 0; ch <= 16; ch++) {
        uint32_t bit = (ch < 16) ? (1u << ch) : MIDI_ROUTE_SEL_CLOCK;
        if (!(sel & bit)) continue;
        int n = (ch < 16) ? snprintf(buf + pos, len - pos, "%s%d", pos ? "+" : "", ch)
                          : snprintf(buf + pos, len - pos, "%sclock", pos ? "+" : "");
        if (n < 0 || pos + (size_t)n >= len) break;
        pos += (size_t)n;
    }
}

static void parse_patches(const char* text)
{
    s_savedCount = 0;
    const char* p = text;
    while (*p && s_savedCount < SAVED_MAX) {
        const char* end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char item[ITEM_LEN];
        if (len < sizeof(item)) {
            memcpy(item, p, len);
            item[len] = '\0';
            char* gt = strchr(item, '>');
            if (gt) {
                *gt = '\0';
                char* to = gt + 1;
                uint32_t sel = MIDI_ROUTE_SEL_ALL;
                char* colon = strchr(to, ':');
                if (colon) {
                    *colon = '\0';
                    sel = parse_selection(colon + 1);
                }
                SavedPatch& sp = s_saved[s_savedCount++];
                copy_str(sp.from, item, sizeof(sp.from));
                copy_str(sp.to, to, sizeof(sp.to));
                sp.sel = sel;
            }
        }
        if (!end) break;
        p = end + 1;
    }
}

static void load_saved()
{
    if (s_loaded) return;
    s_loaded = true;
    initialize_nvs();
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;  // Nothing saved yet
    char* buf = new char[PATCHES_LEN];   // Kept off the caller's stack
    size_t len = PATCHES_LEN;
    if (nvs_get_str(h, "patches", buf, &len) == ESP_OK) parse_patches(buf);
    delete[] buf;
    len = sizeof(s_savedSel[0].key);
    if (nvs_get_str(h, "in", s_savedSel[0].key, &len) != ESP_OK) s_savedSel[0].key[0] = '\0';
    len = sizeof(s_savedSel[1].key);
    if (nvs_get_str(h, "out", s_savedSel[1].key, &len) != ESP_OK) s_savedSel[1].key[0] = '\0';
    nvs_close(h);
}

// Snapshot of what to write, taken under the lock
struct SavedSnapshot {
    char patches[PATCHES_LEN];
    char in[UI_MIDIDEV_KEY_LEN];
    char out[UI_MIDIDEV_KEY_LEN];
};

static void snapshot_locked(SavedSnapshot& snap)
{
    size_t pos = 0;
    snap.patches[0] = '\0';
    for (int i = 0; i < s_savedCount; i++) {
        char item[ITEM_LEN];
        if (s_saved[i].sel & MIDI_ROUTE_SEL_ALL) {
            snprintf(item, sizeof(item), "%.11s>%.11s", s_saved[i].from, s_saved[i].to);
        } else {
            char sel[ITEM_LEN - 2 * UI_MIDIDEV_KEY_LEN];
            format_selection(s_saved[i].sel, sel, sizeof(sel));
            snprintf(item, sizeof(item), "%.11s>%.11s:%s", s_saved[i].from, s_saved[i].to, sel);
        }
        size_t need = strlen(item) + (pos ? 1 : 0);
        if (pos + need >= sizeof(snap.patches)) break;
        if (pos) snap.patches[pos++] = ',';
        strcpy(snap.patches + pos, item);
        pos += strlen(item);
    }
    copy_str(snap.in, s_savedSel[0].key, sizeof(snap.in));
    copy_str(snap.out, s_savedSel[1].key, sizeof(snap.out));
}

static void write_saved(const SavedSnapshot& snap)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed; patches not saved");
        return;
    }
    nvs_set_str(h, "patches", snap.patches);
    if (snap.in[0]) nvs_set_str(h, "in", snap.in); else nvs_erase_key(h, "in");
    if (snap.out[0]) nvs_set_str(h, "out", snap.out); else nvs_erase_key(h, "out");
    nvs_commit(h);
    nvs_close(h);
}

static void save_now()
{
    SavedSnapshot* snap = new SavedSnapshot;   // ~1.2KB, kept off the UI task's stack
    portENTER_CRITICAL(&s_mux);
    snapshot_locked(*snap);
    portEXIT_CRITICAL(&s_mux);
    write_saved(*snap);
    delete snap;
}

static int saved_find_locked(const char* from, const char* to)
{
    for (int i = 0; i < s_savedCount; i++) {
        if (strcmp(s_saved[i].from, from) == 0 && strcmp(s_saved[i].to, to) == 0) return i;
    }
    return -1;
}

static void saved_remove_locked(int i)
{
    for (int j = i; j < s_savedCount - 1; j++) s_saved[j] = s_saved[j + 1];
    s_savedCount--;
}

// --- Registering -----------------------------------------------------------------

int ui_mididev_register(const char* key, const char* name, uint8_t bit,
                        uint16_t color, bool builtin)
{
    load_saved();
    int index;
    portENTER_CRITICAL(&s_mux);
    index = builtin ? find_key_locked(key) : -1;
    if (index < 0) index = find_bit_locked(bit);
    if (index < 0 && s_count < UI_MIDIDEV_MAX) {
        index = s_count++;
        Device& d = s_devs[index];
        memset(&d, 0, sizeof(d));
        if (builtin) {
            d.pub.flags = UI_MIDIDEV_BUILTIN;
            copy_str(d.pub.key, key, sizeof(d.pub.key));
            copy_str(d.defaultName, name, sizeof(d.defaultName));
        }
    }
    if (index >= 0) {
        Device& d = s_devs[index];
        if (bit != 0) {
            d.pub.bit = bit;
            d.pub.flags &= (uint8_t)~UI_MIDIDEV_STANDBY;
            d.wanted = false;
        } else if (d.pub.bit == 0) {
            d.pub.flags |= UI_MIDIDEV_STANDBY;
        }
        if (name && name[0]) copy_str(d.pub.name, name, sizeof(d.pub.name));
        if (!d.pub.name[0]) copy_str(d.pub.name, d.pub.key, sizeof(d.pub.name));
        d.pub.color = color;
    }
    portEXIT_CRITICAL(&s_mux);
    if (index >= 0) bump();
    return index;
}

bool ui_mididev_unregister(uint8_t bit)
{
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_bit_locked(bit);
    if (i >= 0) {
        for (int j = i; j < s_count - 1; j++) s_devs[j] = s_devs[j + 1];
        s_count--;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (found) bump();
    return found;
}

static void route_saved(const char* only_key);

bool ui_mididev_activate(const char* key, uint8_t bit)
{
    if (bit == 0) return false;
    bool found = false, was_standby = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_key_locked(key);
    if (i >= 0) {
        was_standby = (s_devs[i].pub.flags & UI_MIDIDEV_STANDBY) != 0;
        s_devs[i].pub.bit = bit;
        s_devs[i].pub.flags &= (uint8_t)~UI_MIDIDEV_STANDBY;
        s_devs[i].wanted = false;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    // Its saved patches could not be routed without a bit: route them now,
    // and only them (the others are as the script left them), and their chains
    if (was_standby) {
        route_saved(key);
        ui_filter_restore(key);
    }
    if (found) bump();
    return found;
}

void ui_mididev_set_no_out(const char* key)
{
    portENTER_CRITICAL(&s_mux);
    int i = find_key_locked(key);
    if (i >= 0) s_devs[i].pub.flags |= UI_MIDIDEV_NO_OUT;
    portEXIT_CRITICAL(&s_mux);
    bump();
}

// --- Reading -----------------------------------------------------------------------

int ui_mididev_count(void)
{
    return s_count;
}

bool ui_mididev_get(int index, ui_mididev_t* out)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (index >= 0 && index < s_count && out) {
        *out = s_devs[index].pub;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

bool ui_mididev_builtin_key(uint8_t bit, char* key, int len)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_bit_locked(bit);
    if (i >= 0 && (s_devs[i].pub.flags & UI_MIDIDEV_BUILTIN)) {
        copy_str(key, s_devs[i].pub.key, (size_t)len);
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

uint8_t ui_mididev_bit_of_key(const char* key)
{
    portENTER_CRITICAL(&s_mux);
    int i = find_key_locked(key);
    uint8_t bit = i >= 0 ? s_devs[i].pub.bit : 0;
    portEXIT_CRITICAL(&s_mux);
    return bit;
}

int ui_mididev_find_bit(uint8_t bit)
{
    portENTER_CRITICAL(&s_mux);
    int i = find_bit_locked(bit);
    portEXIT_CRITICAL(&s_mux);
    return i;
}

uint8_t ui_mididev_connected_mask(void)
{
    return s_connected;
}

uint32_t ui_mididev_serial(void)
{
    return s_serial;
}

// --- Selection -------------------------------------------------------------------

int ui_mididev_selected(bool out)
{
    portENTER_CRITICAL(&s_mux);
    int i = resolve_locked(s_sel[out ? 1 : 0]);
    portEXIT_CRITICAL(&s_mux);
    return i;
}

void ui_mididev_select(bool out, int index, bool save)
{
    load_saved();
    int k = out ? 1 : 0;
    uint8_t bit = 0;
    bool changed;
    bool saveable = true;
    portENTER_CRITICAL(&s_mux);
    int before = resolve_locked(s_sel[k]);
    set_selection_locked(s_sel[k], index);
    changed = (before != resolve_locked(s_sel[k]));
    if (index >= 0 && index < s_count) {
        Device& d = s_devs[index];
        bit = d.pub.bit;
        if (d.pub.flags & UI_MIDIDEV_STANDBY) d.wanted = true;
        saveable = (d.pub.flags & UI_MIDIDEV_BUILTIN) != 0;
    }
    if (save && saveable) set_selection_locked(s_savedSel[k], index);
    portEXIT_CRITICAL(&s_mux);
    if (save && saveable) save_now();
    if (changed) {
        bump();
        post(out ? UI_MIDIDEV_EVENT_SELECT_OUT : UI_MIDIDEV_EVENT_SELECT_IN, bit, 0, 0);
    }
}

// --- Patches -------------------------------------------------------------------------

uint32_t ui_mididev_patch_selection(int from, int to)
{
    ui_mididev_t a, b;
    if (!ui_mididev_get(from, &a) || !ui_mididev_get(to, &b)) return 0;
    if (a.bit && b.bit) {
        // Every route from a to b: a script may have routed channels separately
        uint32_t sel = 0;
        for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
            uint8_t src, dst;
            uint32_t s;
            if (!MIDI_route_get_selection(i, &src, &dst, &s)) continue;
            if (src != a.bit || !(dst & b.bit)) continue;
            if (s & MIDI_ROUTE_SEL_ALL) return MIDI_ROUTE_SEL_ALL;
            sel |= s;
        }
        return sel;
    }
    // A standby end: the saved patch is all there is
    uint32_t sel = 0;
    portENTER_CRITICAL(&s_mux);
    int i = saved_find_locked(a.key, b.key);
    if (i >= 0) sel = s_saved[i].sel;
    portEXIT_CRITICAL(&s_mux);
    return sel;
}

void ui_mididev_set_patch(int from, int to, uint32_t sel)
{
    ui_mididev_t a, b;
    if (from == to || !ui_mididev_get(from, &a) || !ui_mididev_get(to, &b)) return;
    load_saved();
    bool add = (sel != 0);
    bool saveable = (a.flags & UI_MIDIDEV_BUILTIN) && (b.flags & UI_MIDIDEV_BUILTIN);

    if (a.bit && b.bit) {
        MIDI_route_remove(a.bit, b.bit);
        // The patch's filters go with it (Ruby takes them down, notes off)
        if (!add) ui_filter_drop(a.bit, b.bit, saveable);
        if (add) {
            if (MIDI_route_add_selection(a.bit, b.bit, sel) != 0) {
                ESP_LOGW(TAG, "Route table full");
                add = false;
            } else {
                MIDI_route_start_source(a.bit);
            }
        }
    }

    // A standby end: only the saved chain there is to take off
    if (!add && saveable && !(a.bit && b.bit)) ui_filter_saved_drop(a.key, b.key);

    if (saveable) {
        portENTER_CRITICAL(&s_mux);
        int i = saved_find_locked(a.key, b.key);
        if (i >= 0) saved_remove_locked(i);
        if (add && s_savedCount < SAVED_MAX) {
            SavedPatch& sp = s_saved[s_savedCount++];
            copy_str(sp.from, a.key, sizeof(sp.from));
            copy_str(sp.to, b.key, sizeof(sp.to));
            sp.sel = sel;
        }
        // A standby end starts once Ruby sees it is wanted
        if (add) {
            if (a.flags & UI_MIDIDEV_STANDBY) s_devs[from].wanted = true;
            if (b.flags & UI_MIDIDEV_STANDBY) s_devs[to].wanted = true;
        }
        portEXIT_CRITICAL(&s_mux);
        save_now();
    }

    bump();
    post(add ? UI_MIDIDEV_EVENT_PATCH : UI_MIDIDEV_EVENT_UNPATCH, a.bit, b.bit, add ? sel : 0);
}

void ui_mididev_toggle_patch(int from, int to)
{
    // A patch comes off whatever it passes; a new one passes everything
    ui_mididev_set_patch(from, to, ui_mididev_patch_selection(from, to) ? 0 : MIDI_ROUTE_SEL_ALL);
}

// Route the saved patches (those involving only_key, or all for NULL); a
// standby end is marked wanted instead. Returns how many were routed.
static int route_saved_count(const char* only_key)
{
    int applied = 0;
    uint8_t sources = 0;

    SavedPatch saved[SAVED_MAX];
    portENTER_CRITICAL(&s_mux);
    int n = s_savedCount;
    memcpy(saved, s_saved, sizeof(SavedPatch) * n);
    portEXIT_CRITICAL(&s_mux);

    for (int p = 0; p < n; p++) {
        if (only_key && strcmp(saved[p].from, only_key) != 0 && strcmp(saved[p].to, only_key) != 0) {
            continue;
        }
        uint8_t src = 0, dst = 0;
        portENTER_CRITICAL(&s_mux);
        int a = find_key_locked(saved[p].from);
        int b = find_key_locked(saved[p].to);
        if (a >= 0 && b >= 0) {
            src = s_devs[a].pub.bit;
            dst = s_devs[b].pub.bit;
            if (!src) s_devs[a].wanted = true;
            if (!dst) s_devs[b].wanted = true;
        }
        portEXIT_CRITICAL(&s_mux);
        // A device this board does not have, or one still on standby
        if (!src || !dst) continue;
        if (MIDI_route_add_selection(src, dst, saved[p].sel) == 0) {
            sources |= src;
            applied++;
        }
    }
    for (uint8_t bit = 1; bit; bit <<= 1) {
        if (sources & bit) MIDI_route_start_source(bit);
    }
    return applied;
}

static void route_saved(const char* only_key)
{
    route_saved_count(only_key);
}

int ui_mididev_restore(void)
{
    load_saved();
    portENTER_CRITICAL(&s_mux);
    s_sel[0] = s_savedSel[0];
    s_sel[1] = s_savedSel[1];
    // A selected standby device is wanted too
    for (int k = 0; k < 2; k++) {
        int i = resolve_locked(s_sel[k]);
        if (i >= 0 && (s_devs[i].pub.flags & UI_MIDIDEV_STANDBY)) s_devs[i].wanted = true;
    }
    portEXIT_CRITICAL(&s_mux);
    int applied = route_saved_count(nullptr);
    ui_filter_restore(nullptr);
    bump();
    return applied;
}

bool ui_mididev_save(void)
{
    load_saved();
    portENTER_CRITICAL(&s_mux);
    // Saved patches with a standby end are not in the route table: keep them
    int keep = 0;
    for (int i = 0; i < s_savedCount; i++) {
        int a = find_key_locked(s_saved[i].from);
        int b = find_key_locked(s_saved[i].to);
        bool standby = (a >= 0 && (s_devs[a].pub.flags & UI_MIDIDEV_STANDBY)) ||
                       (b >= 0 && (s_devs[b].pub.flags & UI_MIDIDEV_STANDBY));
        if (standby) s_saved[keep++] = s_saved[i];
    }
    s_savedCount = keep;
    for (int r = 0; r < MIDI_ROUTE_MAX; r++) {
        uint8_t src, dst;
        uint32_t sel;
        if (!MIDI_route_get_selection(r, &src, &dst, &sel)) continue;
        int a = find_bit_locked(src);
        if (a < 0 || !(s_devs[a].pub.flags & UI_MIDIDEV_BUILTIN)) continue;
        for (uint8_t bit = 1; bit; bit <<= 1) {
            if (!(dst & bit)) continue;
            int b = find_bit_locked(bit);
            if (b < 0 || !(s_devs[b].pub.flags & UI_MIDIDEV_BUILTIN)) continue;
            // Routes of the same pair (a script's, one per channel) fold into one
            int k = saved_find_locked(s_devs[a].pub.key, s_devs[b].pub.key);
            if (k >= 0) {
                if ((sel | s_saved[k].sel) & MIDI_ROUTE_SEL_ALL) s_saved[k].sel = MIDI_ROUTE_SEL_ALL;
                else s_saved[k].sel |= sel;
                continue;
            }
            if (s_savedCount >= SAVED_MAX) break;
            SavedPatch& sp = s_saved[s_savedCount++];
            copy_str(sp.from, s_devs[a].pub.key, sizeof(sp.from));
            copy_str(sp.to, s_devs[b].pub.key, sizeof(sp.to));
            sp.sel = sel;
        }
    }
    // The selection too, when it is a built-in
    for (int k = 0; k < 2; k++) {
        int i = resolve_locked(s_sel[k]);
        if (i < 0 || (s_devs[i].pub.flags & UI_MIDIDEV_BUILTIN)) {
            set_selection_locked(s_savedSel[k], i);
        }
    }
    portEXIT_CRITICAL(&s_mux);
    save_now();
    ui_filter_save_live();
    return true;
}

bool ui_mididev_take_wanted(char* key, int len)
{
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < s_count; i++) {
        if (s_devs[i].wanted && (s_devs[i].pub.flags & UI_MIDIDEV_STANDBY)) {
            s_devs[i].wanted = false;
            copy_str(key, s_devs[i].pub.key, (size_t)len);
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return found;
}

// --- Housekeeping ----------------------------------------------------------------------

void ui_mididev_tick(void)
{
    ui_filter_tick();
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_lastTickMs < TICK_MS) return;
    s_lastTickMs = now;

    uint8_t bits[UI_MIDIDEV_MAX];
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < s_count; i++) bits[n++] = s_devs[i].pub.bit;
    portEXIT_CRITICAL(&s_mux);

    uint8_t up = 0;
    for (int i = 0; i < n; i++) {
        if (bits[i] && transport_up(bits[i])) up |= bits[i];
    }
    uint8_t changed = up ^ s_connected;
    if (!changed) return;
    s_connected = up;
    bump();
    for (uint8_t bit = 1; bit; bit <<= 1) {
        if (changed & bit) {
            post((up & bit) ? UI_MIDIDEV_EVENT_CONNECT : UI_MIDIDEV_EVENT_DISCONNECT, bit, 0, 0);
        }
    }
}

void ui_mididev_cleanup(void)
{
    portENTER_CRITICAL(&s_mux);
    // The script's own devices go; built-ins get their names back
    int keep = 0;
    for (int i = 0; i < s_count; i++) {
        Device& d = s_devs[i];
        if (!(d.pub.flags & UI_MIDIDEV_BUILTIN)) continue;
        copy_str(d.pub.name, d.defaultName, sizeof(d.pub.name));
        d.wanted = false;
        s_devs[keep++] = d;
    }
    s_count = keep;
    // The script's IN / OUT choice was its own
    s_sel[0] = s_savedSel[0];
    s_sel[1] = s_savedSel[1];
    portEXIT_CRITICAL(&s_mux);
    // The script's filters went with its VM
    ui_filter_cleanup();
    bump();
}
