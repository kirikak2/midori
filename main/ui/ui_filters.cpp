// Named filters as the MIDI Devices screen sees them
// (docs/MIDI_FILTER_REGISTRY.md).
//
// The filters run in Ruby (MIDI::Filter, picoruby-midi). What lives here:
//   - the registered filters: names, labels and declared parameters, which
//     Ruby defines as it registers them, for the screen's Filters tab and
//     parameter page
//   - the chain on each patch (live, by transport bits). Ruby mirrors every
//     change of its own here; the screen changes them here and Ruby, seeing
//     the revision move, applies them (UI::MIDIDevices._poll)
//   - the saved chains of built-in pairs (by key, NVS "chains")
//
// Written by the PicoRuby task and the UI task. One spinlock guards the
// tables; text parsing, formatting and NVS happen outside it. The tables are
// ~30KB together, allocated on first use (docs/MEMORY_ALLOCATION.md).

#include "ui_common.h"
#include "midi_route.h"
#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char* TAG = "UI_FILTERS";

extern "C" void initialize_nvs(void);   // midori-runtime (picoruby-esp32.c)

static constexpr const char* NVS_NAMESPACE = "midi_devs";
static constexpr const char* NVS_KEY = "chains";
static constexpr int NVS_TEXT_MAX = 4000;          // NVS's limit for a string
static constexpr uint32_t SAVE_DELAY_MS = 3000;    // After a panel's last change

namespace {

struct Live {
    uint8_t src, dst;     // 0: unused
    uint32_t rev;
    ui_filter_chain_t chain;
};

struct Saved {
    char from[UI_MIDIDEV_KEY_LEN];   // Empty: unused
    char to[UI_MIDIDEV_KEY_LEN];
    ui_filter_chain_t chain;
};

struct Tables {
    ui_filter_def_t defs[UI_FILTER_MAX];
    int defCount;
    Live live[UI_FILTER_CHAINS];
    Saved saved[UI_FILTER_CHAINS];
};

}  // namespace

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Tables* s_t = nullptr;
static bool s_loaded;
static uint32_t s_rev;
static volatile uint32_t s_serial;
static volatile bool s_saveDue;
static uint32_t s_saveAtMs;

