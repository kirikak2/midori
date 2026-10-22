#include "ui_manager.h"
#include "screen_log.h"
#include "screen_main.h"
#include "screen_pad.h"
#include "screen_midi_devices.h"
#include "screen_script.h"
#include "screen_settings.h"
#include "screen_tombola.h"
#include "screen_knob.h"
#include "screen_xypad.h"
#include "screen_keys.h"
#include "screen_home.h"
#include "screen_layout.h"
#include "ui_layout.h"
#include "ui_strip.h"
#include <M5Unified.h>
#include "esp_log.h"

extern "C" {
#include "midi.h"
#include "midi_route.h"
#include "midi_transport.h"
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
    , m_currentArg(-1)
    , m_previousIndex(-1)
    , m_previousArg(-1)
    , m_menuHidden(0)
    , m_requestMux(portMUX_INITIALIZER_UNLOCKED)
    , m_pendingIndex(-1)
    , m_pendingArg(-1)
    , m_initialized(false)
    , m_needsRedraw(true)
    , m_internalBpm(UI_BPM_DEFAULT)
    , m_externalBpm(0.0f)
    , m_selectedExternalBpmSource(MIDI_INTERFACE_USB)
    , m_syncMode(false)
    , m_bpmChangeCallback(nullptr)
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
    for (int i = 0; i < MIDI_INTERFACE_COUNT; i++) {
        m_externalBpmBySource[i] = 0.0f;
    }
    m_clockWatch = 0;
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

    // Before anything else claims internal RAM: the screens' off-screen
    // buffer only fits there if it is taken while the heap is still whole.
    ui_strip::init();
    ui_layout::init();
    // The tempo's clock is a MIDI device from the start (MIDI Devices, MIDI.route)
    ui_tempo_init();

    // Initialize screens (placeholders for now)
    initScreens();

    // Reset any text-scroll state left over from lcd_console boot screen.
    // Without this, setTextScroll(true) and setScrollRect from lcd_console_init
    // remain active globally and cause flicker (the scroll-rect area gets
    // filled with the scroll-rect's bgcolor whenever a print() crosses an
    // invisible boundary).
    M5.Lcd.setTextScroll(false);
    M5.Lcd.setScrollRect(0, 0, M5.Lcd.width(), M5.Lcd.height());

    // Enter the first screen. The first update() paints it, replacing the
    // boot console in one go.
    m_currentIndex = UI_SCREEN_HOME;
    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->enter();
    }

    m_initialized = true;
    m_needsRedraw = true;

    return ESP_OK;
}

void UIManager::initScreens()
{
    // Initialize all screens
    m_screens[UI_SCREEN_MAIN] = &getScreenMain();
    m_screens[UI_SCREEN_PADS] = &getScreenPads();
    m_screens[UI_SCREEN_MIDI_INFO] = &getScreenMidiDevices();
    m_screens[UI_SCREEN_LOGS] = &getScreenLog();
    m_screens[UI_SCREEN_SCRIPTS] = &getScreenScripts();
    m_screens[UI_SCREEN_SETTINGS] = &getScreenSettings();
    m_screens[UI_SCREEN_TOMBOLA] = &getScreenTombola();
    m_screens[UI_SCREEN_KNOBS] = &getScreenKnobs();
    m_screens[UI_SCREEN_XYPAD] = &getScreenXYPad();
    m_screens[UI_SCREEN_LAYOUT] = &getScreenLayout();
    m_screens[UI_SCREEN_HOME] = &getScreenHome();
    m_screens[UI_SCREEN_KEYS] = &getScreenKeys();
}

