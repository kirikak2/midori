#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screen dimensions (M5Stack CoreS3 SE: 320x240)
#define UI_SCREEN_WIDTH         320
#define UI_SCREEN_HEIGHT        240

// Layout constants
#define UI_STATUS_BAR_HEIGHT     20
#define UI_NAV_BAR_HEIGHT        40
#define UI_CONTENT_HEIGHT       (UI_SCREEN_HEIGHT - UI_STATUS_BAR_HEIGHT - UI_NAV_BAR_HEIGHT)  // 180px
#define UI_CONTENT_Y            UI_STATUS_BAR_HEIGHT

// Navigation bar touch zones (3 equal sections)
#define UI_NAV_ZONE_WIDTH       (UI_SCREEN_WIDTH / 3)
#define UI_NAV_ZONE_LEFT_END    106
#define UI_NAV_ZONE_RIGHT_START 214

// Screen count
#define UI_SCREEN_COUNT         6

// Screen indices
typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_PADS,
    UI_SCREEN_MIDI_INFO,
    UI_SCREEN_LOGS,
    UI_SCREEN_SCRIPTS,
    UI_SCREEN_SETTINGS,
} ui_screen_index_t;

// Pad configuration
#define UI_PAD_COUNT       6
#define UI_PAD_COLS        3
#define UI_PAD_ROWS        2
#define UI_PAD_WIDTH      95
#define UI_PAD_HEIGHT     75
#define UI_PAD_MARGIN      8

// BPM configuration
#define UI_BPM_MIN         20.0f
#define UI_BPM_MAX        300.0f
#define UI_BPM_DEFAULT    120.0f

// TAP tempo configuration
#define UI_TAP_TEMPO_SAMPLES     4
#define UI_TAP_TEMPO_TIMEOUT_MS  2000

// Color definitions (RGB565)
typedef enum {
    UI_COLOR_BLACK     = 0x0000,
    UI_COLOR_WHITE     = 0xFFFF,
    UI_COLOR_RED       = 0xF800,
    UI_COLOR_GREEN     = 0x07E0,
    UI_COLOR_BLUE      = 0x001F,
    UI_COLOR_YELLOW    = 0xFFE0,
    UI_COLOR_CYAN      = 0x07FF,
    UI_COLOR_MAGENTA   = 0xF81F,
    UI_COLOR_ORANGE    = 0xFD20,
    UI_COLOR_PURPLE    = 0x8010,
    UI_COLOR_GRAY      = 0x8410,
    UI_COLOR_DARKGRAY  = 0x4208,
    UI_COLOR_NAVY      = 0x000F,
    UI_COLOR_DARKGREEN = 0x03E0,
} ui_color_t;

// Pad color type (for Ruby API)
typedef enum {
    PAD_COLOR_RED     = UI_COLOR_RED,
    PAD_COLOR_GREEN   = UI_COLOR_GREEN,
    PAD_COLOR_BLUE    = UI_COLOR_BLUE,
    PAD_COLOR_YELLOW  = UI_COLOR_YELLOW,
    PAD_COLOR_CYAN    = UI_COLOR_CYAN,
    PAD_COLOR_MAGENTA = UI_COLOR_MAGENTA,
    PAD_COLOR_ORANGE  = UI_COLOR_ORANGE,
    PAD_COLOR_PURPLE  = UI_COLOR_PURPLE,
    PAD_COLOR_WHITE   = UI_COLOR_WHITE,
    PAD_COLOR_GRAY    = UI_COLOR_GRAY,
} pad_color_t;

// Pad button type
typedef enum {
    PAD_TYPE_TRIGGER,    // Fire once on tap
    PAD_TYPE_MOMENTARY,  // ON while pressed
    PAD_TYPE_TOGGLE,     // Toggle ON/OFF
} pad_type_t;

// Pad configuration structure
typedef struct {
    bool assigned;           // Set from Ruby
    char label[16];          // Display label
    pad_color_t color;       // Button color
    pad_type_t type;         // Button type
    bool state;              // Current state (toggle/momentary)
} pad_config_t;

// Log source type
typedef enum {
    LOG_SOURCE_ESP,      // ESP-IDF ESP_LOGx
    LOG_SOURCE_RUBY,     // PicoRuby puts/print
    LOG_SOURCE_MIDI,     // MIDI messages
} log_source_t;

// MIDI interface type
typedef enum {
    MIDI_INTERFACE_USB,
    MIDI_INTERFACE_DIN,
    MIDI_INTERFACE_BLE,
} midi_interface_t;

// BPM change callback type
typedef void (*bpm_change_cb_t)(float new_bpm);

// Pad event callback type
typedef void (*pad_event_cb_t)(uint8_t index, bool pressed);

// UI Event types (for Ruby hooks)
typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_BPM_CHANGE,      // BPM was changed via UI
    UI_EVENT_PAD_PRESS,       // Pad was pressed
    UI_EVENT_PAD_RELEASE,     // Pad was released
    UI_EVENT_SYNC_MODE,       // Sync mode was toggled
    UI_EVENT_SCREEN_CHANGE,   // Screen was changed
} ui_event_type_t;

// UI Event structure
typedef struct {
    ui_event_type_t type;
    union {
        float bpm;           // For UI_EVENT_BPM_CHANGE
        struct {
            uint8_t index;   // For UI_EVENT_PAD_*
            bool state;
        } pad;
        bool sync_mode;      // For UI_EVENT_SYNC_MODE
        uint8_t screen;      // For UI_EVENT_SCREEN_CHANGE
    } data;
} ui_event_t;

// Global pad configuration
extern pad_config_t g_pads[UI_PAD_COUNT];

// Drawing functions
void ui_draw_status_bar(const char* title);
void ui_draw_nav_bar(const char* center_label, bool show_arrows);
void ui_draw_button(int x, int y, int w, int h, const char* label, uint16_t bg_color, uint16_t text_color, bool pressed);
void ui_clear_content_area(void);

// MIDI indicator
void ui_set_midi_indicator(bool connected);
bool ui_get_midi_indicator(void);

// Color utilities
uint16_t ui_lighten_color(uint16_t color);
uint16_t ui_darken_color(uint16_t color);

// Pad functions
void ui_pad_set(uint8_t index, const char* label, pad_color_t color, pad_type_t type);
void ui_pad_clear(uint8_t index);
void ui_pad_clear_all(void);
bool ui_pad_get_state(uint8_t index);
void ui_pad_set_state(uint8_t index, bool state);
void ui_pad_set_label(uint8_t index, const char* label);
void ui_pad_set_color(uint8_t index, pad_color_t color);
const pad_config_t* ui_pad_get_config(uint8_t index);

// UI Event queue functions (for Ruby hooks)
void ui_event_init(void);
void ui_event_push(const ui_event_t* event);
bool ui_event_pop(ui_event_t* event);
int ui_event_available(void);

// Get current UI BPM value (for Ruby to read)
float ui_get_bpm(void);

// Log functions (for Ruby to output to screen log)
void ui_add_log(const char* text);
void ui_clear_logs(void);

// ESP-IDF log hook (captures ESP_LOGx to screen log)
// serial_vprintf: the real serial output function (before any LCD redirect)
typedef int (*vprintf_like_t)(const char*, va_list);
void ui_log_hook_init_with_serial(vprintf_like_t serial_vprintf);

#ifdef __cplusplus
}
#endif

#endif // UI_COMMON_H
