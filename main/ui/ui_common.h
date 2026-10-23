#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Boards roomy enough for the spacious layout: bigger fonts, a 4x3 grid,
// centred pad/knob grids. Screens branch on this rather than on a board
// symbol, so a new large-screen board only has to add its geometry below.
// Everything in the large branches is expressed relative to UI_SCREEN_WIDTH /
// UI_CONTENT_* where the position matters.
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5) || \
    defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
#define UI_LAYOUT_LARGE 1
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

// Layout DSL default gap between cells (and around the grid)
#define UI_LAYOUT_GAP      20

#elif defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
// Elecrow CrowPanel Advanced 7": 1024x600 display, native landscape
#define UI_SCREEN_WIDTH         1024
#define UI_SCREEN_HEIGHT        600

// Layout constants (same bar heights as the Tab5; only the middle shrinks)
#define UI_STATUS_BAR_HEIGHT     40
#define UI_NAV_BAR_HEIGHT        60
#define UI_CONTENT_HEIGHT       (UI_SCREEN_HEIGHT - UI_STATUS_BAR_HEIGHT - UI_NAV_BAR_HEIGHT)  // 500px
#define UI_CONTENT_Y            UI_STATUS_BAR_HEIGHT

// Navigation bar touch zones (3 equal sections)
#define UI_NAV_ZONE_WIDTH       (UI_SCREEN_WIDTH / 3)
#define UI_NAV_ZONE_LEFT_END    341
#define UI_NAV_ZONE_RIGHT_START 683

// Pad configuration (4x3 grid = 12 pads).
// screen_pad centres the grid: 4*230 + 3*20 = 980 of 1024 across,
// 3*150 + 2*20 = 490 of the 500px content area down.
#define UI_PAD_COUNT       12
#define UI_PAD_COLS        4
#define UI_PAD_ROWS        3
#define UI_PAD_WIDTH      230
#define UI_PAD_HEIGHT     150
#define UI_PAD_MARGIN      20

// Knob configuration (4x3 grid = 12 knobs).
// The grid is shifted left to leave room for the bank strip at the right
// edge: 12 + 4*226 + 3*16 = 964, strip at 972..1016.
#define UI_KNOB_COUNT      12
#define UI_KNOB_COLS        4
#define UI_KNOB_ROWS        3
#define UI_KNOB_CELL_W    226
#define UI_KNOB_CELL_H    150
#define UI_KNOB_GAP_X      16
#define UI_KNOB_GAP_Y      16
#define UI_KNOB_GRID_X     12
#define UI_KNOB_GRID_Y     52
#define UI_KNOB_R_OUT      56     // Ring outer radius
#define UI_KNOB_R_IN       43     // Ring inner radius
#define UI_KNOB_RING_CY    62     // Ring centre, from the cell's top edge
#define UI_KNOB_LABEL_Y   126     // Label baseline, from the cell's top edge
#define UI_KNOB_TEXT_SIZE   2
#define UI_KNOB_BANK_X    972     // Bank strip: 4*112 + 3*16 = 496 of 500px
#define UI_KNOB_BANK_W     44
#define UI_KNOB_BANK_H    112
#define UI_KNOB_BANK_GAP   16

#define UI_LAYOUT_GAP      16

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

#define UI_LAYOUT_GAP       6
#endif

// Screen count
#define UI_SCREEN_COUNT         12

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
    UI_SCREEN_XYPAD,
    UI_SCREEN_LAYOUT,     // Script-defined layouts (docs/UI_LAYOUT_DSL.md)
    UI_SCREEN_HOME,       // Tile menu every other screen is reached from
    UI_SCREEN_KEYS,       // On-screen keyboard (docs/KEYS.md)
} ui_screen_index_t;

// What caused a screen switch, reported with UI_EVENT_SCREEN_CHANGE
typedef enum {
    UI_SCREEN_SOURCE_TOUCH = 0,   // Home tile, Home / Back on the nav bar
    UI_SCREEN_SOURCE_SCRIPT,      // UI.show and friends
    UI_SCREEN_SOURCE_SYSTEM,      // The firmware itself (a layout went away)
} ui_screen_source_t;

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
// Clamp to the knob's range and snap to its step (what every setter applies)
float ui_knob_quantize(const knob_config_t* k, float value);
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