void UIManager::update()
{
    if (!m_initialized) return;

    // Update external BPM from C side (every 100ms)
    static uint32_t lastBpmUpdate = 0;
    uint32_t now = esp_timer_get_time() / 1000;  // Convert to ms
    if (now - lastBpmUpdate > 100) {
        updateClockWatch();

        float usbBpm = MIDI_Input_get_external_bpm_usb();
        float samBpm = MIDI_Input_get_external_bpm_sam();
        float devBpm = MIDI_Input_get_external_bpm_from(MIDI_TRANSPORT_USB_DEVICE);

        // Track if any source just started receiving BPM
        bool usbStarted = (m_externalBpmBySource[MIDI_INTERFACE_USB] <= 0.0f && usbBpm > 0.0f);
        bool samStarted = (m_externalBpmBySource[MIDI_INTERFACE_DIN] <= 0.0f && samBpm > 0.0f);
        bool devStarted = (m_externalBpmBySource[MIDI_INTERFACE_USB_DEVICE] <= 0.0f && devBpm > 0.0f);

        setExternalBpmSource(MIDI_INTERFACE_USB, usbBpm);
        setExternalBpmSource(MIDI_INTERFACE_DIN, samBpm);
        // BLE not implemented yet
        setExternalBpmSource(MIDI_INTERFACE_BLE, 0.0f);
        setExternalBpmSource(MIDI_INTERFACE_USB_DEVICE, devBpm);

        // Auto-select source if currently selected source has no BPM
        float currentSourceBpm = m_externalBpmBySource[m_selectedExternalBpmSource];
        if (currentSourceBpm <= 0.0f) {
            // Try to find a source with BPM (prefer DIN > USB host > USB device > BLE)
            if (samBpm > 0.0f) {
                setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
            } else if (usbBpm > 0.0f) {
                setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
            } else if (devBpm > 0.0f) {
                setExternalBpmSourceSelection(MIDI_INTERFACE_USB_DEVICE);
            }
        }
        // Also auto-select when a new source starts receiving (prefer DIN)
        else if (samStarted) {
            setExternalBpmSourceSelection(MIDI_INTERFACE_DIN);
        } else if (usbStarted && m_selectedExternalBpmSource != MIDI_INTERFACE_DIN) {
            setExternalBpmSourceSelection(MIDI_INTERFACE_USB);
        } else if (devStarted && m_selectedExternalBpmSource != MIDI_INTERFACE_DIN
                   && m_selectedExternalBpmSource != MIDI_INTERFACE_USB) {
            setExternalBpmSourceSelection(MIDI_INTERFACE_USB_DEVICE);
        }

        lastBpmUpdate = now;
    }

    // Switches requested from other tasks (UI.show) happen here, on the task
    // that owns the screens, before this pass touches or draws anything.
    applyPendingRequest();

    // Handle touch input
    handleTouch();

    // Step the tombola sequencer. Deliberately outside the current-screen
    // check: a running patch must keep playing while the user is looking at
    // another screen.
    ui_tombola_tick();
    ui_mididev_tick();
    ui_tempo_tick();

    // Update current screen. Not when a full repaint is pending (a screen
    // was just switched to, say): update() draws its incremental changes
    // straight to the panel, over whatever is still showing, and the repaint
    // below covers all of it anyway.
    if (m_screens[m_currentIndex] && !m_needsRedraw) {
        m_screens[m_currentIndex]->update();
    }

    // Full repaint: the whole screen is composed off-screen through the
    // shared strip and replaced band by band, so the panel never shows it
    // cleared or half-drawn (screens no longer draw in enter()). Without the
    // strip it is drawn straight to the panel.
    if (m_needsRedraw) {
        Screen* screen = m_screens[m_currentIndex];
        auto paintAll = [&]() {
            drawStatusBar();
            ui_clear_content_area();
            if (screen) screen->draw();
            drawNavBar();
        };
        if (!ui_strip::compose(0, UI_SCREEN_HEIGHT, paintAll)) {
            M5.Lcd.startWrite();
            paintAll();
            M5.Lcd.endWrite();
        }
        m_needsRedraw = false;
    }

    // Push this pass's drawing out of the CPU cache.
    //
    // Tab5's panel is a framebuffer panel: LovyanGFX draws with the CPU into
    // cached write-back PSRAM, and the MIPI-DSI scans that memory out by DMA
    // without snooping the cache. Panel_FrameBufferBase tracks what was
    // modified and writes those lines back -- but only in display(), which
    // endWrite() does not call. Nothing here called it, so drawing reached the
    // panel only when a cache line happened to be evicted: in whatever order,
    // whenever, and never at all for a region small enough to sit in cache
    // undisturbed. The screen showed a mixture of new and stale pixels.
    //
    // Large fills hid this by evicting each other, which is why it only became
    // obvious with the Knobs screen, whose drawing is a scatter of thin arcs
    // and three-character numbers.
    //
    // display() writes back just the modified range, costs nothing when
    // nothing was drawn, and is an empty no-op on CoreS3's SPI panel, where
    // the pixels went out over the bus as they were drawn.
    M5.Lcd.display();
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
                        goHome();
                    } else if (x >= UI_NAV_ZONE_RIGHT_START) {
                        goBack();
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
    ui_draw_nav_bar(centerLabel, m_currentIndex != UI_SCREEN_HOME, canGoBack());
}

