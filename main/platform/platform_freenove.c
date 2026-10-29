#include "platform.h"
#include "esp_log.h"
#include "ui/ui_common.h"

static const char *TAG = "PLATFORM";

esp_err_t platform_init(void)
{
    // Freenove uses default serial logging - nothing to do
    ESP_LOGI(TAG, "Platform: Freenove ESP32-S3");
    ESP_LOGI(TAG, "Logging to serial console");
    return ESP_OK;
}

void platform_deinit(void)
{
    // Nothing to clean up
}

void platform_update(void)
{
    // No periodic updates needed
}

// UI stubs for Freenove (no display)
// These are required by mrbgems/picoruby-ui/ports/esp32/ui.c, which is built
// for every board while main/ui/*.cpp is only built for the M5Stack boards.
// Keep this list in sync with the ui_* functions that gem port calls.

bool ui_event_pop(ui_event_t *event)
{
    // No UI on Freenove - always return false (no events)
    (void)event;
    return false;
}

int ui_event_available(void)
{
    // No UI on Freenove - always return 0 (no events)
    return 0;
}

float ui_get_bpm(void)
{
    // No UI on Freenove - return default BPM
    return UI_BPM_DEFAULT;
}

void ui_set_bpm(float bpm)
{
    // No UI on Freenove - nothing to display
    (void)bpm;
}

void ui_add_log(const char *text)
{
    // No UI on Freenove - just ignore log requests
    (void)text;
}

// Pad stubs - no touch pads without a display

void ui_pad_set(uint8_t index, const char *label, pad_color_t color, pad_type_t type)
{
    (void)index; (void)label; (void)color; (void)type;
}

void ui_pad_clear(uint8_t index)
{
    (void)index;
}

void ui_pad_clear_all(void)
{
}

bool ui_pad_get_state(uint8_t index)
{
    (void)index;
    return false;
}

void ui_pad_set_label(uint8_t index, const char *label)
{
    (void)index; (void)label;
}

void ui_pad_set_color(uint8_t index, pad_color_t color)
{
    (void)index; (void)color;
}

// Knob stubs - the knob grid lives in ui/screen_knob.cpp and its model in
// ui/ui_common.cpp, neither of which is built for a board without a display.
// Scripts that call UI.knob here get silent no-ops instead of a link error.

void ui_knob_set_config(uint8_t bank, uint8_t index, const char *label,
                        uint16_t color, float min, float max, float step,
                        float value, uint8_t origin, float sensitivity,
                        bool notify)
{
    (void)bank; (void)index; (void)label; (void)color; (void)min; (void)max;
    (void)step; (void)value; (void)origin; (void)sensitivity; (void)notify;
}

void ui_knob_clear(uint8_t bank, uint8_t index)
{
    (void)bank; (void)index;
}

void ui_knob_clear_all(void)
{
}

float ui_knob_get_value(uint8_t bank, uint8_t index)
{
    (void)bank; (void)index;
    return 0.0f;
}

bool ui_knob_set_value(uint8_t bank, uint8_t index, float value, bool notify)
{
    (void)bank; (void)index; (void)value; (void)notify;
    return false;
}

bool ui_knob_reset(uint8_t bank, uint8_t index)
{
    (void)bank; (void)index;
    return false;
}

void ui_knob_notify_all(uint8_t bank)
{
    (void)bank;
}

void ui_knob_set_label(uint8_t bank, uint8_t index, const char *label)
{
    (void)bank; (void)index; (void)label;
}

void ui_knob_set_color(uint8_t bank, uint8_t index, uint16_t color)
{
    (void)bank; (void)index; (void)color;
}

const knob_config_t *ui_knob_get_config(uint8_t bank, uint8_t index)
{
    (void)bank; (void)index;
    return NULL;
}

bool ui_knob_bank_in_use(uint8_t bank)
{
    (void)bank;
    return false;
}

uint8_t ui_knob_get_bank(void)
{
    return 0;
}

void ui_knob_set_bank(uint8_t bank)
{
    (void)bank;
}

