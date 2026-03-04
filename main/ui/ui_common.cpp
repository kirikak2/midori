#include "ui_common.h"
#include "screen_log.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
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
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_pad_clear(uint8_t index)
{
    if (index >= UI_PAD_COUNT) return;

    portENTER_CRITICAL(&s_ui_mutex);
    g_pads[index].assigned = false;
    g_pads[index].label[0] = '\0';
    g_pads[index].state = false;
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
    portEXIT_CRITICAL(&s_ui_mutex);
}

void ui_pad_set_color(uint8_t index, pad_color_t color)
{
    if (index >= UI_PAD_COUNT) return;
    g_pads[index].color = color;
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