// --- Navigation --------------------------------------------------------------

// The Home menu, in display order. UI_SCREEN_LAYOUT stands for every live
// layout, each of which becomes an item of its own.
static const struct { ui_screen_index_t screen; uint8_t group; } MENU_ORDER[] = {
    { UI_SCREEN_PADS,      MENU_GROUP_PERFORM },
    { UI_SCREEN_KNOBS,     MENU_GROUP_PERFORM },
    { UI_SCREEN_XYPAD,     MENU_GROUP_PERFORM },
    { UI_SCREEN_KEYS,      MENU_GROUP_PERFORM },
    { UI_SCREEN_TOMBOLA,   MENU_GROUP_PERFORM },
    { UI_SCREEN_LAYOUT,    MENU_GROUP_PERFORM },
    { UI_SCREEN_MAIN,      MENU_GROUP_STATE },
    { UI_SCREEN_MIDI_INFO, MENU_GROUP_STATE },
    { UI_SCREEN_SCRIPTS,   MENU_GROUP_SYSTEM },
    { UI_SCREEN_LOGS,      MENU_GROUP_SYSTEM },
    { UI_SCREEN_SETTINGS,  MENU_GROUP_SYSTEM },
};

// Screens a script may never hide: without them there is no way back to the
// menu, or to stopping the script that hid everything.
static constexpr uint32_t MENU_ALWAYS_VISIBLE =
    (1u << UI_SCREEN_HOME) | (1u << UI_SCREEN_SCRIPTS);

bool UIManager::isAvailable(ui_screen_index_t index, int arg) const
{
    if (index < 0 || index >= UI_SCREEN_COUNT) return false;
    if (!m_screens[index]) return false;
    if ((m_menuHidden & ~MENU_ALWAYS_VISIBLE) & (1u << index)) return false;
    if (index == UI_SCREEN_LAYOUT) {
        // -1 means "the layout last shown"; it still has to exist
        if (arg < 0) arg = ui_layout_last_shown();
        return ui_layout_exists(arg);
    }
    return true;
}

int UIManager::buildMenu(MenuItem* out, int max) const
{
    int n = 0;
    for (const auto& e : MENU_ORDER) {
        if (e.screen == UI_SCREEN_LAYOUT) {
            for (int slot = 0; slot < UI_LAYOUT_MAX_LAYOUTS; slot++) {
                if (n >= max) return n;
                if (!isAvailable(UI_SCREEN_LAYOUT, slot)) continue;
                out[n++] = { UI_SCREEN_LAYOUT, slot, e.group };
            }
            continue;
        }
        if (n >= max) return n;
        if (!isAvailable(e.screen, -1)) continue;
        out[n++] = { e.screen, -1, e.group };
    }
    return n;
}

void UIManager::setMenuHidden(uint32_t hidden)
{
    m_menuHidden = hidden & ~MENU_ALWAYS_VISIBLE;
    // The tiles and the Back button may have changed
    if (m_currentIndex == UI_SCREEN_HOME || m_previousIndex >= 0) {
        m_needsRedraw = true;
    }
}

uint32_t UIManager::getMenuHidden() const
{
    return m_menuHidden;
}