// Screen switching stubs - only one (nonexistent) screen on Freenove

void ui_set_screen(ui_screen_index_t index)
{
    (void)index;
}

ui_screen_index_t ui_get_current_screen(void)
{
    return UI_SCREEN_MAIN;
}

bool ui_request_screen(ui_screen_index_t index, int arg)
{
    (void)index; (void)arg;
    return false;
}

bool ui_request_back(void)
{
    return false;
}

bool ui_request_step(int dir)
{
    (void)dir;
    return false;
}

int ui_get_requested_screen(int *arg)
{
    if (arg) *arg = -1;
    return -1;
}

void ui_menu_set_hidden(uint32_t hidden)
{
    (void)hidden;
}

uint32_t ui_menu_get_hidden(void)
{
    return 0;
}

void ui_script_cleanup(void)
{
}

void ui_set_loading(const char *label, int done, int total)
{
    (void)label; (void)done; (void)total;
}

// Tombola stubs - the sequencer lives in ui/screen_tombola.cpp, which is only
// built for the M5Stack boards. Scripts that touch UI::Tombola on Freenove get
// a silent no-op rather than a link error.

void ui_tombola_reset(void)
{
}

void ui_tombola_start(void)
{
}

void ui_tombola_stop(void)
{
}

bool ui_tombola_running(void)
{
    return false;
}

void ui_tombola_tick(void)
{
}

bool ui_tombola_set_f(const char *name, float value)
{
    (void)name; (void)value;
    return false;
}

bool ui_tombola_set_i(const char *name, int value)
{
    (void)name; (void)value;
    return false;
}

float ui_tombola_get_f(const char *name)
{
    (void)name;
    return 0.0f;
}

int ui_tombola_get_i(const char *name)
{
    (void)name;
    return 0;
}

void ui_tombola_set_scale(const uint8_t *notes, int len)
{
    (void)notes; (void)len;
}

int ui_tombola_add_ball(int note, int channel, uint16_t color, float velocity_scale)
{
    (void)note; (void)channel; (void)color; (void)velocity_scale;
    return -1;
}

bool ui_tombola_remove_ball(int index)
{
    (void)index;
    return false;
}

void ui_tombola_clear_balls(void)
{
}

int ui_tombola_ball_count(void)
{
    return 0;
}

// XYPad stubs - the touch surface lives in ui/screen_xypad.cpp and its model
// in ui/ui_common.cpp, neither of which is built for a board without a
// display. Scripts that touch UI::XYPad here get silent no-ops.

void ui_xypad_reset(void)
{
}

void ui_xypad_set_max_touches(uint8_t n)
{
    (void)n;
}

uint8_t ui_xypad_get_max_touches(void)
{
    return 0;
}

bool ui_xypad_set_f(uint8_t index, const char *name, float value)
{
    (void)index; (void)name; (void)value;
    return false;
}

bool ui_xypad_set_i(uint8_t index, const char *name, int value)
{
    (void)index; (void)name; (void)value;
    return false;
}

float ui_xypad_get_f(uint8_t index, const char *name)
{
    (void)index; (void)name;
    return 0.0f;
}

int ui_xypad_get_i(uint8_t index, const char *name)
{
    (void)index; (void)name;
    return 0;
}

void ui_xypad_set_scale(uint8_t index, const uint8_t *notes, uint8_t len)
{
    (void)index; (void)notes; (void)len;
}

uint8_t ui_xypad_get_scale(uint8_t index, uint8_t *out, uint8_t max_len)
{
    (void)index; (void)out; (void)max_len;
    return 0;
}

const xypad_slot_t *ui_xypad_get_slot(uint8_t index)
{
    (void)index;
    return NULL;
}

int ui_xypad_touch_down(int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    (void)touch_id; (void)x; (void)y;
    (void)content_x; (void)content_y; (void)content_w; (void)content_h;
    return -1;
}

