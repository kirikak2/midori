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
