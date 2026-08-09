#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Screen dimensions - board specific
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
// M5Stack Tab5: 5-inch 1280x720 display
#define UI_SCREEN_WIDTH         1280
#define UI_SCREEN_HEIGHT        720

// Layout constants for Tab5 (larger screen)
#define UI_STATUS_BAR_HEIGHT     40
#define UI_NAV_BAR_HEIGHT        60
#define UI_CONTENT_HEIGHT       (UI_SCREEN_HEIGHT - UI_STATUS_BAR_HEIGHT - UI_NAV_BAR_HEIGHT)  // 620px
#define UI_CONTENT_Y            UI_STATUS_BAR_HEIGHT

// Navigation bar touch zones (3 equal sections)
#define UI_NAV_ZONE_WIDTH       (UI_SCREEN_WIDTH / 3)
#define UI_NAV_ZONE_LEFT_END    426
#define UI_NAV_ZONE_RIGHT_START 854

// Pad configuration for Tab5 (4x3 grid = 12 pads)
#define UI_PAD_COUNT       12
#define UI_PAD_COLS        4
#define UI_PAD_ROWS        3
#define UI_PAD_WIDTH      280
#define UI_PAD_HEIGHT     180
#define UI_PAD_MARGIN      20

// Knob configuration for Tab5 (4x3 grid = 12 knobs).
// The bank strip fits in the margin the pad grid already leaves at the sides,
// so the cells keep the pad dimensions here; only the grid shifts left.
#define UI_KNOB_COUNT      12
#define UI_KNOB_COLS        4
#define UI_KNOB_ROWS        3
#define UI_KNOB_CELL_W    280
#define UI_KNOB_CELL_H    180
#define UI_KNOB_GAP_X      20
#define UI_KNOB_GAP_Y      20
#define UI_KNOB_GRID_X     20
#define UI_KNOB_GRID_Y     60
#define UI_KNOB_R_OUT      68     // Ring outer radius
#define UI_KNOB_R_IN       52     // Ring inner radius
#define UI_KNOB_RING_CY    76     // Ring centre, from the cell's top edge
#define UI_KNOB_LABEL_Y   150     // Label baseline, from the cell's top edge
#define UI_KNOB_TEXT_SIZE   3
#define UI_KNOB_BANK_X   1215     // Bank strip
#define UI_KNOB_BANK_W     50
#define UI_KNOB_BANK_H    140
#define UI_KNOB_BANK_GAP   20

#else
// M5Stack CoreS3 SE: 320x240 display (default)
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

// Pad configuration (3x2 grid = 6 pads)
#define UI_PAD_COUNT       6
#define UI_PAD_COLS        3
#define UI_PAD_ROWS        2
#define UI_PAD_WIDTH      95
#define UI_PAD_HEIGHT     75
#define UI_PAD_MARGIN      8

// Knob configuration (3x2 grid = 6 knobs).
// Cells are narrower than the pads' to make room for the bank strip. The ring
// is unaffected -- 30px radius fits either width -- so what is given up is the
// blank space beside it.
#define UI_KNOB_COUNT       6
#define UI_KNOB_COLS        3
#define UI_KNOB_ROWS        2
#define UI_KNOB_CELL_W     92
#define UI_KNOB_CELL_H     86
#define UI_KNOB_GAP_X       5
#define UI_KNOB_GAP_Y       4
#define UI_KNOB_GRID_X      3
#define UI_KNOB_GRID_Y     22
#define UI_KNOB_R_OUT      30     // Ring outer radius
#define UI_KNOB_R_IN       23     // Ring inner radius
#define UI_KNOB_RING_CY    34     // Ring centre, from the cell's top edge
#define UI_KNOB_LABEL_Y    70     // Label baseline, from the cell's top edge
#define UI_KNOB_TEXT_SIZE   1
#define UI_KNOB_BANK_X    292     // Bank strip
#define UI_KNOB_BANK_W     26
#define UI_KNOB_BANK_H     42
#define UI_KNOB_BANK_GAP    4
#endif

// Screen count
#define UI_SCREEN_COUNT         8

