#include "ui_manager.h"
#include "screen_log.h"
#include "screen_main.h"
#include "screen_pad.h"
#include "screen_midi_info.h"
#include "screen_script.h"
#include "screen_settings.h"
#include "screen_tombola.h"
#include "screen_knob.h"
#include <M5Unified.h>
#include "esp_log.h"

extern "C" {
#include "midi.h"
}

static const char* TAG = "UI_MANAGER";

// UIManager implementation
UIManager& UIManager::getInstance()
{
    static UIManager instance;
    return instance;
}

UIManager::UIManager()
    : m_currentIndex(0)
    , m_initialized(false)
    , m_needsRedraw(true)
    , m_internalBpm(UI_BPM_DEFAULT)
    , m_externalBpm(0.0f)
    , m_selectedExternalBpmSource(MIDI_INTERFACE_USB)
    , m_syncMode(false)
    , m_bpmChangeCallback(nullptr)
    , m_bar(0)
    , m_beat(0)
    , m_beatProgress(0)
    , m_padEventCallback(nullptr)
{
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        m_screens[i] = nullptr;
    }
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchStates[i].isPressed = false;
        m_touchStates[i].inContent = false;
        m_touchStates[i].x = 0;
        m_touchStates[i].y = 0;
    }
    for (int i = 0; i < 3; i++) {
        m_externalBpmBySource[i] = 0.0f;
    }
}

UIManager::~UIManager()
{
    // Placeholder screens are static, no need to delete
}

esp_err_t UIManager::init()
{
    if (m_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing UI Manager");

    // Initialize screens (placeholders for now)
    initScreens();

    // Reset any text-scroll state left over from lcd_console boot screen.
    // Without this, setTextScroll(true) and setScrollRect from lcd_console_init
    // remain active globally and cause flicker (the scroll-rect area gets
    // filled with the scroll-rect's bgcolor whenever a print() crosses an
    // invisible boundary).
    M5.Lcd.setTextScroll(false);
    M5.Lcd.setScrollRect(0, 0, M5.Lcd.width(), M5.Lcd.height());

    // Clear screen
    M5.Lcd.fillScreen(UI_COLOR_BLACK);

    // Enter the first screen
    m_currentIndex = UI_SCREEN_MAIN;
    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->enter();
    }

    drawStatusBar();
    drawNavBar();

    m_initialized = true;
    m_needsRedraw = false;

    return ESP_OK;
}

void UIManager::initScreens()
{
    // Initialize all screens
    m_screens[UI_SCREEN_MAIN] = &getScreenMain();
    m_screens[UI_SCREEN_PADS] = &getScreenPads();
    m_screens[UI_SCREEN_MIDI_INFO] = &getScreenMidiInfo();
    m_screens[UI_SCREEN_LOGS] = &getScreenLog();
    m_screens[UI_SCREEN_SCRIPTS] = &getScreenScripts();
    m_screens[UI_SCREEN_SETTINGS] = &getScreenSettings();
    m_screens[UI_SCREEN_TOMBOLA] = &getScreenTombola();
    m_screens[UI_SCREEN_KNOBS] = &getScreenKnobs();
}

void UIManager::update()
{
    if (!m_initialized) return;

    // Update external BPM from C side (every 100ms)
    static uint32_t lastBpmUpdate = 0;
    uint32_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (now - lastBpmUpdate > 100) {
        float usbBpm = MIDI_Input_get_external_bpm_usb();
        float samBpm = MIDI_Input_get_external_bpm_sam();

        // Track if any source just started receiving BPM
        bool usbStarted = (m_externalBpmBySource[MIDI_INTERFACE_USB] <= 0.0f && usbBpm > 0.0f);
        bool samStarted = (m_externalBpmBySource[MIDI_INTERFACE_DIN] <= 0.0f && samBpm > 0.0f);

        setExternalBpmSource(MIDI_INTERFACE_USB, usbBpm);
        setExternalBpmSource(MIDI_INTERFACE_DIN, samBpm);
        // BLE not implemented yet
        setExternalBpmSource(MIDI_INTERFACE_BLE, 0.0f);

        // Auto-select source if currently selected source has no BPM
        float currentSourceBpm = m_externalBpmBySource[m_selectedExternalBpmSource];
        if (currentSourceBpm <= 0.0f) {
            // Try to find a source with BPM (prefer DIN > USB > BLE)
            if (samBpm > 0.0f) {
                setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
            } else if (usbBpm > 0.0f) {
                setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
            }
        }
        // Also auto-select when a new source starts receiving (prefer DIN)
        else if (samStarted) {
            setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
        } else if (usbStarted && m_selectedExternalBpmSource != MIDI_INTERFACE_DIN) {
            setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
        }

        lastBpmUpdate = now;
    }

    // Handle touch input
    handleTouch();

    // Step the tombola sequencer. Deliberately outside the current-screen
    // check: a running patch must keep playing while the user is looking at
    // another screen.
    ui_tombola_tick();

    // Update current screen
    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->update();
    }

    // Redraw if needed
    if (m_needsRedraw) {
        M5.Lcd.startWrite();
        drawStatusBar();
        if (m_screens[m_currentIndex]) {
            m_screens[m_currentIndex]->draw();
        }
        drawNavBar();
        M5.Lcd.endWrite();
        m_needsRedraw = false;
    }
}

