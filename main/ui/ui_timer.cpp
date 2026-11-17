// The Timer (docs/TIMER.md): a countdown that MIDI Start starts and that
// ends with MIDI Stop.
//
// "Start" is picoruby-midi's MIDI::Clock going from stopped to running -- the
// Tempo screen's Play, UI::Tempo.start, MIDI::Clock.start -- which the UI
// task notices on its next pass (ui_timer_tick). While armed, that starts the
// countdown from the time set. The seconds are counted on an esp_timer
// one-shot re-armed for each second boundary from the start time, so neither
// a slow UI pass nor a busy Ruby VM moves them. Each second down is a
// UI_EVENT_TIMER (UI::Timer's blocks run from UI.process); at 0 the timer
// stops the clock itself, which sends MIDI Stop along the routes from the
// tempo. A Stop from anywhere else before that halts the countdown where it is.
//
// Not armed, nothing happens on Start: a timer nobody asked for must not stop
// the music after a minute.

#include "ui_common.h"
#include "midi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "UI_TIMER";

// State shared by the UI task, the esp_timer task and the PicoRuby task
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_seconds = UI_TIMER_DEFAULT_SECONDS;    // The time set
static int s_total = UI_TIMER_DEFAULT_SECONDS;      // What the running countdown started from
static int s_remaining = UI_TIMER_DEFAULT_SECONDS;
static uint8_t s_state = UI_TIMER_IDLE;
static int64_t s_startUs;
static bool s_userArmed;
static bool s_scriptArmed;
static uint32_t s_serial;

// UI task only
static esp_timer_handle_t s_timer;
static bool s_lastRunning;

static void post(uint8_t kind, int remaining)
{
    ui_event_t event;
    event.type = UI_EVENT_TIMER;
    event.data.timer.kind = kind;
    event.data.timer.remaining = (uint16_t)(remaining < 0 ? 0 : remaining);
    ui_event_push(&event);
}

// The one-shot for the second after `elapsed` whole seconds since start
static void schedule(int64_t startUs, int elapsed)
{
    if (!s_timer) return;
    int64_t due = startUs + (int64_t)(elapsed + 1) * 1000000 - esp_timer_get_time();
    if (due < 0) due = 0;
    esp_timer_stop(s_timer);   // Not running is fine
    esp_timer_start_once(s_timer, (uint64_t)due);
}

// esp_timer task: a second boundary
static void on_second(void* arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    if (s_state != UI_TIMER_RUNNING) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    int elapsed = (int)((now - s_startUs) / 1000000);
    int remaining = s_total - elapsed;
    if (remaining < 0) remaining = 0;
    int before = s_remaining;
    bool finished = remaining == 0;
    s_remaining = remaining;
    if (finished) s_state = UI_TIMER_FINISHED;
    int64_t start = s_startUs;
    s_serial++;
    portEXIT_CRITICAL(&s_mux);

    // Every second down gets its event, even if this one fired late
    for (int r = before - 1; r >= remaining; r--) post(UI_TIMER_EVENT_TICK, r);

    if (finished) {
        ESP_LOGI(TAG, "Time up: MIDI Stop");
        post(UI_TIMER_EVENT_FINISH, 0);
        MIDI_Clock_stop();
    } else {
        schedule(start, elapsed);
    }
}

static bool ensure_timer(void)
{
    if (s_timer) return true;
    esp_timer_create_args_t args = {};
    args.callback = on_second;
    args.arg = nullptr;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "ui_timer";
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed");
        s_timer = nullptr;
        return false;
    }
    return true;
}

static void start_countdown(void)
{
    if (!ensure_timer()) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    s_total = s_seconds;
    s_remaining = s_total;
    s_startUs = now;
    s_state = UI_TIMER_RUNNING;
    s_serial++;
    int total = s_total;
    portEXIT_CRITICAL(&s_mux);

    post(UI_TIMER_EVENT_START, total);
    schedule(now, 0);
}

void ui_timer_tick(void)
{
    bool running = MIDI_Clock_is_running();
    if (running == s_lastRunning) return;
    s_lastRunning = running;

    if (running) {
        if (ui_timer_armed()) start_countdown();
        return;
    }

    // Stopped: by someone else unless the countdown got to 0 (FINISHED)
    portENTER_CRITICAL(&s_mux);
    bool wasRunning = s_state == UI_TIMER_RUNNING;
    if (wasRunning) {
        s_state = UI_TIMER_STOPPED;
        s_serial++;
    }
    int remaining = s_remaining;
    portEXIT_CRITICAL(&s_mux);
    if (!wasRunning) return;
    if (s_timer) esp_timer_stop(s_timer);
    post(UI_TIMER_EVENT_STOP, remaining);
}

void ui_timer_set_seconds(int seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > UI_TIMER_MAX_SECONDS) seconds = UI_TIMER_MAX_SECONDS;
    portENTER_CRITICAL(&s_mux);
    s_seconds = seconds;
    // A countdown going on keeps its time; the next one uses this
    if (s_state != UI_TIMER_RUNNING) {
        s_remaining = seconds;
        s_state = UI_TIMER_IDLE;
    }
    s_serial++;
    portEXIT_CRITICAL(&s_mux);
}

int ui_timer_seconds(void)
{
    return s_seconds;
}

int ui_timer_remaining(void)
{
    return s_remaining;
}

int ui_timer_state(void)
{
    return s_state;
}

void ui_timer_reset(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    bool running = s_state == UI_TIMER_RUNNING;
    s_total = s_seconds;
    s_remaining = s_seconds;
    if (running) {
        s_startUs = now;
    } else {
        s_state = UI_TIMER_IDLE;
    }
    s_serial++;
    int total = s_total;
    portEXIT_CRITICAL(&s_mux);

    if (running) {
        post(UI_TIMER_EVENT_START, total);
        schedule(now, 0);
    }
}

void ui_timer_set_armed(bool on, bool by_script)
{
    portENTER_CRITICAL(&s_mux);
    if (!on) {
        s_userArmed = false;
        s_scriptArmed = false;
    } else if (by_script) {
        s_scriptArmed = true;
    } else {
        s_userArmed = true;
    }
    s_serial++;
    portEXIT_CRITICAL(&s_mux);
}

bool ui_timer_armed(void)
{
    return s_userArmed || s_scriptArmed;
}

uint32_t ui_timer_serial(void)
{
    return s_serial;
}

// A script ended. Its arming goes with it; the clock is stopped by the MIDI
// cleanup (without a Stop event for the countdown: nothing is listening), and
// the time set stays.
void ui_timer_cleanup(void)
{
    if (s_timer) esp_timer_stop(s_timer);
    portENTER_CRITICAL(&s_mux);
    s_scriptArmed = false;
    s_state = UI_TIMER_IDLE;
    s_remaining = s_seconds;
    s_serial++;
    portEXIT_CRITICAL(&s_mux);
    s_lastRunning = MIDI_Clock_is_running();
}