// MIDI interface type (external clock sources on the Tempo screen)
typedef enum {
    MIDI_INTERFACE_USB,          // USB-MIDI host
    MIDI_INTERFACE_DIN,          // UART (SAM2695)
    MIDI_INTERFACE_BLE,
    MIDI_INTERFACE_USB_DEVICE,   // USB-MIDI device: what the PC sends
    MIDI_INTERFACE_COUNT
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
    UI_EVENT_XYPAD_TOUCH,     // An XYPad slot's touch moved through a phase
    UI_EVENT_WIDGET,          // A pad / knob on a layout was operated
    UI_EVENT_KEY,             // A key of the on-screen keyboard went down / up
    UI_EVENT_MIDI_DEVICES,    // MIDI Devices: selection, patch or connection changed
} ui_event_type_t;

typedef enum {
    UI_WIDGET_PRESS = 0,      // Pad pressed
    UI_WIDGET_RELEASE,        // Pad released
    UI_WIDGET_CHANGE,         // Knob value moved
} ui_widget_phase_t;

typedef enum {
    XYPAD_PHASE_DOWN = 0,
    XYPAD_PHASE_MOVE,
    XYPAD_PHASE_UP,
} xypad_phase_t;

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
        struct {
            uint8_t index;   // For UI_EVENT_SCREEN_CHANGE (ui_screen_index_t)
            uint8_t from;
            uint8_t source;  // ui_screen_source_t
            int8_t  layout;  // Layout slot when index is UI_SCREEN_LAYOUT, else -1
        } screen;
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
        struct {
            uint8_t slot;    // For UI_EVENT_XYPAD_TOUCH (0-based)
            uint8_t phase;   // xypad_phase_t
            uint8_t channel;
            uint8_t note;
            float   bend_semitones;  // Meaningful when the slot's x_mode is :note
            float   x;               // Meaningful when the slot's x_mode is :cc
            float   y;
        } xypad;
        struct {
            uint8_t layout;  // For UI_EVENT_WIDGET: layout slot
            uint8_t id;      // Widget index within the layout
            uint8_t kind;    // ui_widget_kind_t
            uint8_t phase;   // ui_widget_phase_t
            bool    state;   // Pad state after the press / release
            bool    final;   // Knob: last change of a drag
            float   value;   // Knob value
        } widget;
        struct {
            uint8_t note;    // For UI_EVENT_KEY
            uint8_t velocity;
            uint8_t channel;
            bool    on;
        } key;
        struct {
            uint8_t kind;    // For UI_EVENT_MIDI_DEVICES: ui_mididev_event_t
            uint8_t from;    // Transport bit (the device for select / connect)
            uint8_t to;      // Transport bit (patches)
            int8_t  channel; // Patches: 0..15, -1 all, -2 clock, -3 several (see sel)
            uint32_t sel;    // Patches: MIDI_ROUTE_SEL_* (0 for the other kinds)
        } mididev;
    } data;
} ui_event_t;

// Global pad configuration
extern pad_config_t g_pads[UI_PAD_COUNT];

// Drawing functions
void ui_draw_status_bar(const char* title);
// home_enabled / back_enabled grey the two side buttons out when they would
// do nothing (Home while already on Home, Back with nowhere to go back to).
void ui_draw_nav_bar(const char* center_label, bool home_enabled, bool back_enabled);
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