void UIManager::handleTouch()
{
    // Get number of active touch points
    uint8_t touchCount = M5.Touch.getCount();

    // Track which touch points are currently active (by touch ID)
    bool currentlyPressed[MAX_TOUCH_POINTS] = {false};

    // Process each active touch point
    for (uint8_t i = 0; i < touchCount && i < MAX_TOUCH_POINTS; i++) {
        auto touch = M5.Touch.getDetail(i);
        if (!touch.isPressed()) continue;

        // Use the actual touch ID from the hardware
        uint8_t touchId = touch.id;
        if (touchId >= MAX_TOUCH_POINTS) continue;

        currentlyPressed[touchId] = true;
        int x = touch.x;
        int y = touch.y;

        if (!m_touchStates[touchId].isPressed) {
            // New touch start
            m_touchStates[touchId].isPressed = true;
            m_touchStates[touchId].inContent = false;
            m_touchStates[touchId].x = x;
            m_touchStates[touchId].y = y;

            // Check which area was touched
            if (y < UI_STATUS_BAR_HEIGHT) {
                // Status bar - ignore
            } else if (y >= UI_SCREEN_HEIGHT - UI_NAV_BAR_HEIGHT) {
                // Navigation bar - only handle on first touch point (touchId 0)
                if (touchId == 0) {
                    if (x < UI_NAV_ZONE_LEFT_END) {
                        prevScreen();
                    } else if (x >= UI_NAV_ZONE_RIGHT_START) {
                        nextScreen();
                    } else {
                        if (m_screens[m_currentIndex]) {
                            m_screens[m_currentIndex]->onNavCenter();
                        }
                    }
                }
            } else {
                // Content area - pass to current screen with touch ID
                m_touchStates[touchId].inContent = true;
                if (m_screens[m_currentIndex]) {
                    m_screens[m_currentIndex]->onTouch(touchId, x, y, true);
                }
            }
        } else {
            // Touch point moved
            m_touchStates[touchId].x = x;
            m_touchStates[touchId].y = y;
            if (m_touchStates[touchId].inContent && m_screens[m_currentIndex]) {
                m_screens[m_currentIndex]->onTouchMove(touchId, x, y);
            }
        }
    }

    // Check for released touch points
    for (int touchId = 0; touchId < MAX_TOUCH_POINTS; touchId++) {
        if (m_touchStates[touchId].isPressed && !currentlyPressed[touchId]) {
            // Touch released. Keyed off where the press landed, not where the
            // finger ended up: a drag that wanders onto the nav bar before
            // lifting still has to reach the screen, or it never learns the
            // touch ended (a pad would stay stuck down, a drag stuck active).
            if (m_touchStates[touchId].inContent && m_screens[m_currentIndex]) {
                m_screens[m_currentIndex]->onTouch(touchId, m_touchStates[touchId].x, m_touchStates[touchId].y, false);
            }

            m_touchStates[touchId].isPressed = false;
            m_touchStates[touchId].inContent = false;
        }
    }
}

void UIManager::drawStatusBar()
{
    if (m_screens[m_currentIndex]) {
        ui_draw_status_bar(m_screens[m_currentIndex]->getTitle());
    }
}