// Screen indices
// New screens must be appended: Ruby's UI::SCREEN_* constants mirror these
// values, so renumbering an existing entry would break SD-card scripts.
typedef enum {
    UI_SCREEN_MAIN = 0,
    UI_SCREEN_PADS,
    UI_SCREEN_MIDI_INFO,
    UI_SCREEN_LOGS,
    UI_SCREEN_SCRIPTS,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_TOMBOLA,
    UI_SCREEN_KNOBS,
} ui_screen_index_t;

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

// --- Knobs ----------------------------------------------------------------
// A knob holds a continuous value the user turns by sweeping a finger around
// it. Nothing here knows about MIDI: the Ruby block attached to the knob is
// what sends anything, exactly as it is for pads.
#define UI_KNOB_BANKS       4     // A B C D

typedef enum {
    KNOB_ORIGIN_MIN = 0,    // Gauge grows from the start of the sweep
    KNOB_ORIGIN_CENTER,     // Gauge grows either way from 12 o'clock
} knob_origin_t;

typedef struct {
    bool     assigned;      // Set from Ruby
    char     label[16];     // Display label
    uint16_t color;         // Gauge color (RGB565, shares pad_color_t values)
    float    value;
    float    min, max;
    float    step;          // Quantum; 0 for continuous
    float    initial;       // Where ui_knob_reset() puts it back
    float    sensitivity;   // 1.0 = the gauge tracks the fingertip exactly
    uint8_t  origin;        // knob_origin_t
    bool     notify;        // Queue changes for Ruby (a block is attached)
} knob_config_t;

// Bank 1 is static; banks 2-4 are allocated the first time a script writes to
// them. Four banks of config would otherwise be ~2.3KB of .bss on Tab5, and
// large static buffers here have broken the display before (docs/MEMORY_ALLOCATION.md).
extern knob_config_t  g_knob_bank0[UI_KNOB_COUNT];
extern knob_config_t *g_knob_banks[UI_KNOB_BANKS];

// Every setter runs on the caller's task (usually PicoRuby) and only mutates
// shared state, marking the knob dirty; ScreenKnobs::update() does the drawing.
void ui_knob_set_config(uint8_t bank, uint8_t index, const char* label,
                        uint16_t color, float min, float max, float step,
                        float value, uint8_t origin, float sensitivity,
                        bool notify);
void ui_knob_clear(uint8_t bank, uint8_t index);
void ui_knob_clear_all(void);           // Every bank; frees the allocated ones

float ui_knob_get_value(uint8_t bank, uint8_t index);
// Clamps and quantizes. Returns true when the stored value actually changed,
// which is also the only case that marks the knob dirty or queues an event.
// notify additionally requires the knob to carry a Ruby block.
bool  ui_knob_set_value(uint8_t bank, uint8_t index, float value, bool notify);
bool  ui_knob_reset(uint8_t bank, uint8_t index);
void  ui_knob_notify_all(uint8_t bank); // Re-announce every knob (the [Send] key)

void ui_knob_set_label(uint8_t bank, uint8_t index, const char* label);
void ui_knob_set_color(uint8_t bank, uint8_t index, uint16_t color);
// NULL for an out-of-range index or a bank no script has touched yet; both
// draw as unassigned.
const knob_config_t* ui_knob_get_config(uint8_t bank, uint8_t index);
bool ui_knob_bank_in_use(uint8_t bank);

uint8_t ui_knob_get_bank(void);
void    ui_knob_set_bank(uint8_t bank);

// Repaint tracking for the visible bank only, same as the pads'. Split in two
// because the two kinds of change cost very different amounts to draw: a new
// value only needs the wedge between the old and new angles repainted, while a
// new label or colour needs the whole cell.
void     ui_knob_mark_dirty(uint8_t index);    // Value moved
uint32_t ui_knob_take_dirty(void);
void     ui_knob_mark_repaint(uint8_t index);  // Appearance changed
uint32_t ui_knob_take_repaint(void);