static void copy_str(char* dst, const char* src, size_t len)
{
    if (!src) src = "";
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static uint32_t now_ms()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void bump()
{
    s_serial = s_serial + 1;
}

static void load_saved();

// The tables, made on first use (in PSRAM when there is some)
static bool tables()
{
    if (s_t) return true;
    Tables* t = (Tables*)heap_caps_calloc(1, sizeof(Tables), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!t) t = (Tables*)calloc(1, sizeof(Tables));
    if (!t) {
        ESP_LOGE(TAG, "No memory for the filter tables");
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    if (!s_t) {
        s_t = t;
        t = nullptr;
    }
    portEXIT_CRITICAL(&s_mux);
    free(t);
    load_saved();
    return true;
}

// --- Text ------------------------------------------------------------------------
// A chain: "name(param:value,param:value),name2" -- stages by ',', each
// one's values in parentheses (none: no parentheses).

int ui_filter_chain_format(const ui_filter_chain_t* chain, char* buf, int len)
{
    int pos = 0;
    if (len <= 0) return 0;
    buf[0] = '\0';
    for (int i = 0; i < chain->len; i++) {
        const ui_filter_stage_t& st = chain->stages[i];
        int n = snprintf(buf + pos, len - pos, "%s%s", i ? "," : "", st.name);
        if (n < 0 || pos + n >= len) break;
        pos += n;
        for (int v = 0; v < st.nvalues; v++) {
            n = snprintf(buf + pos, len - pos, "%s%s:%d", v ? "," : "(", st.pnames[v], st.values[v]);
            if (n < 0 || pos + n >= len) return pos;
            pos += n;
        }
        if (st.nvalues > 0) {
            if (pos + 1 >= len) break;
            buf[pos++] = ')';
            buf[pos] = '\0';
        }
    }
    return pos;
}

bool ui_filter_chain_parse(const char* text, ui_filter_chain_t* out)
{
    memset(out, 0, sizeof(*out));
    const char* p = text ? text : "";
    while (*p) {
        if (out->len >= UI_FILTER_CHAIN_MAX) break;
        ui_filter_stage_t& st = out->stages[out->len];
        int n = 0;
        while (*p && *p != ',' && *p != '(') {
            if (n < UI_FILTER_NAME_LEN - 1) st.name[n++] = *p;
            p++;
        }
        st.name[n] = '\0';
        if (*p == '(') {
            p++;
            while (*p && *p != ')') {
                char pname[UI_FILTER_LABEL_LEN];
                int k = 0;
                while (*p && *p != ':' && *p != ')' && *p != ',') {
                    if (k < UI_FILTER_LABEL_LEN - 1) pname[k++] = *p;
                    p++;
                }
                pname[k] = '\0';
                int value = 0;
                if (*p == ':') value = (int)strtol(p + 1, (char**)&p, 10);
                if (k > 0 && st.nvalues < UI_FILTER_PARAM_MAX) {
                    copy_str(st.pnames[st.nvalues], pname, UI_FILTER_LABEL_LEN);
                    st.values[st.nvalues++] = (int16_t)value;
                }
                while (*p && *p != ',' && *p != ')') p++;
                if (*p == ',') p++;
            }
            if (*p == ')') p++;
        }
        if (st.name[0]) out->len++;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return true;
}

static bool same_names(const ui_filter_chain_t& a, const ui_filter_chain_t& b)
{
    if (a.len != b.len) return false;
    for (int i = 0; i < a.len; i++) {
        if (strcmp(a.stages[i].name, b.stages[i].name) != 0) return false;
    }
    return true;
}

// --- NVS -------------------------------------------------------------------------
// "chains": "from>to=<chain>;from>to=<chain>" by built-in key

static void load_saved()
{
    if (s_loaded || !s_t) return;
    s_loaded = true;
    initialize_nvs();
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    char* buf = (char*)malloc(NVS_TEXT_MAX);
    if (!buf) {
        nvs_close(h);
        return;
    }
    // Off the stack: the UI task (whose stack is small) may be the first user
    Saved* sv = (Saved*)malloc(sizeof(Saved));
    size_t len = NVS_TEXT_MAX;
    if (sv && nvs_get_str(h, NVS_KEY, buf, &len) == ESP_OK) {
        int count = 0;
        char* save = nullptr;
        for (char* item = strtok_r(buf, ";", &save); item && count < UI_FILTER_CHAINS;
             item = strtok_r(nullptr, ";", &save)) {
            char* gt = strchr(item, '>');
            char* eq = gt ? strchr(gt, '=') : nullptr;
            if (!gt || !eq) continue;
            *gt = '\0';
            *eq = '\0';
            memset(sv, 0, sizeof(*sv));
            copy_str(sv->from, item, sizeof(sv->from));
            copy_str(sv->to, gt + 1, sizeof(sv->to));
            ui_filter_chain_parse(eq + 1, &sv->chain);
            if (!sv->from[0] || !sv->to[0] || sv->chain.len == 0) continue;
            portENTER_CRITICAL(&s_mux);
            s_t->saved[count++] = *sv;
            portEXIT_CRITICAL(&s_mux);
        }
    }
    free(sv);
    free(buf);
    nvs_close(h);
}

static void write_saved()
{
    if (!s_t) return;
    Saved* copy = (Saved*)malloc(sizeof(Saved) * UI_FILTER_CHAINS);
    char* buf = (char*)malloc(NVS_TEXT_MAX);
    char* item = (char*)malloc(UI_FILTER_CHAIN_TEXT + 2 * UI_MIDIDEV_KEY_LEN + 4);
    if (!copy || !buf || !item) {
        free(copy);
        free(buf);
        free(item);
        return;
    }
    portENTER_CRITICAL(&s_mux);
    memcpy(copy, s_t->saved, sizeof(Saved) * UI_FILTER_CHAINS);
    portEXIT_CRITICAL(&s_mux);

    int pos = 0;
    buf[0] = '\0';
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        if (!copy[i].from[0]) continue;
        int n = snprintf(item, UI_FILTER_CHAIN_TEXT, "%s>%s=", copy[i].from, copy[i].to);
        ui_filter_chain_format(&copy[i].chain, item + n, UI_FILTER_CHAIN_TEXT);
        int need = (int)strlen(item) + (pos ? 1 : 0);
        if (pos + need >= NVS_TEXT_MAX) {
            ESP_LOGW(TAG, "Saved chains too long; the rest are not saved");
            break;
        }
        if (pos) buf[pos++] = ';';
        strcpy(buf + pos, item);
        pos += (int)strlen(item);
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (pos > 0) nvs_set_str(h, NVS_KEY, buf);
        else nvs_erase_key(h, NVS_KEY);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "NVS open failed; chains not saved");
    }
    s_saveDue = false;
    free(copy);
    free(buf);
    free(item);
}

// --- Lookups (callers hold s_mux) --------------------------------------------------

static int find_def_locked(const char* name)
{
    for (int i = 0; i < s_t->defCount; i++) {
        if (strcmp(s_t->defs[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_live_locked(uint8_t src, uint8_t dst)
{
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        if (s_t->live[i].src == src && s_t->live[i].dst == dst) return i;
    }
    return -1;
}

static int free_live_locked()
{
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        if (s_t->live[i].src == 0) return i;
    }
    return -1;
}

static int find_saved_locked(const char* from, const char* to)
{
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        if (s_t->saved[i].from[0] && strcmp(s_t->saved[i].from, from) == 0 &&
            strcmp(s_t->saved[i].to, to) == 0) return i;
    }
    return -1;
}

static int free_saved_locked()
{
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        if (!s_t->saved[i].from[0]) return i;
    }
    return -1;
}

// Both ends' keys when both are built-ins
static bool builtin_pair(uint8_t src, uint8_t dst, char* from, char* to)
{
    return ui_mididev_builtin_key(src, from, UI_MIDIDEV_KEY_LEN) &&
           ui_mididev_builtin_key(dst, to, UI_MIDIDEV_KEY_LEN);
}

static bool route_exists(uint8_t src, uint8_t dst)
{
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        uint8_t s, d;
        if (MIDI_route_get(i, &s, &d, nullptr) && s == src && (d & dst)) return true;
    }
    return false;
}

// --- Registered filters ---------------------------------------------------------------

// "name|label|kind|min|max|default|step|choice;choice;..." lines
static void parse_params(const char* text, ui_filter_def_t& def)
{
    def.nparams = 0;
    const char* line = text ? text : "";
    while (*line && def.nparams < UI_FILTER_PARAM_MAX) {
        const char* end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[160];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        ui_filter_param_t& p = def.params[def.nparams];
        memset(&p, 0, sizeof(p));
        char* fields[8] = {};
        int nf = 0;
        char* s = buf;
        fields[nf++] = s;
        for (; *s && nf < 8; s++) {
            if (*s == '|') {
                *s = '\0';
                fields[nf++] = s + 1;
            }
        }
        if (nf >= 7) {
            copy_str(p.name, fields[0], sizeof(p.name));
            copy_str(p.label, fields[1], sizeof(p.label));
            p.kind = (uint8_t)atoi(fields[2]);
            p.min = (int16_t)atoi(fields[3]);
            p.max = (int16_t)atoi(fields[4]);
            p.def = (int16_t)atoi(fields[5]);
            p.step = (int16_t)atoi(fields[6]);
            if (p.step <= 0) p.step = 1;
            if (nf >= 8) {
                char* save = nullptr;
                for (char* c = strtok_r(fields[7], ";", &save); c && p.nchoices < UI_FILTER_CHOICE_MAX;
                     c = strtok_r(nullptr, ";", &save)) {
                    copy_str(p.choices[p.nchoices++], c, UI_FILTER_CHOICE_LEN);
                }
            }
            if (p.name[0]) def.nparams++;
        }
        if (!end) break;
        line = end + 1;
    }
}

bool ui_filter_define(const char* name, const char* label, const char* params)
{
    if (!name || !name[0] || !tables()) return false;
    ui_filter_def_t* def = (ui_filter_def_t*)calloc(1, sizeof(ui_filter_def_t));
    if (!def) return false;
    copy_str(def->name, name, sizeof(def->name));
    copy_str(def->label, label && label[0] ? label : name, sizeof(def->label));
    parse_params(params, *def);

    bool ok = true;
    portENTER_CRITICAL(&s_mux);
    int i = find_def_locked(def->name);
    if (i < 0) {
        if (s_t->defCount < UI_FILTER_MAX) i = s_t->defCount++;
        else ok = false;
    }
    if (ok) s_t->defs[i] = *def;
    portEXIT_CRITICAL(&s_mux);
    free(def);
    if (ok) bump();
    return ok;
}

bool ui_filter_undefine(const char* name)
{
    if (!s_t || !name) return false;
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_def_locked(name);
    if (i >= 0) {
        for (int j = i; j < s_t->defCount - 1; j++) s_t->defs[j] = s_t->defs[j + 1];
        s_t->defCount--;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (found) bump();
    return found;
}

int ui_filter_count(void)
{
    return s_t ? s_t->defCount : 0;
}

bool ui_filter_get(int index, ui_filter_def_t* out)
{
    if (!s_t || !out) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (index >= 0 && index < s_t->defCount) {
        *out = s_t->defs[index];
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

int ui_filter_find(const char* name)
{
    if (!s_t || !name) return -1;
    portENTER_CRITICAL(&s_mux);
    int i = find_def_locked(name);
    portEXIT_CRITICAL(&s_mux);
    return i;
}

uint32_t ui_filter_serial(void)
{
    return s_serial;
}

// --- Chains -------------------------------------------------------------------------

bool ui_filter_chain_get(uint8_t src, uint8_t dst, ui_filter_chain_t* out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!s_t) return false;
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_live_locked(src, dst);
    if (i >= 0) {
        if (out) *out = s_t->live[i].chain;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return found;
}

// Put chain on the live table (len 0: off). Lock held. Returns the revision.
static uint32_t set_live_locked(uint8_t src, uint8_t dst, const ui_filter_chain_t& chain)
{
    int i = find_live_locked(src, dst);
    if (chain.len == 0) {
        if (i >= 0) s_t->live[i].src = 0;
        return ++s_rev;
    }
    if (i < 0) i = free_live_locked();
    if (i < 0) return 0;
    Live& l = s_t->live[i];
    l.src = src;
    l.dst = dst;
    l.chain = chain;
    l.rev = ++s_rev;
    return l.rev;
}

uint32_t ui_filter_chain_set(uint8_t src, uint8_t dst, const ui_filter_chain_t* chain,
                             ui_filter_save_t save)
{
    if (!src || !dst || !chain || !tables()) return 0;
    char from[UI_MIDIDEV_KEY_LEN], to[UI_MIDIDEV_KEY_LEN];
    bool saveable = save != UI_FILTER_SAVE_NONE && builtin_pair(src, dst, from, to);
    bool write = false;

    portENTER_CRITICAL(&s_mux);
    uint32_t rev = set_live_locked(src, dst, *chain);
    if (saveable) {
        int k = find_saved_locked(from, to);
        if (save == UI_FILTER_SAVE_NOW) {
            if (chain->len == 0) {
                if (k >= 0) s_t->saved[k].from[0] = '\0';
            } else {
                if (k < 0) k = free_saved_locked();
                if (k >= 0) {
                    copy_str(s_t->saved[k].from, from, UI_MIDIDEV_KEY_LEN);
                    copy_str(s_t->saved[k].to, to, UI_MIDIDEV_KEY_LEN);
                    s_t->saved[k].chain = *chain;
                }
            }
            write = true;
        } else if (k >= 0 && same_names(s_t->saved[k].chain, *chain)) {
            // A panel turned a value of a saved chain: save it once the knob rests
            s_t->saved[k].chain = *chain;
            s_saveDue = true;
            s_saveAtMs = now_ms() + SAVE_DELAY_MS;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    if (write) write_saved();
    bump();
    return rev;
}

// (The UI task calls these two: its stack is small, so they work in place.)
void ui_filter_set_value(uint8_t src, uint8_t dst, int stage, const char* pname, int16_t value)
{
    if (!s_t || !pname) return;
    char from[UI_MIDIDEV_KEY_LEN], to[UI_MIDIDEV_KEY_LEN];
    bool pair = builtin_pair(src, dst, from, to);
    bool changed = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_live_locked(src, dst);
    if (i >= 0 && stage >= 0 && stage < s_t->live[i].chain.len) {
        ui_filter_stage_t& st = s_t->live[i].chain.stages[stage];
        int v = 0;
        while (v < st.nvalues && strcmp(st.pnames[v], pname) != 0) v++;
        if (v == st.nvalues && v < UI_FILTER_PARAM_MAX) {
            copy_str(st.pnames[v], pname, UI_FILTER_LABEL_LEN);
            st.nvalues++;
        }
        if (v < st.nvalues) {
            st.values[v] = value;
            s_t->live[i].rev = ++s_rev;
            changed = true;
            int k = pair ? find_saved_locked(from, to) : -1;
            if (k >= 0 && same_names(s_t->saved[k].chain, s_t->live[i].chain)) {
                // Saved a moment after the last step (+/- held down repeats)
                s_t->saved[k].chain = s_t->live[i].chain;
                s_saveDue = true;
                s_saveAtMs = now_ms() + SAVE_DELAY_MS / 3;
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);
    if (changed) bump();
}

void ui_filter_drop(uint8_t src, uint8_t dst, bool save)
{
    if (!s_t) return;
    char from[UI_MIDIDEV_KEY_LEN], to[UI_MIDIDEV_KEY_LEN];
    bool pair = save && builtin_pair(src, dst, from, to);
    bool changed = false, write = false;
    portENTER_CRITICAL(&s_mux);
    int i = find_live_locked(src, dst);
    if (i >= 0) {
        s_t->live[i].src = 0;
        changed = true;
    }
    int k = pair ? find_saved_locked(from, to) : -1;
    if (k >= 0) {
        s_t->saved[k].from[0] = '\0';
        changed = write = true;
    }
    if (changed) ++s_rev;
    portEXIT_CRITICAL(&s_mux);
    if (write) write_saved();
    if (changed) bump();
}

bool ui_filter_saved_get(const char* from, const char* to, ui_filter_chain_t* out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!tables() || !from || !to) return false;
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    int k = find_saved_locked(from, to);
    if (k >= 0) {
        if (out) *out = s_t->saved[k].chain;
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return found;
}

void ui_filter_saved_drop(const char* from, const char* to)
{
    if (!tables() || !from || !to) return;
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    int k = find_saved_locked(from, to);
    if (k >= 0) {
        s_t->saved[k].from[0] = '\0';
        found = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (found) {
        write_saved();
        bump();
    }
}

void ui_filter_saved_set(const char* from, const char* to, const ui_filter_chain_t* chain)
{
    if (!tables() || !from || !to || !chain) return;
    portENTER_CRITICAL(&s_mux);
    int k = find_saved_locked(from, to);
    if (chain->len == 0) {
        if (k >= 0) s_t->saved[k].from[0] = '\0';
    } else {
        if (k < 0) k = free_saved_locked();
        if (k >= 0) {
            copy_str(s_t->saved[k].from, from, UI_MIDIDEV_KEY_LEN);
            copy_str(s_t->saved[k].to, to, UI_MIDIDEV_KEY_LEN);
            s_t->saved[k].chain = *chain;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    write_saved();
    bump();
}

int ui_filter_chain_len(uint8_t src, uint8_t dst)
{
    if (!s_t) return 0;
    portENTER_CRITICAL(&s_mux);
    int i = find_live_locked(src, dst);
    int n = i >= 0 ? s_t->live[i].chain.len : 0;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

int ui_filter_saved_len(const char* from, const char* to)
{
    if (!s_t || !from || !to) return 0;
    portENTER_CRITICAL(&s_mux);
    int k = find_saved_locked(from, to);
    int n = k >= 0 ? s_t->saved[k].chain.len : 0;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

int ui_filter_live_count(void)
{
    return s_t ? UI_FILTER_CHAINS : 0;
}

bool ui_filter_live_get(int index, uint8_t* src, uint8_t* dst, uint32_t* rev,
                        ui_filter_chain_t* out)
{
    if (!s_t || index < 0 || index >= UI_FILTER_CHAINS) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    const Live& l = s_t->live[index];
    if (l.src) {
        if (src) *src = l.src;
        if (dst) *dst = l.dst;
        if (rev) *rev = l.rev;
        if (out) *out = l.chain;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

// --- Saved chains ---------------------------------------------------------------------

void ui_filter_restore(const char* only_key)
{
    if (!tables()) return;
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        Saved sv;
        portENTER_CRITICAL(&s_mux);
        sv = s_t->saved[i];
        portEXIT_CRITICAL(&s_mux);
        if (!sv.from[0]) continue;
        if (only_key && strcmp(sv.from, only_key) != 0 && strcmp(sv.to, only_key) != 0) continue;
        uint8_t src = ui_mididev_bit_of_key(sv.from);
        uint8_t dst = ui_mididev_bit_of_key(sv.to);
        // An end still on standby (or not on this board): later, or never
        if (!src || !dst || !route_exists(src, dst)) continue;
        ui_filter_chain_set(src, dst, &sv.chain, UI_FILTER_SAVE_NONE);
    }
}

void ui_filter_save_live(void)
{
    if (!tables()) return;
    Saved* next = (Saved*)calloc(UI_FILTER_CHAINS, sizeof(Saved));
    if (!next) return;
    int n = 0;
    // Saved chains with an end on standby are not live: keep them
    for (int i = 0; i < UI_FILTER_CHAINS; i++) {
        Saved sv;
        portENTER_CRITICAL(&s_mux);
        sv = s_t->saved[i];
        portEXIT_CRITICAL(&s_mux);
        if (!sv.from[0]) continue;
        if (!ui_mididev_bit_of_key(sv.from) || !ui_mididev_bit_of_key(sv.to)) next[n++] = sv;
    }
    for (int i = 0; i < UI_FILTER_CHAINS && n < UI_FILTER_CHAINS; i++) {
        uint8_t src, dst;
        ui_filter_chain_t chain;
        if (!ui_filter_live_get(i, &src, &dst, nullptr, &chain)) continue;
        Saved& sv = next[n];
        if (!builtin_pair(src, dst, sv.from, sv.to)) continue;
        sv.chain = chain;
        n++;
    }
    portENTER_CRITICAL(&s_mux);
    memcpy(s_t->saved, next, sizeof(Saved) * UI_FILTER_CHAINS);
    portEXIT_CRITICAL(&s_mux);
    free(next);
    write_saved();
}

// --- Housekeeping ---------------------------------------------------------------------

void ui_filter_tick(void)
{
    if (s_saveDue && (int32_t)(now_ms() - s_saveAtMs) >= 0) write_saved();
}

void ui_filter_cleanup(void)
{
    if (!s_t) return;
    portENTER_CRITICAL(&s_mux);
    s_t->defCount = 0;
    for (int i = 0; i < UI_FILTER_CHAINS; i++) s_t->live[i].src = 0;
    portEXIT_CRITICAL(&s_mux);
    bump();
}