void UIManager::drawNavBar()
{
    const char* centerLabel = nullptr;
    if (m_screens[m_currentIndex]) {
        centerLabel = m_screens[m_currentIndex]->getNavCenterLabel();
    }
    if (!centerLabel) {
        centerLabel = m_screens[m_currentIndex] ? m_screens[m_currentIndex]->getTitle() : "";
    }
    ui_draw_nav_bar(centerLabel, true);
}

void UIManager::nextScreen()
{
    int nextIndex = (m_currentIndex + 1) % UI_SCREEN_COUNT;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->leave();
    }

    m_currentIndex = nextIndex;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->enter();
    }

    m_needsRedraw = true;
    ESP_LOGD(TAG, "Switched to screen %d", m_currentIndex);
}

void UIManager::prevScreen()
{
    int prevIndex = (m_currentIndex - 1 + UI_SCREEN_COUNT) % UI_SCREEN_COUNT;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->leave();
    }

    m_currentIndex = prevIndex;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->enter();
    }

    m_needsRedraw = true;
    ESP_LOGD(TAG, "Switched to screen %d", m_currentIndex);
}

void UIManager::setScreen(ui_screen_index_t index)
{
    if (index >= UI_SCREEN_COUNT || index == m_currentIndex) {
        return;
    }

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->leave();
    }

    m_currentIndex = index;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->enter();
    }

    m_needsRedraw = true;
}

ui_screen_index_t UIManager::getCurrentScreenIndex() const
{
    return static_cast<ui_screen_index_t>(m_currentIndex);
}

Screen* UIManager::getCurrentScreen()
{
    return m_screens[m_currentIndex];
}

void UIManager::setBpm(float bpm)
{
    if (bpm < UI_BPM_MIN) bpm = UI_BPM_MIN;
    if (bpm > UI_BPM_MAX) bpm = UI_BPM_MAX;

    if (m_internalBpm != bpm) {
        m_internalBpm = bpm;
        // Push UI event for Ruby hooks
        ui_event_t event;
        event.type = UI_EVENT_BPM_CHANGE;
        event.data.bpm = bpm;
        ui_event_push(&event);
        if (m_bpmChangeCallback) {
            m_bpmChangeCallback(bpm);
        }
        // NOTE: do NOT set m_needsRedraw here. The Main screen's update() polls
        // for BPM changes and updates only the BPM area. Setting m_needsRedraw
        // would force a full-screen redraw (status bar + content + nav bar) on
        // every external MIDI clock tick (~48 Hz in sync mode), causing visible
        // flicker.
    }
}

float UIManager::getBpm() const
{
    return m_syncMode ? m_externalBpm : m_internalBpm;
}

void UIManager::setExternalBpm(float bpm)
{
    m_externalBpm = bpm;
    // NOTE: do NOT set m_needsRedraw here. The Main screen's update() polls
    // ui.getBpm() (which returns externalBpm in sync mode) and ui.getExternalBpm()
    // and refreshes only the affected widgets. Forcing a full-screen redraw on
    // every MIDI clock tick (~48 Hz at 120 BPM) caused severe flicker.
}

float UIManager::getExternalBpm() const
{
    return m_externalBpm;
}

void UIManager::setExternalBpmSource(midi_interface_t source, float bpm)
{
    if (source < 0 || source > 2) return;
    m_externalBpmBySource[source] = bpm;

    // Update main external BPM if this is the selected source
    if (source == m_selectedExternalBpmSource) {
        m_externalBpm = bpm;
        // NOTE: do NOT set m_needsRedraw. Same reason as setExternalBpm() above
        // (called ~48 Hz from MIDI clock; full-screen redraw causes flicker).
    }
}

float UIManager::getExternalBpmBySource(midi_interface_t source) const
{
    if (source < 0 || source > 2) return 0.0f;
    return m_externalBpmBySource[source];
}

void UIManager::setExternalBpmSourceSelection(midi_interface_t source)
{
    if (source < 0 || source > 2) return;
    m_selectedExternalBpmSource = source;
    m_externalBpm = m_externalBpmBySource[source];
    if (m_syncMode && m_currentIndex == UI_SCREEN_MAIN) {
        m_needsRedraw = true;
    }
}

midi_interface_t UIManager::getExternalBpmSourceSelection() const
{
    return m_selectedExternalBpmSource;
}