// --- XYPad ------------------------------------------------------------------
// Up to five simultaneous touches, each an independent "slot": its own X
// meaning (scale + glide, or a raw CC), its own Y range, channel and hold
// flag. A touch claims the lowest-numbered free slot on touchdown and uses
// that slot's own settings until it is released. Nothing here knows about
// MIDI destinations, CC numbers or velocity -- those live in the Ruby class
// alongside the block that actually sends (mrbgems/picoruby-ui/mrblib/ui.rb),
// exactly as it is for knobs.
//
// Must match MAX_TOUCH_POINTS in ui_manager.h. Kept as a separate constant
// because that one is a C++ constexpr and this header has to stay C-compatible
// (platform_freenove.c and the mrubyc bindings include it as plain C).
#define UI_XYPAD_MAX_TOUCHES  5
#define UI_XYPAD_MAX_SCALE   16    // Same cap as Tombola's scale

typedef enum {
    XYPAD_XMODE_NOTE = 0,      // X: snap to the scale, then glide via pitch bend
    XYPAD_XMODE_CC,            // X: absolute value -> CC, same treatment as Y
} xypad_xmode_t;

typedef struct {
    // --- Configuration (set via ui_xypad_set_f/set_i/set_scale) ---
    uint8_t  x_mode;             // xypad_xmode_t
    uint8_t  scale[UI_XYPAD_MAX_SCALE];
    uint8_t  scale_len;
    float    glide_range;        // Semitones of bend across the full width
    float    x_min, x_max;       // Value range when x_mode is XYPAD_XMODE_CC
    float    y_min, y_max;
    bool     y_invert;
    uint8_t  gate_note;          // Fixed note used to gate when x_mode is CC
    uint8_t  channel;
    bool     hold;

    // --- Runtime state (changes over a touch's lifecycle) ---
    bool     active;             // A finger is currently down on this slot
    bool     latched;            // Held past release because hold was true
    int      touch_id;           // Hardware touch id occupying this slot, -1 if free
    uint8_t  note;                // Sounding note (landed note, or gate_note copy)
    float    bend_semitones;     // -glide_range .. +glide_range (:note only)
    float    x;                   // x_min .. x_max (:cc only)
    float    y;                   // y_min .. y_max
    int16_t  touchdown_x;         // Pixel X at touchdown, for the relative bend
    // Snapshot of the mode/range in effect for this touch, taken at touchdown
    // so a mid-drag config change (pad.slot(n, ...) from the script) cannot
    // flip what an in-progress touch means.
    uint8_t  touch_x_mode;
    float    touch_glide_range;
} xypad_slot_t;

void ui_xypad_reset(void);                     // Every slot back to defaults, drops all touches
void ui_xypad_set_max_touches(uint8_t n);      // Clamped to UI_XYPAD_MAX_TOUCHES
uint8_t ui_xypad_get_max_touches(void);

// Named-parameter access per slot (index 0-based), the same shape as Tombola's
// ui_tombola_set_f/set_i: keeps the mrubyc binding two functions wide no
// matter how many slot parameters exist. Only the configuration fields are
// touched -- an in-progress touch's runtime state is left alone, except for
// the hold-release transition documented on the definition.
// Float keys : glide_range x_min x_max y_min y_max
//              touch_glide_range (read-only; the glide_range snapshotted for
//              whichever touch currently occupies the slot, see xypad_slot_t)
// Int keys   : x_mode y_invert gate_note channel hold
bool  ui_xypad_set_f(uint8_t index, const char* name, float value);
bool  ui_xypad_set_i(uint8_t index, const char* name, int value);
float ui_xypad_get_f(uint8_t index, const char* name);
int   ui_xypad_get_i(uint8_t index, const char* name);

void    ui_xypad_set_scale(uint8_t index, const uint8_t* notes, uint8_t len);
uint8_t ui_xypad_get_scale(uint8_t index, uint8_t* out, uint8_t max_len);  // Returns the count

// NULL for an out-of-range index; the screen draws it as an empty slot.
const xypad_slot_t* ui_xypad_get_slot(uint8_t index);

// Driven by ScreenXYPad's touch handlers. content_x/y/w/h describe the content
// area in screen pixels, which is what X/Y get mapped against. Finds/frees the
// slot itself and returns which one it used (-1 when down finds no free slot,
// or move/up are called for a touch that never claimed one), so the screen
// knows what to (re)draw without keeping its own touch-to-slot map.
int ui_xypad_touch_down(int touch_id, int x, int y, int content_x, int content_y, int content_w, int content_h);
int ui_xypad_touch_move(int touch_id, int x, int y, int content_x, int content_y, int content_w, int content_h);
int ui_xypad_touch_up(int touch_id);

