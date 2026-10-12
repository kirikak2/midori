#include "sdkconfig.h"
#include "screen_layout.h"
#include "screen_xypad.h"
#include "screen_tombola.h"
#include "ui_layout.h"
#include "ui_strip.h"
#include "ui_widgets.h"
#include <M5Unified.h>
#include <cstring>
#include <cstdio>

// Global instance
static ScreenLayout s_screenLayout;

ScreenLayout& getScreenLayout()
{
    return s_screenLayout;
}

static constexpr float SWEEP_RAD = UI_KNOB_ANGLE_SWEEP * 3.14159265f / 180.0f;

static bool isView(uint8_t kind)
{
    return kind == UI_WIDGET_XYPAD || kind == UI_WIDGET_TOMBOLA;
}

ScreenLayout::ScreenLayout()
    : m_isActive(false)
    , m_slot(-1)
    , m_version(0)
    , m_hasXYPad(false)
    , m_hasTombola(false)
{
    m_title[0] = '\0';
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        m_touchOwner[i] = -1;
        m_ownerKind[i] = 0;
        m_lastX[i] = 0;
        m_lastY[i] = 0;
        m_accum[i] = 0.0f;
        m_moved[i] = false;
    }
}

// --- Views ------------------------------------------------------------------

void ScreenLayout::attachViews(LayoutDef* def)
{
    m_hasXYPad = false;
    m_hasTombola = false;
    if (!def) return;

    for (int i = 0; i < def->count; i++) {
        const ui_widget_t& wd = def->widgets[i];
        if (wd.kind == UI_WIDGET_XYPAD && !m_hasXYPad) {
            getScreenXYPad().setArea(wd.x, wd.y, wd.w, wd.h);
            getScreenXYPad().enter();
            m_hasXYPad = true;
        } else if (wd.kind == UI_WIDGET_TOMBOLA && !m_hasTombola) {
            getScreenTombola().setArea(wd.x, wd.y, wd.w, wd.h);
            getScreenTombola().enter();
            m_hasTombola = true;
        }
    }
}

// Hands the views back with their full-screen area, so their own screens
// find them as they left them.
void ScreenLayout::detachViews()
{
    if (m_hasXYPad) {
        getScreenXYPad().leave();
        getScreenXYPad().resetArea();
        m_hasXYPad = false;
    }
    if (m_hasTombola) {
        getScreenTombola().leave();
        getScreenTombola().resetArea();
        m_hasTombola = false;
    }
}

// --- Geometry & drawing -----------------------------------------------------

int ScreenLayout::hitTest(LayoutDef* def, int x, int y)
{
    for (int i = 0; i < def->count; i++) {
        const ui_widget_t& wd = def->widgets[i];
        if (x >= wd.x && x < wd.x + wd.w && y >= wd.y && y < wd.y + wd.h) return i;
    }
    return -1;
}

bool ScreenLayout::isHeld(int id, int exceptTouch)
{
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (i != exceptTouch && m_touchOwner[i] == id) return true;
    }
    return false;
}

void ScreenLayout::paintWidget(LayoutDef* def, int id)
{
    if (!def || id < 0 || id >= def->count) return;
    const ui_widget_t& wd = def->widgets[id];

    if (wd.kind == UI_WIDGET_PAD) {
        int size = ui_pad_text_size_for(wd.w, wd.h, wd.pad.label);
        auto render = [&](LovyanGFX* g, int ox, int oy) {
            ui_pad_paint(g, ox, oy, wd.x, wd.y, wd.w, wd.h, &wd.pad, wd.pad.label, size);
        };
        if (!ui_strip::paint(wd.x, wd.y, wd.w, wd.h, UI_COLOR_BLACK, render)) {
            render(&ui_gfx(), 0, 0);
        }
    } else if (wd.kind == UI_WIDGET_KNOB) {
        KnobGeom geom = ui_knob_geom_for(wd.x, wd.y, wd.w, wd.h);
        bool grabbed = isHeld(id, -1);
        auto render = [&](LovyanGFX* g, int ox, int oy) {
            ui_knob_paint(g, ox, oy, geom, &wd.knob, wd.knob.label, grabbed);
        };
        if (!ui_strip::paint(wd.x, wd.y, wd.w, wd.h, UI_COLOR_BLACK, render)) {
            render(&ui_gfx(), 0, 0);
        }
    }
}

// --- Screen lifecycle -------------------------------------------------------

void ScreenLayout::onShowArg(int arg)
{
    if (arg >= 0 && arg < UI_LAYOUT_MAX_LAYOUTS) m_slot = arg;
}