int ui_xypad_touch_move(int touch_id, int x, int y,
                        int content_x, int content_y, int content_w, int content_h)
{
    (void)touch_id; (void)x; (void)y;
    (void)content_x; (void)content_y; (void)content_w; (void)content_h;
    return -1;
}

int ui_xypad_touch_up(int touch_id)
{
    (void)touch_id;
    return -1;
}

// Layout stubs (docs/UI_LAYOUT_DSL.md) - no screen to put them on. Ruby still
// resolves and validates the grid, so a script's mistakes show up here too;
// ui_layout_begin returning false tells it nothing reached the device.

bool ui_layout_begin(int slot, const char *title)
{
    (void)slot; (void)title;
    return false;
}

int ui_layout_add_pad(int x, int y, int w, int h, const char *label,
                      uint16_t color, uint8_t type)
{
    (void)x; (void)y; (void)w; (void)h; (void)label; (void)color; (void)type;
    return -1;
}

int ui_layout_add_knob(int x, int y, int w, int h, const char *label,
                       uint16_t color, float min, float max, float step,
                       float value, uint8_t origin, float sensitivity,
                       bool notify)
{
    (void)x; (void)y; (void)w; (void)h; (void)label; (void)color;
    (void)min; (void)max; (void)step; (void)value; (void)origin;
    (void)sensitivity; (void)notify;
    return -1;
}

int ui_layout_add_envelope(int x, int y, int w, int h, const char *label,
                           uint16_t color, float attack, float decay,
                           float sustain, float release, float max_time,
                           bool notify)
{
    (void)x; (void)y; (void)w; (void)h; (void)label; (void)color;
    (void)attack; (void)decay; (void)sustain; (void)release; (void)max_time;
    (void)notify;
    return -1;
}

int ui_layout_add_view(uint8_t kind, int x, int y, int w, int h)
{
    (void)kind; (void)x; (void)y; (void)w; (void)h;
    return -1;
}

bool ui_layout_commit(void)
{
    return false;
}

void ui_layout_delete(int slot)
{
    (void)slot;
}

bool ui_layout_pad_state(int slot, int id)
{
    (void)slot; (void)id;
    return false;
}

void ui_layout_set_label(int slot, int id, const char *label)
{
    (void)slot; (void)id; (void)label;
}

void ui_layout_set_color(int slot, int id, uint16_t color)
{
    (void)slot; (void)id; (void)color;
}

float ui_layout_knob_value(int slot, int id)
{
    (void)slot; (void)id;
    return 0.0f;
}

bool ui_layout_knob_set_value(int slot, int id, float value)
{
    (void)slot; (void)id; (void)value;
    return false;
}

bool ui_layout_knob_reset(int slot, int id)
{
    (void)slot; (void)id;
    return false;
}

bool ui_layout_envelope_get(int slot, int id, float out[4])
{
    (void)slot; (void)id; (void)out;
    return false;
}

bool ui_layout_envelope_set(int slot, int id, float attack, float decay,
                            float sustain, float release)
{
    (void)slot; (void)id; (void)attack; (void)decay; (void)sustain; (void)release;
    return false;
}

bool ui_layout_envelope_reset(int slot, int id)
{
    (void)slot; (void)id;
    return false;
}

// Keys stubs (docs/KEYS.md) - no screen to play. The keyboard never joins the
// transport registry here, so UI::Keys#transport_id is 0 and MIDI.route
// refuses it like any unregistered transport.

void ui_keys_reset(void)
{
}

bool ui_keys_set_i(const char *name, int value)
{
    (void)name; (void)value;
    return false;
}

int ui_keys_get_i(const char *name)
{
    (void)name;
    return 0;
}

void ui_keys_set_key_color(int note, int color)
{
    (void)note; (void)color;
}

int ui_keys_get_key_color(int note)
{
    (void)note;
    return -1;
}

void ui_keys_clear_key_colors(void)
{
}

void ui_keys_set_channel_color(int channel, int color)
{
    (void)channel; (void)color;
}

void ui_keys_receive(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)cin; (void)b1; (void)b2; (void)b3;
}