// --- Keys -------------------------------------------------------------------
// An on-screen keyboard of 1 to 4 octaves (13 to 49 keys) that is also a
// picoruby-midi transport (docs/KEYS.md). Implemented in ui_keys.cpp.
//
// As a source, a touched key sends its note on / off from the UI task, in C,
// to the keyboard's own destinations (out_mask) and to wherever MIDI.route
// sends what comes from its transport bit -- the same latency argument as
// Tombola's. As a destination (MIDI::Device.new(keys), MIDI.route(x, keys))
// incoming notes light the keys, so the keyboard can mirror what another
// instrument plays.
//
// One keyboard (a single model, like the XYPad's), allocated the first time
// anything touches it: the per-note tables are ~1KB, which is not something
// to leave in .bss (docs/MEMORY_ALLOCATION.md).
#define UI_KEYS_MIN_OCTAVES   1
#define UI_KEYS_MAX_OCTAVES   4
#define UI_KEYS_MAX_KEYS     (UI_KEYS_MAX_OCTAVES * 12 + 1)   // 49

typedef enum {
    KEYS_VELOCITY_FIXED = 0,   // Always the configured velocity
    KEYS_VELOCITY_POSITION,    // Softer near the top of a key, louder at its bottom
} keys_velocity_mode_t;

// Every setter may be called from any task; drawing happens on the UI task.
void ui_keys_reset(void);                // Defaults, lights and key colours cleared

// Named parameters, string-keyed like Tombola's.
// Int keys: octaves low channel velocity velocity_mode slide labels notify
//           out_mask white_color black_color press_color light_color
//           transport_bit (read-only)
// low is moved down to the nearest white key; octaves is clamped to
// UI_KEYS_MIN_OCTAVES..UI_KEYS_MAX_OCTAVES and low so the top key fits 0..127.
bool ui_keys_set_i(const char* name, int value);
int  ui_keys_get_i(const char* name);

// Per-note colour (RGB565), shown while the key is neither pressed nor lit.
// color < 0 clears it back to plain white / black.
void ui_keys_set_key_color(int note, int color);
int  ui_keys_get_key_color(int note);    // -1 when not set
void ui_keys_clear_key_colors(void);
// Colour incoming notes on a channel light their key with; color < 0 clears
// it back to light_color.
void ui_keys_set_channel_color(int channel, int color);

// The keyboard as a destination: what its transport's send_packet does.
// Note on / off light and unlight keys, All Notes Off / All Sound Off and
// System Reset clear the lights.
void ui_keys_receive(uint8_t cin, uint8_t b1, uint8_t b2, uint8_t b3);
void ui_keys_clear_lights(void);

bool ui_keys_is_lit(int note);           // Lit by incoming MIDI
bool ui_keys_is_pressed(int note);       // Held down by a finger

// A script ended: back to defaults (any task, including the MIDI clock's).
void ui_keys_cleanup(void);

// --- Layouts ----------------------------------------------------------------
// Script-defined screens of pads, knobs, XYPad... placed on a grid by Ruby
// (docs/UI_LAYOUT_DSL.md). Up to UI_LAYOUT_MAX_LAYOUTS live at once, each a
// Home tile, all shown through UI_SCREEN_LAYOUT. Implemented in ui_layout.cpp.
#define UI_LAYOUT_MAX_LAYOUTS  4
#define UI_LAYOUT_MAX_WIDGETS 32
#define UI_LAYOUT_TITLE_LEN   24

typedef enum {
    UI_WIDGET_PAD = 0,
    UI_WIDGET_KNOB,
    UI_WIDGET_XYPAD,      // The XYPad screen's view, drawn in the widget's cell
    UI_WIDGET_TOMBOLA,    // Likewise the Tombola's
    UI_WIDGET_KEYS,       // Likewise the Keys'
    UI_WIDGET_TEMPO,      // Likewise the Tempo screen's
} ui_widget_kind_t;

