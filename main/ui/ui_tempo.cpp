// The tempo as a MIDI device that sends clock (docs/TEMPO.md).
//
// The clock itself is picoruby-midi's MIDI::Clock -- one per process, a
// transport that only sends and a route source, ticking on an esp_timer in
// the gem's port -- so MIDI.route(UI::Tempo, dev), MIDI.route(MIDI::Clock,
// dev) and the MIDI Devices screen all send the same clock. This file lists
// it on the MIDI Devices screen and keeps it in step with the UI: the
// clock's tempo is the UI's internal BPM (a script that sets MIDI::Clock.bpm
// shows up here as a BPM change), and in Sync mode it follows the external
// clock instead.

#include "ui_common.h"
#include "ui_manager.h"
#include "midi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "UI_TEMPO";

// Below this change the follow BPM is left alone: in Sync mode the measured
// BPM wobbles a little on every clock it is measured from.
static constexpr float RETIME_THRESHOLD_BPM = 0.05f;

static bool s_ready;
static float s_followBpm;     // What the clock was last told to follow (0: none)

// TAP tempo
static uint32_t s_tapTimes[UI_TAP_TEMPO_SAMPLES];
static int s_tapIndex;
static int s_tapCount;

void ui_tempo_init(void)
{
    if (s_ready) return;
    if (MIDI_Clock_init() != 0 || !MIDI_Clock_bit()) {
        ESP_LOGW(TAG, "No MIDI clock: the tempo sends no clock");
        return;
    }
    s_ready = true;
    MIDI_Clock_set_bpm(UIManager::getInstance().getInternalBpm());
    ui_mididev_register("tempo", "Tempo", MIDI_Clock_bit(), UI_COLOR_CYAN, true);
    ui_mididev_set_no_out("tempo");
}

void ui_tempo_tick(void)
{
    if (!s_ready) return;
    UIManager& ui = UIManager::getInstance();

    // A script set the clock's tempo (MIDI::Clock.bpm =, MIDI.bpm_loop(120))
    float clockBpm = MIDI_Clock_get_bpm();
    if (clockBpm != ui.getInternalBpm()) ui.setBpm(clockBpm);

    float follow = ui.getSyncMode() ? ui.getExternalBpm() : 0.0f;
    if (follow < 0.0f) follow = 0.0f;
    if ((follow == 0.0f) != (s_followBpm == 0.0f) ||
        std::fabs(follow - s_followBpm) >= RETIME_THRESHOLD_BPM) {
        s_followBpm = follow;
        MIDI_Clock_set_follow_bpm(follow);
    }
}

void ui_tempo_bpm_changed(float bpm)
{
    if (s_ready) MIDI_Clock_set_bpm(bpm);
}

uint8_t ui_tempo_bit(void)
{
    return s_ready ? MIDI_Clock_bit() : 0;
}

void ui_tempo_set_running(bool running)
{
    if (!s_ready) return;
    if (running) {
        MIDI_Clock_start();
    } else {
        MIDI_Clock_stop();
    }
}

bool ui_tempo_running(void)
{
    return MIDI_Clock_is_running();
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