void ui_keys_clear_lights(void)
{
}

bool ui_keys_is_lit(int note)
{
    (void)note;
    return false;
}

bool ui_keys_is_pressed(int note)
{
    (void)note;
    return false;
}

void ui_keys_cleanup(void)
{
}

// Sequencer stubs (docs/SEQUENCER.md) - no screen, no sequencer task. It never
// joins the transport registry, so UI::Sequencer#transport_id is 0 and
// MIDI.route refuses it; edits are dropped and reads give defaults.

void ui_seq_reset(int lines)
{
    (void)lines;
}

bool ui_seq_set_i(int pattern, int line, const char *name, int value)
{
    (void)pattern; (void)line; (void)name; (void)value;
    return false;
}

int ui_seq_get_i(int pattern, int line, const char *name)
{
    (void)pattern; (void)line; (void)name;
    return 0;
}

bool ui_seq_set_name(int line, const char *name)
{
    (void)line; (void)name;
    return false;
}

int ui_seq_get_name(int line, char *buf, int size)
{
    (void)line;
    if (buf && size > 0) buf[0] = '\0';
    return 0;
}

int ui_seq_add_line(void)
{
    return -1;
}

bool ui_seq_remove_line(int line)
{
    (void)line;
    return false;
}

bool ui_seq_clear_pattern(int pattern)
{
    (void)pattern;
    return false;
}

bool ui_seq_copy_pattern(int src, int dst)
{
    (void)src; (void)dst;
    return false;
}

bool ui_seq_copy_line(int src_pattern, int src_line, int dst_pattern, int dst_line)
{
    (void)src_pattern; (void)src_line; (void)dst_pattern; (void)dst_line;
    return false;
}

int ui_seq_place(int pattern, int line, int step, int len,
                 const uint8_t *notes, const uint8_t *velocities, int n)
{
    (void)pattern; (void)line; (void)step; (void)len; (void)notes; (void)velocities; (void)n;
    return -1;
}

int ui_seq_remove(int pattern, int line, int step, int note)
{
    (void)pattern; (void)line; (void)step; (void)note;
    return 0;
}

bool ui_seq_set_length(int pattern, int line, int step, int len)
{
    (void)pattern; (void)line; (void)step; (void)len;
    return false;
}

bool ui_seq_add_event(int pattern, int line, int pos, int len, int note, int velocity)
{
    (void)pattern; (void)line; (void)pos; (void)len; (void)note; (void)velocity;
    return false;
}

void ui_seq_clear(int pattern, int line, int mode)
{
    (void)pattern; (void)line; (void)mode;
}

int ui_seq_notes_read(int pattern, int line, int mode, int start, int *out, int max)
{
    (void)pattern; (void)line; (void)mode; (void)start; (void)out; (void)max;
    return 0;
}

int ui_seq_get_chord(uint8_t *notes, uint8_t *velocities, int max)
{
    (void)notes; (void)velocities; (void)max;
    return 0;
}

void ui_seq_set_chord(const uint8_t *notes, const uint8_t *velocities, int n)
{
    (void)notes; (void)velocities; (void)n;
}

int ui_seq_get_chain(uint8_t *patterns, uint8_t *repeats, int max)
{
    (void)patterns; (void)repeats; (void)max;
    return 0;
}

bool ui_seq_set_chain(const uint8_t *patterns, const uint8_t *repeats, int n)
{
    (void)patterns; (void)repeats; (void)n;
    return false;
}

void ui_seq_receive(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)cin; (void)b1; (void)b2; (void)b3;
}

bool ui_seq_dump_begin(bool dirty_flag)
{
    (void)dirty_flag;
    return false;
}

int ui_seq_dump_next(uint8_t *buf, int cap)
{
    (void)buf; (void)cap;
    return 0;
}

void ui_seq_dump_end(bool saved, bool autosave)
{
    (void)saved; (void)autosave;
}

bool ui_seq_load_begin(void)
{
    return false;
}