// --- Tombola sequencer ---------------------------------------------------
// Physics runs in normalized units: the polygon's circumradius is 1.0 and the
// origin is the polygon centre, so the same patch behaves identically on the
// 320x240 CoreS3 and the 1280x720 Tab5. Pixels only enter at draw time.
#define UI_TOMBOLA_MAX_BALLS    16
#define UI_TOMBOLA_MIN_SIDES     3
#define UI_TOMBOLA_MAX_SIDES    16
#define UI_TOMBOLA_MAX_SCALE    16

typedef enum {
    TOMBOLA_GRAVITY_DOWN = 0,   // Constant downward pull (classic)
    TOMBOLA_GRAVITY_CENTER,     // Pull toward the polygon centre
    TOMBOLA_GRAVITY_NONE,       // Free floating: even, non-decaying patterns
} tombola_gravity_mode_t;

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
    UI_EVENT_TOMBOLA_HIT,     // A tombola ball hit a wall
    UI_EVENT_KNOB_CHANGE,     // A knob was turned
    UI_EVENT_KNOB_BANK,       // The visible knob bank was switched
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
        struct {
            uint8_t ball;    // For UI_EVENT_TOMBOLA_HIT
            uint8_t side;
            uint8_t note;
            uint8_t velocity;
        } tombola;
        struct {
            uint8_t bank;    // For UI_EVENT_KNOB_CHANGE
            uint8_t index;
            bool    final;   // Last one of a drag, sent on release
            float   value;
        } knob;
        uint8_t knob_bank;   // For UI_EVENT_KNOB_BANK
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

// Pad repaint tracking.
// ui_pad_set/ui_pad_clear/ui_pad_set_label/ui_pad_set_color only mutate the
// shared pad state -- they cannot draw, because they run on the caller's task
// (PicoRuby) and only the UI task may touch the LCD. They instead mark the pad
// dirty here, and ScreenPads::update() repaints whatever it takes.
// ui_pad_take_dirty() returns a bitmask (bit N = pad N) and clears it.
void ui_pad_mark_dirty(uint8_t index);
uint32_t ui_pad_take_dirty(void);

// Tombola functions.
// Every setter here is called from the PicoRuby task and only mutates shared
// state; ui_tombola_tick() (UI task) does the stepping and the drawing.
void ui_tombola_reset(void);          // Restore defaults and drop every ball
void ui_tombola_start(void);
void ui_tombola_stop(void);
bool ui_tombola_running(void);
void ui_tombola_tick(void);           // Physics step, called from ui_update()

// Named parameter access. Keeping this string-keyed lets the Ruby binding stay
// two functions wide no matter how many knobs the sequencer grows.
// Float keys : rotation radius gravity bounce friction spin_transfer ball_size
// Int keys   : sides gravity_mode channel duration velocity_min velocity_max
//              retrigger_ms max_voices transport sound touch_add notify
bool  ui_tombola_set_f(const char* name, float value);
bool  ui_tombola_set_i(const char* name, int value);
float ui_tombola_get_f(const char* name);
int   ui_tombola_get_i(const char* name);

void ui_tombola_set_scale(const uint8_t* notes, int len);
// note < 0 / channel < 0 mean "inherit from the scale / default channel".
int  ui_tombola_add_ball(int note, int channel, uint16_t color, float velocity_scale);
bool ui_tombola_remove_ball(int index);
void ui_tombola_clear_balls(void);
int  ui_tombola_ball_count(void);

// UI Event queue functions (for Ruby hooks)
void ui_event_init(void);
void ui_event_push(const ui_event_t* event);
// Queues a knob change, replacing the value of one already waiting for the
// same knob instead of appending. A drag emits a change every few degrees, and
// only the newest value is worth anything; this keeps the pending count at one
// per knob so a slow poller gets coarser updates rather than a backlog.
void ui_knob_event_post(uint8_t bank, uint8_t index, float value, bool final);
bool ui_event_pop(ui_event_t* event);
int ui_event_available(void);

// Get current UI BPM value (for Ruby to read)
float ui_get_bpm(void);

// Set UI BPM value (used by Ruby to seed the on-screen tempo widget
// with a script-supplied initial value).
void ui_set_bpm(float bpm);

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