void UIManager::setSyncMode(bool enabled)
{
    if (m_syncMode != enabled) {
        m_syncMode = enabled;

        // When enabling sync mode, auto-select a source with BPM if current source has none
        if (enabled) {
            float currentBpm = m_externalBpmBySource[m_selectedExternalBpmSource];
            if (currentBpm <= 0.0f) {
                // Try to find a source with BPM
                if (m_externalBpmBySource[MIDI_INTERFACE_DIN] > 0.0f) {
                    setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
                } else if (m_externalBpmBySource[MIDI_INTERFACE_USB] > 0.0f) {
                    setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
                } else if (m_externalBpmBySource[MIDI_INTERFACE_BLE] > 0.0f) {
                    setExternalBpmSourceSelection(MIDI_INTERFACE_BLE);
                }
            }
        }

        if (m_currentIndex == UI_SCREEN_MAIN) {
            m_needsRedraw = true;
        }
    }
}

bool UIManager::getSyncMode() const
{
    return m_syncMode;
}

void UIManager::setBpmChangeCallback(bpm_change_cb_t cb)
{
    m_bpmChangeCallback = cb;
}

void UIManager::setBarBeat(uint32_t bar, uint8_t beat)
{
    m_bar = bar;
    m_beat = beat;
}

void UIManager::setBeatProgress(uint8_t progress)
{
    if (progress > 23) progress = 23;
    m_beatProgress = progress;
}

uint32_t UIManager::getBar() const
{
    return m_bar;
}

uint8_t UIManager::getBeat() const
{
    return m_beat;
}

uint8_t UIManager::getBeatProgress() const
{
    return m_beatProgress;
}

void UIManager::setPadEventCallback(pad_event_cb_t cb)
{
    m_padEventCallback = cb;
}

pad_event_cb_t UIManager::getPadEventCallback() const
{
    return m_padEventCallback;
}

void UIManager::requestRedraw()
{
    m_needsRedraw = true;
}

// C API implementation
extern "C" {

esp_err_t ui_init(void)
{
    return UIManager::getInstance().init();
}

void ui_update(void)
{
    UIManager::getInstance().update();
}

void ui_set_bpm(float bpm)
{
    UIManager::getInstance().setBpm(bpm);
}

float ui_get_bpm(void)
{
    return UIManager::getInstance().getBpm();
}

void ui_set_external_bpm(float bpm)
{
    UIManager::getInstance().setExternalBpm(bpm);
}

float ui_get_external_bpm(void)
{
    return UIManager::getInstance().getExternalBpm();
}

void ui_set_external_bpm_source(midi_interface_t source, float bpm)
{
    UIManager::getInstance().setExternalBpmSource(source, bpm);
}

float ui_get_external_bpm_by_source(midi_interface_t source)
{
    return UIManager::getInstance().getExternalBpmBySource(source);
}

void ui_set_external_bpm_source_selection(midi_interface_t source)
{
    UIManager::getInstance().setExternalBpmSourceSelection(source);
}

midi_interface_t ui_get_external_bpm_source_selection(void)
{
    return UIManager::getInstance().getExternalBpmSourceSelection();
}

void ui_set_sync_mode(bool enabled)
{
    UIManager::getInstance().setSyncMode(enabled);
}

bool ui_get_sync_mode(void)
{
    return UIManager::getInstance().getSyncMode();
}

void ui_set_bpm_change_callback(bpm_change_cb_t cb)
{
    UIManager::getInstance().setBpmChangeCallback(cb);
}

void ui_set_bar_beat(uint32_t bar, uint8_t beat)
{
    UIManager::getInstance().setBarBeat(bar, beat);
}

void ui_set_beat_progress(uint8_t progress)
{
    UIManager::getInstance().setBeatProgress(progress);
}

void ui_set_screen(ui_screen_index_t index)
{
    UIManager::getInstance().setScreen(index);
}

ui_screen_index_t ui_get_current_screen(void)
{
    return UIManager::getInstance().getCurrentScreenIndex();
}

void ui_set_pad_event_callback(pad_event_cb_t cb)
{
    UIManager::getInstance().setPadEventCallback(cb);
}

void ui_request_redraw(void)
{
    UIManager::getInstance().requestRedraw();
}

} // extern "C"
