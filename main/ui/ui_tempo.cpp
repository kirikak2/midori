// The tempo as a MIDI device that sends clock (docs/TEMPO.md).
//
// It is a picoruby-midi transport that only sends: a route source that pushes
// its own output (like the on-screen keys), so MIDI.route(UI::Tempo, dev) and
// the MIDI Devices screen send its clock anywhere. An esp_timer emits Timing
// Clock (F8) at 24 per quarter note of ui_get_bpm() -- the internal BPM, or
// the external clock's in Sync mode -- through MIDI_route_dispatch(), so
// filters and listeners on it work too. The timer runs only while something
// is routed from the tempo.

#include "ui_common.h"
#include "ui_manager.h"
#include "midi_route.h"
#include "midi_transport.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "UI_TEMPO";

static constexpr int PPQ = 24;
// Below this change the period is left alone: in Sync mode the measured BPM
// wobbles a little on every clock it is measured from.
static constexpr float RETIME_THRESHOLD_BPM = 0.05f;

static uint8_t s_bit;
static esp_timer_handle_t s_timer;
static bool s_timerOn;
static float s_timerBpm;
static volatile bool s_running;     // Between Start and Stop

// TAP tempo
static uint32_t s_tapTimes[UI_TAP_TEMPO_SAMPLES];
static int s_tapIndex;
static int s_tapCount;

// --- Transport ----------------------------------------------------------------------
// Nothing is sent *to* the tempo (it has no MIDI OUT column), and nothing reads
// it: it pushes its clock itself.

static int tempo_send_packet(void*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t)
{
    return 0;
}

static bool tempo_is_connected(void*)
{
    return true;
}

static const midi_transport_ops_t s_ops = {
    .send_packet = tempo_send_packet,
    .read_bytes = nullptr,
    .bytes_available = nullptr,
    .is_connected = tempo_is_connected,
    .transport_id = MIDI_TRANSPORT_ID_NONE,
};
static const midi_transport_t s_transport = { &s_ops, nullptr };

static void send(uint8_t status)
{
    if (!s_bit) return;
    MIDI_route_dispatch(s_bit, MIDI_route_targets(s_bit, 0xF, status), 0xF, status, 0, 0);
}

// --- Clock ----------------------------------------------------------------------------

static void clock_cb(void*)
{
    send(0xF8);
}

static uint64_t period_us(float bpm)
{
    if (bpm < UI_BPM_MIN) bpm = UI_BPM_MIN;
    if (bpm > UI_BPM_MAX) bpm = UI_BPM_MAX;
    return (uint64_t)(60000000.0f / (bpm * PPQ));
}

void ui_tempo_init(void)
{
    if (s_bit) return;
    s_bit = MIDI_transport_register(&s_transport);
    if (!s_bit) {
        ESP_LOGW(TAG, "MIDI transport registry full: the tempo sends no clock");
        return;
    }
    MIDI_route_set_source(s_bit, true);
    ui_mididev_register("tempo", "Tempo", s_bit, UI_COLOR_CYAN, true);
    ui_mididev_set_no_out("tempo");

    esp_timer_create_args_t args = {};
    args.callback = clock_cb;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "tempo_clock";
    args.skip_unhandled_events = true;
    if (esp_timer_create(&args, &s_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the clock timer");
        s_timer = nullptr;
    }
}

void ui_tempo_tick(void)
{
    if (!s_timer) return;
    bool want = MIDI_route_from(s_bit);
    float bpm = ui_get_bpm();

    if (want && !s_timerOn) {
        s_timerBpm = bpm;
        esp_timer_start_periodic(s_timer, period_us(bpm));
        s_timerOn = true;
    } else if (!want && s_timerOn) {
        esp_timer_stop(s_timer);
        s_timerOn = false;
    } else if (s_timerOn && std::fabs(bpm - s_timerBpm) >= RETIME_THRESHOLD_BPM) {
        s_timerBpm = bpm;
        esp_timer_restart(s_timer, period_us(bpm));
    }
}

uint8_t ui_tempo_bit(void)
{
    return s_bit;
}

void ui_tempo_set_running(bool running)
{
    s_running = running;
    send(running ? 0xFA : 0xFC);
    // The first clock after Start marks the downbeat: start the period afresh
    if (running && s_timerOn) esp_timer_restart(s_timer, period_us(s_timerBpm));
}

bool ui_tempo_running(void)
{
    return s_running;
}

void ui_tempo_tap(void)
{
    // In Sync mode the tempo is the external clock's
    if (ui_get_sync_mode()) return;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_tapCount > 0) {
        uint32_t last = s_tapTimes[(s_tapIndex - 1 + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES];
        if (now - last > UI_TAP_TEMPO_TIMEOUT_MS) {
            s_tapCount = 0;
            s_tapIndex = 0;
        }
    }
    s_tapTimes[s_tapIndex] = now;
    s_tapIndex = (s_tapIndex + 1) % UI_TAP_TEMPO_SAMPLES;
    if (s_tapCount < UI_TAP_TEMPO_SAMPLES) s_tapCount++;
    if (s_tapCount < 2) return;

    uint32_t total = 0;
    int intervals = s_tapCount - 1;
    for (int i = 0; i < intervals; i++) {
        int curr = (s_tapIndex - 1 - i + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES;
        int prev = (curr - 1 + UI_TAP_TEMPO_SAMPLES) % UI_TAP_TEMPO_SAMPLES;
        total += s_tapTimes[curr] - s_tapTimes[prev];
    }
    float bpm = 60000.0f * intervals / (float)total;
    if (bpm >= UI_BPM_MIN && bpm <= UI_BPM_MAX) {
        ui_set_bpm(bpm);
        ESP_LOGI(TAG, "TAP tempo: %.1f BPM", bpm);
    }
}

bool ui_tempo_sync(void)
{
    return ui_get_sync_mode();
}

void ui_tempo_set_sync(bool on)
{
    // As the Sync button: nothing to follow without an external clock
    if (on && UIManager::getInstance().getExternalBpm() <= 0) return;
    ui_set_sync_mode(on);
}

void ui_tempo_cleanup(void)
{
    // picoruby_esp32_midi_cleanup() has just sent Stop everywhere
    s_running = false;
}