typedef struct {
    uint8_t kind;          // ui_widget_kind_t
    int16_t x, y, w, h;    // Screen pixels, resolved by Ruby
    union {
        pad_config_t  pad;
        knob_config_t knob;
    };
} ui_widget_t;

// Defining a layout (PicoRuby task): begin, add each widget (each returns its
// id, or -1), commit. Nothing is visible until commit swaps the whole layout
// in, replacing whatever was in that slot.
bool ui_layout_begin(int slot, const char* title);
int  ui_layout_add_pad(int x, int y, int w, int h, const char* label,
                       uint16_t color, uint8_t type);
int  ui_layout_add_knob(int x, int y, int w, int h, const char* label,
                        uint16_t color, float min, float max, float step,
                        float value, uint8_t origin, float sensitivity,
                        bool notify);
int  ui_layout_add_view(uint8_t kind, int x, int y, int w, int h);
bool ui_layout_commit(void);
void ui_layout_delete(int slot);

// Operating a widget from Ruby (PicoRuby task). Changes are drawn by the UI
// task. ui_layout_knob_set_value returns whether the value moved.
bool  ui_layout_pad_state(int slot, int id);
void  ui_layout_set_label(int slot, int id, const char* label);
void  ui_layout_set_color(int slot, int id, uint16_t color);
float ui_layout_knob_value(int slot, int id);
bool  ui_layout_knob_set_value(int slot, int id, float value);
bool  ui_layout_knob_reset(int slot, int id);

// Queues a layout knob change, coalesced per knob like ui_knob_event_post
void ui_widget_knob_event_post(uint8_t layout, uint8_t id, float value, bool final);

bool ui_layout_exists(int slot);
int  ui_layout_last_shown(void);                 // -1 before any was shown
void ui_layout_title(int slot, char* buf, int len);
// The script that defined them is gone: drop every layout. Any task.
void ui_layout_cleanup(void);

// --- MIDI Devices -------------------------------------------------------------
// The devices the MIDI Devices screen patches between (docs/UI_MIDI_DEVICES.md),
// each a picoruby-midi transport bit. Patches are MIDI.route's route table;
// this keeps the device list, the program's IN / OUT selection and the saved
// patches (NVS). Implemented in ui_midi_devices.cpp.
#define UI_MIDIDEV_MAX       8
#define UI_MIDIDEV_KEY_LEN  12
#define UI_MIDIDEV_NAME_LEN 16

enum {
    UI_MIDIDEV_BUILTIN = 0x01,   // The firmware's: survives scripts, its patches are saved
    UI_MIDIDEV_STANDBY = 0x02,   // Not started yet, no bit (AMY until first used)
    UI_MIDIDEV_NO_OUT  = 0x04,   // Only sends: no MIDI OUT column (Tempo)
};

typedef enum {
    UI_MIDIDEV_EVENT_SELECT_IN = 0,
    UI_MIDIDEV_EVENT_SELECT_OUT,
    UI_MIDIDEV_EVENT_PATCH,
    UI_MIDIDEV_EVENT_UNPATCH,
    UI_MIDIDEV_EVENT_CONNECT,
    UI_MIDIDEV_EVENT_DISCONNECT,
} ui_mididev_event_t;

typedef struct {
    uint8_t  bit;                       // Transport bit, 0 while standby
    uint8_t  flags;                     // UI_MIDIDEV_*
    uint16_t color;                     // RGB565
    char     key[UI_MIDIDEV_KEY_LEN];   // Built-ins: what NVS knows it by
    char     name[UI_MIDIDEV_NAME_LEN];
} ui_mididev_t;

// Registering (PicoRuby task). A device is found again by its key (built-ins)
// or its bit: registering it again renames it. bit 0 registers a built-in
// standby. Returns the entry's index, or -1 when the list is full.
int  ui_mididev_register(const char* key, const char* name, uint8_t bit,
                         uint16_t color, bool builtin);