void UIManager::setScreen(ui_screen_index_t index, int arg, ui_screen_source_t source)
{
    if (index < 0 || index >= UI_SCREEN_COUNT || !m_screens[index]) {
        return;
    }
    if (index == UI_SCREEN_LAYOUT && arg < 0) {
        arg = ui_layout_last_shown();
    }

    // Already there: only the argument (a Knobs bank) can still change. A
    // different layout is a different page, so that does switch for real.
    if (index == m_currentIndex &&
        !(index == UI_SCREEN_LAYOUT && arg != m_currentArg)) {
        if (arg >= 0) m_screens[index]->onShowArg(arg);
        return;
    }

    int from = m_currentIndex;
    int fromArg = m_currentArg;

    if (m_screens[m_currentIndex]) {
        m_screens[m_currentIndex]->leave();
    }

    m_currentIndex = index;
    m_currentArg = (index == UI_SCREEN_LAYOUT) ? arg : -1;
    if (arg >= 0) {
        m_screens[m_currentIndex]->onShowArg(arg);
    }
    m_screens[m_currentIndex]->enter();

    // Back skips Home: going A -> Home -> B leaves Back pointing at A, so it
    // flips between the two screens actually in use.
    if (from != UI_SCREEN_HOME) {
        m_previousIndex = from;
        m_previousArg = fromArg;
    }

    m_needsRedraw = true;

    ui_event_t event;
    event.type = UI_EVENT_SCREEN_CHANGE;
    event.data.screen.index = (uint8_t)index;
    event.data.screen.from = (uint8_t)from;
    event.data.screen.source = (uint8_t)source;
    event.data.screen.layout = (int8_t)m_currentArg;
    ui_event_push(&event);

    ESP_LOGD(TAG, "Switched to screen %d (arg %d)", m_currentIndex, arg);
}

void UIManager::goHome()
{
    setScreen(UI_SCREEN_HOME, -1, UI_SCREEN_SOURCE_TOUCH);
}

bool UIManager::canGoBack() const
{
    int prev = m_previousIndex;
    if (prev < 0) return false;
    if (prev == m_currentIndex && m_previousArg == m_currentArg) return false;
    return isAvailable((ui_screen_index_t)prev, m_previousArg);
}

void UIManager::goBack()
{
    if (!canGoBack()) return;
    setScreen((ui_screen_index_t)m_previousIndex, m_previousArg, UI_SCREEN_SOURCE_TOUCH);
}

bool UIManager::resolveStep(int dir, ui_screen_index_t& index, int& arg) const
{
    MenuItem items[MENU_MAX_ITEMS];
    int n = buildMenu(items, MENU_MAX_ITEMS);
    if (n == 0) return false;

    int pos = -1;
    for (int i = 0; i < n; i++) {
        if (items[i].screen == m_currentIndex &&
            (items[i].screen != UI_SCREEN_LAYOUT || items[i].arg == m_currentArg)) {
            pos = i;
            break;
        }
    }

    int next;
    if (pos < 0) {
        // On Home (or a screen that just got hidden): start from either end
        next = (dir > 0) ? 0 : n - 1;
    } else {
        next = (pos + (dir > 0 ? 1 : n - 1)) % n;
    }
    index = items[next].screen;
    arg = items[next].arg;
    return true;
}

bool UIManager::requestScreen(ui_screen_index_t index, int arg)
{
    if (!isAvailable(index, arg)) return false;

    portENTER_CRITICAL(&m_requestMux);
    m_pendingIndex = index;
    m_pendingArg = arg;
    portEXIT_CRITICAL(&m_requestMux);
    return true;
}

bool UIManager::requestBack()
{
    // Resolved now, against what is on screen now, so UI.screen can report
    // the destination before the UI task gets round to it.
    if (!canGoBack()) return false;
    return requestScreen((ui_screen_index_t)m_previousIndex, m_previousArg);
}

bool UIManager::requestStep(int dir)
{
    ui_screen_index_t index;
    int arg;
    if (!resolveStep(dir, index, arg)) return false;
    return requestScreen(index, arg);
}

ui_screen_index_t UIManager::getRequestedScreen(int* arg) const
{
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&m_requestMux));
    int index = m_pendingIndex;
    int pendingArg = m_pendingArg;
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&m_requestMux));

    if (index < 0) {
        index = m_currentIndex;
        pendingArg = m_currentArg;
    } else if (index == UI_SCREEN_LAYOUT && pendingArg < 0) {
        pendingArg = ui_layout_last_shown();
    }
    if (arg) *arg = (index == UI_SCREEN_LAYOUT) ? pendingArg : -1;
    return (ui_screen_index_t)index;
}