int ui_seq_load_feed(const uint8_t *data, int len)
{
    (void)data; (void)len;
    return -8;
}

int ui_seq_load_commit(bool autosave)
{
    (void)autosave;
    return -8;
}

void ui_seq_load_abort(void)
{
}

const char *ui_seq_load_error_name(int err)
{
    (void)err;
    return "no sequencer";
}

int ui_seq_take_request(char *name, int size)
{
    if (name && size > 0) name[0] = '\0';
    return 0;
}

void ui_seq_set_files(const char *names)
{
    (void)names;
}

void ui_seq_set_status(const char *message)
{
    (void)message;
}

// MIDI Devices stubs (docs/UI_MIDI_DEVICES.md) - no screen to patch on. The
// list stays empty, so UI::MIDIDevices has no devices, no selection and
// nothing saved; MIDI.route itself works as everywhere.

int ui_mididev_register(const char *key, const char *name, uint8_t bit,
                        uint16_t color, bool builtin)
{
    (void)key; (void)name; (void)bit; (void)color; (void)builtin;
    return -1;
}

bool ui_mididev_unregister(uint8_t bit)
{
    (void)bit;
    return false;
}

bool ui_mididev_activate(const char *key, uint8_t bit)
{
    (void)key; (void)bit;
    return false;
}

int ui_mididev_count(void)
{
    return 0;
}

bool ui_mididev_get(int index, ui_mididev_t *out)
{
    (void)index; (void)out;
    return false;
}

int ui_mididev_selected(bool out)
{
    (void)out;
    return -1;
}

void ui_mididev_select(bool out, int index, bool save)
{
    (void)out; (void)index; (void)save;
}

int ui_mididev_restore(void)
{
    return 0;
}

bool ui_mididev_save(void)
{
    return false;
}

bool ui_mididev_take_wanted(char *key, int len)
{
    (void)key; (void)len;
    return false;
}

uint8_t ui_mididev_connected_mask(void)
{
    return 0;
}

uint32_t ui_mididev_serial(void)
{
    return 0;
}

void ui_mididev_cleanup(void)
{
}

// Named filter stubs (docs/MIDI_FILTER_REGISTRY.md) - no screen to pick them
// on: nothing is listed or saved. MIDI::Filter and MIDI.chain work as
// everywhere (they live in Ruby).

bool ui_filter_define(const char *name, const char *label, const char *params)
{
    (void)name; (void)label; (void)params;
    return false;
}

bool ui_filter_undefine(const char *name)
{
    (void)name;
    return false;
}

uint32_t ui_filter_serial(void)
{
    return 0;
}

int ui_filter_live_count(void)
{
    return 0;
}

bool ui_filter_live_get(int index, uint8_t *src, uint8_t *dst, uint32_t *rev,
                        ui_filter_chain_t *out)
{
    (void)index; (void)src; (void)dst; (void)rev; (void)out;
    return false;
}

int ui_filter_chain_format(const ui_filter_chain_t *chain, char *buf, int len)
{
    (void)chain;
    if (len > 0) buf[0] = '\0';
    return 0;
}

bool ui_filter_chain_parse(const char *text, ui_filter_chain_t *out)
{
    (void)text;
    out->len = 0;
    return true;
}

uint32_t ui_filter_chain_set(uint8_t src, uint8_t dst, const ui_filter_chain_t *chain,
                             ui_filter_save_t save)
{
    (void)src; (void)dst; (void)chain; (void)save;
    return 1;
}

// Tempo stubs (docs/TEMPO.md) - no UI, so no tempo device: UI::Tempo has no
// transport bit and MIDI.route refuses it like any unregistered transport.

uint8_t ui_tempo_bit(void)
{
    return 0;
}

void ui_tempo_set_running(bool running)
{
    (void)running;
}

bool ui_tempo_running(void)
{
    return false;
}

void ui_tempo_tap(void)
{
}

bool ui_tempo_sync(void)
{
    return false;
}

void ui_tempo_set_sync(bool on)
{
    (void)on;
}