bool ui_mididev_unregister(uint8_t bit);
// A standby device started and got its bit
bool ui_mididev_activate(const char* key, uint8_t bit);
// Mark a built-in as only sending (UI_MIDIDEV_NO_OUT)
void ui_mididev_set_no_out(const char* key);

// Reading (any task)
int  ui_mididev_count(void);
bool ui_mididev_get(int index, ui_mididev_t* out);
int  ui_mididev_find_bit(uint8_t bit);           // index, or -1
uint8_t ui_mididev_connected_mask(void);         // Transports that are up
uint32_t ui_mididev_serial(void);                // Bumped on every change

// The program's IN / OUT (Prog column / row). index -1 for none. save: also
// make it what a script starts with (NVS).
int  ui_mididev_selected(bool out);
void ui_mididev_select(bool out, int index, bool save);

// What a patch passes is a route selection (midi_route.h): MIDI_ROUTE_SEL_ALL
// for everything, else any of channels 0..15 (bits 0..15) and the clock
// (MIDI_ROUTE_SEL_CLOCK). 0: no patch.

// Patches from the screen (UI task), saved when both are built-ins.
// set_patch replaces the route between two entries (sel 0 removes it);
// toggle_patch removes it, or patches everything when there is none.
void ui_mididev_set_patch(int from, int to, uint32_t sel);
void ui_mididev_toggle_patch(int from, int to);
// What the routes between two entries pass, or 0
uint32_t ui_mididev_patch_selection(int from, int to);

// Saved patches (NVS)
int  ui_mididev_restore(void);                   // Route them again; how many
bool ui_mididev_save(void);                      // Save the live ones
// A standby device something needs (a saved patch, the selection): copies its
// key and clears the request. Polled by Ruby, which starts it.
bool ui_mididev_take_wanted(char* key, int len);

void ui_mididev_tick(void);      // UI task: connection changes -> events
void ui_mididev_cleanup(void);   // A script ended (any task)

// --- Tempo ------------------------------------------------------------------------
// The tempo is a MIDI device that sends clock (docs/TEMPO.md): picoruby-midi's
// MIDI::Clock (one per process), listed on the MIDI Devices screen as "Tempo"
// and routed like any other source (MIDI.route / the MIDI Devices screen). Its
// tempo is the UI's internal BPM; in Sync mode it follows the external clock.
// Implemented in ui_tempo.cpp.
void    ui_tempo_init(void);          // UIManager::init: lists the clock as a device
void    ui_tempo_tick(void);          // UI task, every pass: BPM to / from the clock, Sync
void    ui_tempo_bpm_changed(float bpm);      // UIManager::setBpm: the clock follows
uint8_t ui_tempo_bit(void);           // The clock's transport bit, 0 if there is none
void    ui_tempo_set_running(bool running);   // Sends Start (FA) / Stop (FC)
bool    ui_tempo_running(void);
void    ui_tempo_tap(void);           // One tap of TAP tempo
bool    ui_tempo_sync(void);          // Following the external clock
void    ui_tempo_set_sync(bool on);   // Only takes when an external clock is there

// UI Event queue functions (for Ruby hooks)
void ui_event_init(void);
void ui_event_push(const ui_event_t* event);
// Queues a knob change, replacing the value of one already waiting for the
// same knob instead of appending. A drag emits a change every few degrees, and
// only the newest value is worth anything; this keeps the pending count at one
// per knob so a slow poller gets coarser updates rather than a backlog.
void ui_knob_event_post(uint8_t bank, uint8_t index, float value, bool final);
// Queues an XYPad touch phase. MOVE replaces a pending MOVE for the same slot
// the same way knob changes coalesce; DOWN and UP are never coalesced away
// since they are what tells Ruby to gate a note on or off.
void ui_xypad_event_post(uint8_t slot, xypad_phase_t phase, uint8_t channel,
                         uint8_t note, float bend_semitones, float x, float y);
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
