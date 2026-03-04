#include "ui_manager.h"
#include "screen_log.h"
#include "screen_main.h"
#include "screen_pad.h"
#include "screen_midi_info.h"
#include "screen_script.h"
#include "screen_settings.h"
#include <M5Unified.h>
#include "esp_log.h"

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
    , m_syncMode(false)
    , m_bpmChangeCallback(nullptr)
    , m_bar(0)
    , m_beat(0)
    , m_beatProgress(0)
    , m_padEventCallback(nullptr)
    , m_wasTouched(false)
    , m_lastTouchX(0)
    , m_lastTouchY(0)
{
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        m_screens[i] = nullptr;
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
}

void UIManager::update()
{
    if (!m_initialized) return;

    // Handle touch input
    handleTouch();

    // Update current screen
    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->update();
    }

    // Redraw if needed
    if (m_needsRedraw) {
        drawStatusBar();
        if (m_screens[m_currentIndex]) {
            m_screens[m_currentIndex]->draw();
        }
        drawNavBar();
        m_needsRedraw = false;
    }
}

void UIManager::handleTouch()
{
    auto touch = M5.Touch.getDetail();
    bool isTouched = touch.isPressed();

    if (isTouched && !m_wasTouched) {
        // Touch start
        int x = touch.x;
        int y = touch.y;

        // Check which area was touched
        if (y < UI_STATUS_BAR_HEIGHT) {
            // Status bar - ignore
        } else if (y >= UI_SCREEN_HEIGHT - UI_NAV_BAR_HEIGHT) {
            // Navigation bar
            if (x < UI_NAV_ZONE_LEFT_END) {
                // Left zone - previous screen
                prevScreen();
            } else if (x >= UI_NAV_ZONE_RIGHT_START) {
                // Right zone - next screen
                nextScreen();
            } else {
                // Center zone - screen-specific action
                if (m_screens[m_currentIndex]) {
                    m_screens[m_currentIndex]->onNavCenter();
                }
            }
        } else {
            // Content area - pass to current screen
            if (m_screens[m_currentIndex]) {
                m_screens[m_currentIndex]->onTouch(x, y, true);
            }
        }

        m_lastTouchX = x;
        m_lastTouchY = y;
    } else if (!isTouched && m_wasTouched) {
        // Touch release
        int y = m_lastTouchY;

        // Only send release to content area touches
        if (y >= UI_STATUS_BAR_HEIGHT && y < UI_SCREEN_HEIGHT - UI_NAV_BAR_HEIGHT) {
            if (m_screens[m_currentIndex]) {
                m_screens[m_currentIndex]->onTouch(m_lastTouchX, m_lastTouchY, false);
            }
        }
    }

    m_wasTouched = isTouched;
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
        if (m_currentIndex == UI_SCREEN_MAIN) {
            m_needsRedraw = true;
        }
    }
}

float UIManager::getBpm() const
{
    return m_syncMode ? m_externalBpm : m_internalBpm;
}

void UIManager::setExternalBpm(float bpm)
{
    m_externalBpm = bpm;
    if (m_syncMode && m_currentIndex == UI_SCREEN_MAIN) {
        m_needsRedraw = true;
    }
}

float UIManager::getExternalBpm() const
{
    return m_externalBpm;
}

void UIManager::setSyncMode(bool enabled)
{
    if (m_syncMode != enabled) {
        m_syncMode = enabled;
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