void ScreenLayout::enter()
{
    m_isActive = true;
    ui_layout::set_last_shown(m_slot);
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) m_touchOwner[i] = -1;

    ui_layout::lock();
    LayoutDef* def = ui_layout::get(m_slot);
    m_version = def ? def->version : 0;
    snprintf(m_title, sizeof(m_title), "%s", def ? def->title : "");
    attachViews(def);
    // UIManager repaints everything right after this
    if (def) ui_layout::take_dirty(def);
    ui_layout::unlock();
}

void ScreenLayout::leave()
{
    ui_layout::lock();
    releaseTouches(ui_layout::get(m_slot), true);
    ui_layout::unlock();
    detachViews();
    m_isActive = false;
}

// Lets go of everything the fingers hold. With notify, pads report their
// release and knobs their final value, as if the fingers had lifted; def is
// nullptr when the layout they were on is gone, and nothing is reported.
void ScreenLayout::releaseTouches(LayoutDef* def, bool notify)
{
    for (int t = 0; t < MAX_TOUCH_POINTS; t++) {
        int id = m_touchOwner[t];
        if (id < 0) continue;
        m_touchOwner[t] = -1;

        if (isView(m_ownerKind[t])) {
            // The view's model would otherwise keep a note held
            if (m_ownerKind[t] == UI_WIDGET_XYPAD) {
                getScreenXYPad().onTouch(t, m_lastX[t], m_lastY[t], false);
            } else {
                getScreenTombola().onTouch(t, m_lastX[t], m_lastY[t], false);
            }
            continue;
        }
        if (!def || !notify || id >= def->count) continue;

        ui_widget_t& wd = def->widgets[id];
        if (wd.kind == UI_WIDGET_PAD && !isHeld(id, -1)) {
            padPress(def, id, false);
        } else if (wd.kind == UI_WIDGET_KNOB && m_moved[t] && wd.knob.notify) {
            ui_widget_knob_event_post((uint8_t)m_slot, (uint8_t)id, wd.knob.value, true);
        }
        m_moved[t] = false;
    }
}

void ScreenLayout::update()
{
    if (!m_isActive) return;

    ui_layout::lock();
    LayoutDef* def = ui_layout::get(m_slot);
    if (!def) {
        // Deleted or its script ended; UIManager falls back to Home
        ui_layout::unlock();
        return;
    }

    if (def->version != m_version) {
        // Redefined under us: a different set of widgets in different places.
        // The old one is already freed, so nothing can be reported for it.
        releaseTouches(nullptr, false);
        detachViews();
        m_version = def->version;
        snprintf(m_title, sizeof(m_title), "%s", def->title);
        attachViews(def);
        ui_layout::take_dirty(def);
        ui_layout::unlock();
        ui_request_redraw();
        return;
    }

    // Labels, colours and values changed from Ruby
    uint32_t dirty = ui_layout::take_dirty(def);
    for (int i = 0; dirty && i < def->count; i++) {
        if (dirty & (1u << i)) paintWidget(def, i);
    }
    ui_layout::unlock();

    // The views animate on their own schedule, inside their cells
    if (m_hasXYPad) getScreenXYPad().update();
    if (m_hasTombola) getScreenTombola().update();
}

void ScreenLayout::draw()
{
    ui_layout::lock();
    LayoutDef* def = ui_layout::get(m_slot);
    if (def) {
        for (int i = 0; i < def->count; i++) {
            if (!isView(def->widgets[i].kind)) paintWidget(def, i);
        }
    }
    ui_layout::unlock();

    if (m_hasXYPad) getScreenXYPad().draw();
    if (m_hasTombola) getScreenTombola().draw();
}

// --- Touch ------------------------------------------------------------------

void ScreenLayout::padPress(LayoutDef* def, int id, bool pressed)
{
    pad_config_t& pad = def->widgets[id].pad;

    switch (pad.type) {
        case PAD_TYPE_TRIGGER:
        case PAD_TYPE_MOMENTARY:
            pad.state = pressed;
            break;
        case PAD_TYPE_TOGGLE:
            if (pressed) pad.state = !pad.state;
            break;
    }
    paintWidget(def, id);

    ui_event_t event;
    event.type = UI_EVENT_WIDGET;
    event.data.widget.layout = (uint8_t)m_slot;
    event.data.widget.id = (uint8_t)id;
    event.data.widget.kind = UI_WIDGET_PAD;
    event.data.widget.phase = pressed ? UI_WIDGET_PRESS : UI_WIDGET_RELEASE;
    event.data.widget.state = pad.state;
    event.data.widget.final = false;
    event.data.widget.value = 0.0f;
    ui_event_push(&event);
}