void UIManager::applyPendingRequest()
{
    portENTER_CRITICAL(&m_requestMux);
    int index = m_pendingIndex;
    int arg = m_pendingArg;
    m_pendingIndex = -1;
    portEXIT_CRITICAL(&m_requestMux);

    if (index >= 0) {
        setScreen((ui_screen_index_t)index, arg, UI_SCREEN_SOURCE_SCRIPT);
    }

    // Free the layouts of a script that has ended
    ui_layout::collect();

    // The layout on screen was deleted (or its script ended): fall back to the
    // menu rather than showing a page nothing answers to any more.
    if (m_currentIndex == UI_SCREEN_LAYOUT && !ui_layout_exists(m_currentArg)) {
        setScreen(UI_SCREEN_HOME, -1, UI_SCREEN_SOURCE_SYSTEM);
    }
}

void UIManager::scriptCleanup()
{
    m_menuHidden = 0;
    ui_layout_cleanup();
    ui_keys_cleanup();
    ui_mididev_cleanup();
    ui_tempo_cleanup();
    m_needsRedraw = true;
}

ui_screen_index_t UIManager::getCurrentScreenIndex() const
{
    return static_cast<ui_screen_index_t>(m_currentIndex);
}

int UIManager::getCurrentArg() const
{
    return m_currentArg;
}

Screen* UIManager::getCurrentScreen()
{
    return m_screens[m_currentIndex];
}

Screen* UIManager::getScreen(ui_screen_index_t index)
{
    if (index < 0 || index >= UI_SCREEN_COUNT) return nullptr;
    return m_screens[index];
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

// The input task reads the USB device and the UART only while something is
// routed from them, so their clock would go unmeasured. While the Tempo screen
// is up or Sync is on, ask for every source that is there to be read.
void UIManager::updateClockWatch()
{
    uint8_t want = 0;
    if (m_currentIndex == UI_SCREEN_MAIN || m_syncMode) {
#if CONFIG_USB_MIDI_HOST_ENABLED
        want |= MIDI_TRANSPORT_USB;   // Read once a device is plugged in
#endif
        const uint8_t others[] = { MIDI_TRANSPORT_SAM2695, MIDI_TRANSPORT_USB_DEVICE };
        for (uint8_t bit : others) {
            const midi_transport_t* t = MIDI_transport_get(bit);
            if (t && t->ops && t->ops->is_connected && t->ops->is_connected(t->ctx)) want |= bit;
        }
    }
    if (want == m_clockWatch) return;
    uint8_t added = want & (uint8_t)~m_clockWatch;
    m_clockWatch = want;
    MIDI_route_set_watch(MIDI_WATCH_APP, want);
    for (uint8_t bit = 1; bit; bit <<= 1) {
        if (added & bit) MIDI_route_start_source(bit);
    }
}

void UIManager::setExternalBpmSource(midi_interface_t source, float bpm)
{
    if (source < 0 || source >= MIDI_INTERFACE_COUNT) return;
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
    if (source < 0 || source >= MIDI_INTERFACE_COUNT) return 0.0f;
    return m_externalBpmBySource[source];
}

void UIManager::setExternalBpmSourceSelection(midi_interface_t source)
{
    if (source < 0 || source >= MIDI_INTERFACE_COUNT) return;
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
                } else if (m_externalBpmBySource[MIDI_INTERFACE_USB_DEVICE] > 0.0f) {
                    setExternalBpmSourceSelection(MIDI_INTERFACE_USB_DEVICE);
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


void ui_set_screen(ui_screen_index_t index)
{
    UIManager::getInstance().requestScreen(index, -1);
}

bool ui_request_screen(ui_screen_index_t index, int arg)
{
    return UIManager::getInstance().requestScreen(index, arg);
}

bool ui_request_back(void)
{
    return UIManager::getInstance().requestBack();
}

bool ui_request_step(int dir)
{
    return UIManager::getInstance().requestStep(dir);
}

int ui_get_requested_screen(int* arg)
{
    return (int)UIManager::getInstance().getRequestedScreen(arg);
}

void ui_menu_set_hidden(uint32_t hidden)
{
    UIManager::getInstance().setMenuHidden(hidden);
}

uint32_t ui_menu_get_hidden(void)
{
    return UIManager::getInstance().getMenuHidden();
}

void ui_script_cleanup(void)
{
    UIManager::getInstance().scriptCleanup();
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