void ScreenLayout::onTouch(int touchId, int x, int y, bool pressed)
{
    if (!m_isActive || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;

    if (pressed) {
        if (m_touchOwner[touchId] >= 0) return;

        ui_layout::lock();
        LayoutDef* def = ui_layout::get(m_slot);
        int id = def ? hitTest(def, x, y) : -1;
        if (id < 0) {
            ui_layout::unlock();
            return;
        }
        ui_widget_t& wd = def->widgets[id];
        uint8_t kind = wd.kind;

        if (kind == UI_WIDGET_KNOB && isHeld(id, touchId)) {
            // One finger per knob: a second would add its angular velocity
            // to the first's and make the value lurch.
            ui_layout::unlock();
            return;
        }

        m_touchOwner[touchId] = id;
        m_ownerKind[touchId] = kind;
        m_lastX[touchId] = x;
        m_lastY[touchId] = y;
        m_moved[touchId] = false;

        if (kind == UI_WIDGET_PAD) {
            if (!isHeld(id, touchId)) padPress(def, id, true);
        } else if (kind == UI_WIDGET_KNOB) {
            m_accum[touchId] = wd.knob.value;
            paintWidget(def, id);   // Grab ring
        }
        ui_layout::unlock();

        if (kind == UI_WIDGET_XYPAD) getScreenXYPad().onTouch(touchId, x, y, true);
        if (kind == UI_WIDGET_TOMBOLA) getScreenTombola().onTouch(touchId, x, y, true);
        return;
    }

    int id = m_touchOwner[touchId];
    if (id < 0) return;
    m_touchOwner[touchId] = -1;
    uint8_t kind = m_ownerKind[touchId];

    if (kind == UI_WIDGET_XYPAD) {
        getScreenXYPad().onTouch(touchId, x, y, false);
        return;
    }
    if (kind == UI_WIDGET_TOMBOLA) {
        getScreenTombola().onTouch(touchId, x, y, false);
        return;
    }

    ui_layout::lock();
    LayoutDef* def = ui_layout::get(m_slot);
    if (def && id < def->count && def->widgets[id].kind == kind) {
        ui_widget_t& wd = def->widgets[id];
        if (kind == UI_WIDGET_PAD) {
            if (!isHeld(id, -1)) padPress(def, id, false);
        } else if (kind == UI_WIDGET_KNOB) {
            paintWidget(def, id);   // Without the grab ring
            // The value left behind has to reach the script even if it
            // dropped every intermediate one
            if (m_moved[touchId] && wd.knob.notify) {
                ui_widget_knob_event_post((uint8_t)m_slot, (uint8_t)id, wd.knob.value, true);
            }
        }
    }
    m_moved[touchId] = false;
    ui_layout::unlock();
}

void ScreenLayout::onTouchMove(int touchId, int x, int y)
{
    if (!m_isActive || touchId < 0 || touchId >= MAX_TOUCH_POINTS) return;
    int id = m_touchOwner[touchId];
    if (id < 0) return;

    uint8_t kind = m_ownerKind[touchId];
    if (kind == UI_WIDGET_XYPAD) {
        m_lastX[touchId] = x;
        m_lastY[touchId] = y;
        getScreenXYPad().onTouchMove(touchId, x, y);
        return;
    }
    if (kind == UI_WIDGET_TOMBOLA) {
        m_lastX[touchId] = x;
        m_lastY[touchId] = y;
        getScreenTombola().onTouchMove(touchId, x, y);
        return;
    }
    if (kind != UI_WIDGET_KNOB) return;

    int dx = x - m_lastX[touchId];
    int dy = y - m_lastY[touchId];
    m_lastX[touchId] = x;
    m_lastY[touchId] = y;
    if (dx == 0 && dy == 0) return;

    ui_layout::lock();
    LayoutDef* def = ui_layout::get(m_slot);
    if (!def || id >= def->count || def->widgets[id].kind != UI_WIDGET_KNOB) {
        ui_layout::unlock();
        return;
    }
    ui_widget_t& wd = def->widgets[id];
    knob_config_t& k = wd.knob;

    // Same gesture as the Knobs screen: the knob turns by the angle the
    // finger sweeps around its centre, accumulated unquantized.
    KnobGeom geom = ui_knob_geom_for(wd.x, wd.y, wd.w, wd.h);
    float dtheta = ui_knob_drag_angle(geom, x, y, dx, dy);
    float span = k.max - k.min;
    m_accum[touchId] += span * dtheta / SWEEP_RAD * k.sensitivity;
    if (m_accum[touchId] < k.min) m_accum[touchId] = k.min;
    if (m_accum[touchId] > k.max) m_accum[touchId] = k.max;

    float q = ui_knob_quantize(&k, m_accum[touchId]);
    if (q != k.value) {
        k.value = q;
        m_moved[touchId] = true;
        paintWidget(def, id);
        if (k.notify) {
            ui_widget_knob_event_post((uint8_t)m_slot, (uint8_t)id, k.value, false);
        }
    }
    ui_layout::unlock();
}

const char* ScreenLayout::getTitle()
{
    return m_title;
}

const char* ScreenLayout::getNavCenterLabel()
{
    return "";
}
