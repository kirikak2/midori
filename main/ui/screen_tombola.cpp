#include "sdkconfig.h"
#include "screen_tombola.h"
#include <M5Unified.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern "C" {
#include "midi.h"
}

static const char* TAG = "SCREEN_TOMBOLA";

// Global instance
static ScreenTombola s_screenTombola;

ScreenTombola& getScreenTombola()
{
    return s_screenTombola;
}

// ---------------------------------------------------------------------------
// Tuning constants
// ---------------------------------------------------------------------------

// Acceleration applied at gravity == 1.0, in circumradii per second squared.
// A ball dropped from the top of the polygon then crosses it in about 1s.
static constexpr float GRAVITY_UNIT = 4.0f;

// Speed clamp (circumradii/s). Balls cannot escape the polygon regardless --
// collision is a half-space test, so an overshooting ball is simply pushed
// back next step -- but a clamp keeps the motion readable and the notes sane.
static constexpr float MAX_SPEED = 8.0f;

// Impact speed (circumradii/s) mapped to the bottom / top of velocity_range.
static constexpr float IMPACT_SOFT = 0.2f;
static constexpr float IMPACT_HARD = 4.0f;

// A stalled UI task must not make the simulation explode on the next step.
static constexpr float MAX_DT = 0.05f;

static constexpr int64_t FLASH_US = 90000;  // Hit highlight duration

// Frame pacing. Pushing the play area is the dominant cost of this screen
// (620x620x16bpp is 768KB a frame on Tab5, and CoreS3 pushes its 65KB over
// SPI), and it buys little above 30fps for balls this size. Physics keeps its
// own 10ms step either way, so the note timing does not change with this.
#if defined(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
static constexpr int64_t DRAW_INTERVAL_US = 33000;  // ~30fps
static constexpr int HUD_TEXT_SIZE = 2;
#else
static constexpr int64_t DRAW_INTERVAL_US = 50000;  // ~20fps
static constexpr int HUD_TEXT_SIZE = 1;
#endif

// The play area: the largest square that fits the content area, centred. Both
// the sprite and the polygon are sized from this, so the buffer never has to
// be reallocated when the radius parameter changes.
// Tab5: 620x620 (~768KB @16bpp) / CoreS3: 180x180 (~65KB).
static constexpr int BOX_SIZE =
    (UI_SCREEN_WIDTH < UI_CONTENT_HEIGHT) ? UI_SCREEN_WIDTH : UI_CONTENT_HEIGHT;
static constexpr int BOX_X = (UI_SCREEN_WIDTH - BOX_SIZE) / 2;
static constexpr int BOX_Y = UI_CONTENT_Y + (UI_CONTENT_HEIGHT - BOX_SIZE) / 2;

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

namespace {

struct Ball {
    bool active;
    float x, y;          // Position, circumradius == 1.0, origin at centre
    float vx, vy;        // Velocity, circumradii/s
    int note;            // -1 => take the note from scale[side]
    int channel;         // -1 => use the sequencer's default channel
    uint16_t color;
    float velScale;
    int64_t lastHitUs;
};

struct Hit {
    uint8_t ball;
    uint8_t side;
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
};

struct Model {
    // Geometry
    int sides;
    float rotationRpm;   // Signed: negative spins the other way
    float radiusRatio;   // Fraction of the usable content area

    // Physics
    float gravity;
    int gravityMode;
    float bounce;
    float friction;
    float spinTransfer;
    float ballSize;

    // Sound
    uint8_t scale[UI_TOMBOLA_MAX_SCALE];
    int scaleLen;
    int channel;
    int durationMs;
    int velMin, velMax;
    int retriggerMs;
    int maxVoices;
    int transport;
    bool sound;          // Let the C side sound hits directly
    bool notify;         // Also queue hits for Ruby (set by Tombola#on_hit)
    bool touchAdd;       // Tapping inside the polygon spawns a ball

    // Runtime
    bool running;
    float angle;         // Current rotation, radians
    int64_t lastStepUs;
    Ball balls[UI_TOMBOLA_MAX_BALLS];
    int flashSide;
    int64_t flashUntilUs;
};

Model g_model;

// Both the PicoRuby task (setters) and the UI task (stepping, drawing) touch
// the model, and a step is far too long to sit in a spinlock, so use a real
// mutex. Statically allocated: it must exist before app_main runs.
StaticSemaphore_t g_mutexBuf;
SemaphoreHandle_t g_mutex = nullptr;

inline void modelLock()
{
    if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}

inline void modelUnlock()
{
    if (g_mutex) xSemaphoreGive(g_mutex);
}

float randUnit()  // [-1, 1)
{
    return (float)((int32_t)(esp_random() & 0xFFFF) - 32768) / 32768.0f;
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float clampFloat(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Default General MIDI drum voices: audible on the SAM2695 without any setup.
const uint8_t DEFAULT_SCALE[6] = { 36, 38, 42, 45, 46, 49 };

void modelDefaults()
{
    Model& m = g_model;
    memset(&m, 0, sizeof(m));

    m.sides = 6;
    m.rotationRpm = 12.0f;
    m.radiusRatio = 0.85f;

    m.gravity = 0.5f;
    m.gravityMode = TOMBOLA_GRAVITY_DOWN;
    m.bounce = 0.8f;
    m.friction = 0.99f;
    m.spinTransfer = 0.3f;
    m.ballSize = 0.05f;

    memcpy(m.scale, DEFAULT_SCALE, sizeof(DEFAULT_SCALE));
    m.scaleLen = (int)sizeof(DEFAULT_SCALE);
    m.channel = 9;
    m.durationMs = 120;
    m.velMin = 40;
    m.velMax = 127;
    m.retriggerMs = 30;
    m.maxVoices = 8;
    m.transport = MIDI_TRANSPORT_ALL;
    m.sound = true;
    m.notify = false;
    m.touchAdd = true;

    m.running = false;
    m.angle = 0.0f;
    m.lastStepUs = 0;
    m.flashSide = -1;
    m.flashUntilUs = 0;
}

// Caller holds the lock.
int spawnBall(float nx, float ny, int note, int channel, uint16_t color, float velScale)
{
    Model& m = g_model;
    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        if (m.balls[i].active) continue;
        Ball& b = m.balls[i];
        b.active = true;
        b.x = nx;
        b.y = ny;
        b.vx = randUnit() * 0.6f;
        b.vy = randUnit() * 0.3f;
        b.note = note;
        b.channel = channel;
        b.color = color;
        b.velScale = velScale;
        b.lastHitUs = 0;
        return i;
    }
    return -1;
}

// One physics step. Caller holds the lock; hits are returned rather than
// sounded here so the MIDI writes happen outside the critical section.
void stepModel(float dt, int64_t nowUs, Hit* hits, int* hitCount)
{
    Model& m = g_model;
    *hitCount = 0;

    const int sides = clampInt(m.sides, UI_TOMBOLA_MIN_SIDES, UI_TOMBOLA_MAX_SIDES);
    const float omega = m.rotationRpm * 2.0f * (float)M_PI / 60.0f;  // rad/s
    m.angle += omega * dt;
    if (m.angle > 2.0f * (float)M_PI)  m.angle -= 2.0f * (float)M_PI;
    if (m.angle < -2.0f * (float)M_PI) m.angle += 2.0f * (float)M_PI;

    const float step = 2.0f * (float)M_PI / (float)sides;
    const float apothem = cosf((float)M_PI / (float)sides);
    const float ballR = clampFloat(m.ballSize, 0.005f, 0.4f);
    const float wallLimit = apothem - ballR;
    const float wallLimitSq = wallLimit * wallLimit;

    // Outward normals, once per step rather than once per ball per side: the
    // inner loop was by far the biggest consumer of cosf/sinf in the build.
    float normX[UI_TOMBOLA_MAX_SIDES];
    float normY[UI_TOMBOLA_MAX_SIDES];
    for (int s = 0; s < sides; s++) {
        float phi = m.angle + ((float)s + 0.5f) * step;
        normX[s] = cosf(phi);
        normY[s] = sinf(phi);
    }

    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        Ball& b = m.balls[i];
        if (!b.active) continue;

        // Gravity
        switch (m.gravityMode) {
            case TOMBOLA_GRAVITY_CENTER: {
                float len = sqrtf(b.x * b.x + b.y * b.y);
                if (len > 1e-4f) {
                    float a = GRAVITY_UNIT * m.gravity * dt / len;
                    b.vx -= b.x * a;
                    b.vy -= b.y * a;
                }
                break;
            }
            case TOMBOLA_GRAVITY_NONE:
                break;
            case TOMBOLA_GRAVITY_DOWN:
            default:
                b.vy += GRAVITY_UNIT * m.gravity * dt;
                break;
        }

        float speed = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (speed > MAX_SPEED) {
            float s = MAX_SPEED / speed;
            b.vx *= s;
            b.vy *= s;
        }

        b.x += b.vx * dt;
        b.y += b.vy * dt;

        // A ball closer to the centre than the wall cannot be touching any of
        // them, because p.n <= |p| for a unit normal. Most balls are in flight
        // most frames, so this skips the side scan nearly every time.
        float distSq = b.x * b.x + b.y * b.y;
        if (distSq < wallLimitSq) continue;

        // Find the most deeply violated side. The polygon is convex, so a
        // single half-space test per side is enough.
        int worstSide = -1;
        float worstDepth = 0.0f;
        float worstNx = 0.0f, worstNy = 0.0f;
        for (int s = 0; s < sides; s++) {
            float d = b.x * normX[s] + b.y * normY[s];
            float depth = d - wallLimit;
            if (depth > worstDepth) {
                worstDepth = depth;
                worstSide = s;
                worstNx = normX[s];
                worstNy = normY[s];
            }
        }
        if (worstSide < 0) continue;

        // Push back inside
        b.x -= worstNx * worstDepth;
        b.y -= worstNy * worstDepth;

        // Velocity of the wall at the contact point (rigid rotation about the
        // centre), so a spinning polygon can actually drive the balls.
        float wallVx = -omega * b.y;
        float wallVy =  omega * b.x;

        float relVx = b.vx - wallVx;
        float relVy = b.vy - wallVy;
        float vn = relVx * worstNx + relVy * worstNy;
        if (vn <= 0.0f) {
            // Already travelling inward: repositioning is enough, and skipping
            // it here is what stops a resting ball from machine-gunning notes.
            continue;
        }

        float relNx = vn * worstNx;
        float relNy = vn * worstNy;
        float relTx = relVx - relNx;
        float relTy = relVy - relNy;

        float bounce = clampFloat(m.bounce, 0.0f, 1.2f);
        float friction = clampFloat(m.friction, 0.0f, 1.0f);
        float spin = clampFloat(m.spinTransfer, 0.0f, 1.0f);

        // Resolve in the wall's frame, then return to the world frame by adding
        // the wall velocity back whole. spin_transfer drags the tangential
        // component toward the wall's own speed instead of adding a share of it
        // on top: adding it meant a ball moving against the rotation was kicked
        // further every bounce, so the balls kept gaining speed even at
        // bounce < 1.0. Blending keeps |v_t| between the ball's and the wall's,
        // which cannot run away.
        float tangential = friction * (1.0f - spin);
        b.vx = -bounce * relNx + tangential * relTx + wallVx;
        b.vy = -bounce * relNy + tangential * relTy + wallVy;

        m.flashSide = worstSide;
        m.flashUntilUs = nowUs + FLASH_US;

        // Note emission
        if (m.retriggerMs > 0 &&
            b.lastHitUs != 0 &&
            (nowUs - b.lastHitUs) < (int64_t)m.retriggerMs * 1000) {
            continue;
        }
        if (*hitCount >= m.maxVoices || *hitCount >= UI_TOMBOLA_MAX_BALLS) {
            continue;
        }
        b.lastHitUs = nowUs;

        int note;
        if (b.note >= 0) {
            note = b.note;
        } else if (m.scaleLen > 0) {
            note = m.scale[worstSide % m.scaleLen];
        } else {
            continue;
        }

        float t = (vn - IMPACT_SOFT) / (IMPACT_HARD - IMPACT_SOFT);
        t = clampFloat(t, 0.0f, 1.0f);
        int vel = m.velMin + (int)(t * (float)(m.velMax - m.velMin));
        vel = clampInt((int)((float)vel * b.velScale), 1, 127);

        Hit& h = hits[*hitCount];
        h.ball = (uint8_t)i;
        h.side = (uint8_t)worstSide;
        h.note = (uint8_t)clampInt(note, 0, 127);
        h.velocity = (uint8_t)vel;
        h.channel = (uint8_t)clampInt(b.channel >= 0 ? b.channel : m.channel, 0, 15);
        (*hitCount)++;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// C API (declared in ui_common.h)
// ---------------------------------------------------------------------------

extern "C" void ui_tombola_reset(void)
{
    modelLock();
    modelDefaults();
    modelUnlock();
}

extern "C" void ui_tombola_start(void)
{
    modelLock();
    // Restart the clock so a long pause does not arrive as one huge step.
    g_model.lastStepUs = esp_timer_get_time();
    if (ui_tombola_ball_count() == 0) {
        // Nothing to hear otherwise; three balls is the classic starting point.
        for (int i = 0; i < 3; i++) {
            float a = (float)i * 2.0f * (float)M_PI / 3.0f;
            spawnBall(cosf(a) * 0.4f, sinf(a) * 0.4f, -1, -1, UI_COLOR_WHITE, 1.0f);
        }
    }
    g_model.running = true;
    modelUnlock();
}

extern "C" void ui_tombola_stop(void)
{
    modelLock();
    g_model.running = false;
    modelUnlock();
}

extern "C" bool ui_tombola_running(void)
{
    return g_model.running;
}

extern "C" void ui_tombola_tick(void)
{
    if (!g_model.running) return;

    int64_t now = esp_timer_get_time();
    if (g_model.lastStepUs == 0) {
        g_model.lastStepUs = now;
        return;
    }
    float dt = (float)(now - g_model.lastStepUs) / 1000000.0f;
    g_model.lastStepUs = now;
    if (dt <= 0.0f) return;
    if (dt > MAX_DT) dt = MAX_DT;

    Hit hits[UI_TOMBOLA_MAX_BALLS];
    int hitCount = 0;
    bool sound, notify;
    int transport, duration;

    modelLock();
    stepModel(dt, now, hits, &hitCount);
    sound = g_model.sound;
    notify = g_model.notify;
    transport = g_model.transport;
    duration = g_model.durationMs;
    modelUnlock();

    for (int i = 0; i < hitCount; i++) {
        if (sound) {
            MIDI_Note_trigger((uint8_t)transport, hits[i].channel,
                              hits[i].note, hits[i].velocity, (uint32_t)duration);
        }
        if (notify) {
            ui_event_t event;
            event.type = UI_EVENT_TOMBOLA_HIT;
            event.data.tombola.ball = hits[i].ball;
            event.data.tombola.side = hits[i].side;
            event.data.tombola.note = hits[i].note;
            event.data.tombola.velocity = hits[i].velocity;
            ui_event_push(&event);
        }
    }
}

extern "C" bool ui_tombola_set_f(const char* name, float value)
{
    if (!name) return false;
    Model& m = g_model;
    bool ok = true;

    modelLock();
    if      (strcmp(name, "rotation") == 0)      m.rotationRpm = clampFloat(value, -600.0f, 600.0f);
    else if (strcmp(name, "radius") == 0)        m.radiusRatio = clampFloat(value, 0.2f, 1.0f);
    else if (strcmp(name, "gravity") == 0)       m.gravity = clampFloat(value, 0.0f, 4.0f);
    else if (strcmp(name, "bounce") == 0)        m.bounce = clampFloat(value, 0.0f, 1.2f);
    else if (strcmp(name, "friction") == 0)      m.friction = clampFloat(value, 0.0f, 1.0f);
    else if (strcmp(name, "spin_transfer") == 0) m.spinTransfer = clampFloat(value, 0.0f, 1.0f);
    else if (strcmp(name, "ball_size") == 0)     m.ballSize = clampFloat(value, 0.005f, 0.4f);
    else ok = false;
    modelUnlock();

    return ok;
}

extern "C" bool ui_tombola_set_i(const char* name, int value)
{
    if (!name) return false;
    Model& m = g_model;
    bool ok = true;

    modelLock();
    if      (strcmp(name, "sides") == 0)        m.sides = clampInt(value, UI_TOMBOLA_MIN_SIDES, UI_TOMBOLA_MAX_SIDES);
    else if (strcmp(name, "gravity_mode") == 0) m.gravityMode = clampInt(value, 0, 2);
    else if (strcmp(name, "channel") == 0)      m.channel = clampInt(value, 0, 15);
    else if (strcmp(name, "duration") == 0)     m.durationMs = clampInt(value, 1, 10000);
    else if (strcmp(name, "velocity_min") == 0) m.velMin = clampInt(value, 1, 127);
    else if (strcmp(name, "velocity_max") == 0) m.velMax = clampInt(value, 1, 127);
    else if (strcmp(name, "retrigger_ms") == 0) m.retriggerMs = clampInt(value, 0, 5000);
    else if (strcmp(name, "max_voices") == 0)   m.maxVoices = clampInt(value, 1, UI_TOMBOLA_MAX_BALLS);
    else if (strcmp(name, "transport") == 0)    m.transport = value;
    else if (strcmp(name, "sound") == 0)        m.sound = (value != 0);
    else if (strcmp(name, "touch_add") == 0)    m.touchAdd = (value != 0);
    else if (strcmp(name, "notify") == 0)       m.notify = (value != 0);
    else ok = false;
    modelUnlock();

    return ok;
}

extern "C" float ui_tombola_get_f(const char* name)
{
    if (!name) return 0.0f;
    const Model& m = g_model;

    if (strcmp(name, "rotation") == 0)      return m.rotationRpm;
    if (strcmp(name, "radius") == 0)        return m.radiusRatio;
    if (strcmp(name, "gravity") == 0)       return m.gravity;
    if (strcmp(name, "bounce") == 0)        return m.bounce;
    if (strcmp(name, "friction") == 0)      return m.friction;
    if (strcmp(name, "spin_transfer") == 0) return m.spinTransfer;
    if (strcmp(name, "ball_size") == 0)     return m.ballSize;
    return 0.0f;
}

extern "C" int ui_tombola_get_i(const char* name)
{
    if (!name) return 0;
    const Model& m = g_model;

    if (strcmp(name, "sides") == 0)        return m.sides;
    if (strcmp(name, "gravity_mode") == 0) return m.gravityMode;
    if (strcmp(name, "channel") == 0)      return m.channel;
    if (strcmp(name, "duration") == 0)     return m.durationMs;
    if (strcmp(name, "velocity_min") == 0) return m.velMin;
    if (strcmp(name, "velocity_max") == 0) return m.velMax;
    if (strcmp(name, "retrigger_ms") == 0) return m.retriggerMs;
    if (strcmp(name, "max_voices") == 0)   return m.maxVoices;
    if (strcmp(name, "transport") == 0)    return m.transport;
    if (strcmp(name, "sound") == 0)        return m.sound ? 1 : 0;
    if (strcmp(name, "touch_add") == 0)    return m.touchAdd ? 1 : 0;
    if (strcmp(name, "notify") == 0)       return m.notify ? 1 : 0;
    return 0;
}

extern "C" void ui_tombola_set_scale(const uint8_t* notes, int len)
{
    if (!notes || len <= 0) return;
    if (len > UI_TOMBOLA_MAX_SCALE) len = UI_TOMBOLA_MAX_SCALE;

    modelLock();
    memcpy(g_model.scale, notes, (size_t)len);
    g_model.scaleLen = len;
    modelUnlock();
}

extern "C" int ui_tombola_add_ball(int note, int channel, uint16_t color, float velocity_scale)
{
    // Spawn on a ring inside the polygon so a ball never appears embedded in a
    // wall, at a random angle so repeated calls do not stack them up.
    float a = randUnit() * (float)M_PI;
    modelLock();
    int index = spawnBall(cosf(a) * 0.4f, sinf(a) * 0.4f, note, channel, color,
                          clampFloat(velocity_scale, 0.0f, 2.0f));
    modelUnlock();
    return index;
}

extern "C" bool ui_tombola_remove_ball(int index)
{
    if (index < 0 || index >= UI_TOMBOLA_MAX_BALLS) return false;

    modelLock();
    bool was = g_model.balls[index].active;
    g_model.balls[index].active = false;
    modelUnlock();
    return was;
}

extern "C" void ui_tombola_clear_balls(void)
{
    modelLock();
    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        g_model.balls[i].active = false;
    }
    modelUnlock();
}

extern "C" int ui_tombola_ball_count(void)
{
    int count = 0;
    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        if (g_model.balls[i].active) count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

ScreenTombola::ScreenTombola()
    : m_isActive(false)
    , m_sprite(nullptr)
    , m_drawnSides(0)
    , m_drawnAngle(0.0f)
    , m_drawnRadius(0.0f)
    , m_hasDrawnFrame(false)
    , m_lastDrawUs(0)
{
    memset(&m_lastKey, 0, sizeof(m_lastKey));
    // Runs during static init, before app_main: the model must be usable by
    // the time PicoRuby (or any other task) can reach the ui_tombola_* API.
    g_mutex = xSemaphoreCreateMutexStatic(&g_mutexBuf);
    modelDefaults();
    memset(m_drawnBalls, 0, sizeof(m_drawnBalls));
}

const char* ScreenTombola::getTitle()
{
    return "Tombola";
}

const char* ScreenTombola::getNavCenterLabel()
{
    return ui_tombola_running() ? "Stop" : "Start";
}

void ScreenTombola::enter()
{
    m_isActive = true;
    m_hasDrawnFrame = false;

    // Lazy-create the play-area sprite. Prefer internal RAM for speed; fall
    // back to PSRAM (Tab5's 620x620 buffer is ~768KB, far beyond internal RAM).
    if (!m_sprite) {
        m_sprite = new LGFX_Sprite(&M5.Lcd);
        m_sprite->setColorDepth(16);
        m_sprite->setPsram(false);
        void* result = m_sprite->createSprite(BOX_SIZE, BOX_SIZE);
        if (!result) {
            ESP_LOGW(TAG, "Play-area sprite alloc in internal RAM failed (%dx%d), trying PSRAM",
                     BOX_SIZE, BOX_SIZE);
            m_sprite->setPsram(true);
            result = m_sprite->createSprite(BOX_SIZE, BOX_SIZE);
        }
        if (!result) {
            // Not fatal: renderFrame() can draw straight to the panel, and
            // update() then erases the previous frame first. It flickers.
            ESP_LOGE(TAG, "Failed to create play-area sprite, falling back to direct draw");
            delete m_sprite;
            m_sprite = nullptr;
        } else {
            ESP_LOGI(TAG, "Play-area sprite created: %dx%d", BOX_SIZE, BOX_SIZE);
        }
    }

    ui_clear_content_area();
    draw();
}

void ScreenTombola::leave()
{
    m_isActive = false;
    // The sequencer keeps running on purpose: ui_tombola_tick() is driven from
    // ui_update(), so a patch does not fall silent while the user looks at the
    // Log or MIDI Info screen. The sprite is kept too -- re-entering this
    // screen is common and the allocation is the expensive part.
}

void ScreenTombola::onNavCenter()
{
    if (ui_tombola_running()) {
        ui_tombola_stop();
    } else {
        ui_tombola_start();
    }
    ui_request_redraw();  // Repaints the nav bar with the new label
}

void ScreenTombola::onTouch(int touchId, int x, int y, bool pressed)
{
    (void)touchId;
    if (!pressed || !m_isActive) return;
    if (!g_model.touchAdd) return;

    int cx, cy;
    float radius;
    geometry(cx, cy, radius);
    if (radius <= 0.0f) return;

    float nx = (float)(x - cx) / radius;
    float ny = (float)(y - cy) / radius;
    if (sqrtf(nx * nx + ny * ny) > 0.7f) return;  // Outside: leave it alone

    modelLock();
    spawnBall(nx, ny, -1, -1, UI_COLOR_WHITE, 1.0f);
    modelUnlock();
}

void ScreenTombola::geometry(int& cx, int& cy, float& radius)
{
    cx = BOX_X + BOX_SIZE / 2;
    cy = BOX_Y + BOX_SIZE / 2;
    // Two pixels of slack so the outermost vertex still lands inside the
    // sprite at radius == 1.0.
    radius = (float)(BOX_SIZE / 2 - 2) * clampFloat(g_model.radiusRatio, 0.2f, 1.0f);
}

void ScreenTombola::polygonPoint(int cx, int cy, float radius, float angle, int i,
                                 int sides, int& px, int& py)
{
    float a = angle + (float)i * 2.0f * (float)M_PI / (float)sides;
    px = cx + (int)(cosf(a) * radius);
    py = cy + (int)(sinf(a) * radius);
}

void ScreenTombola::drawPolygon(LovyanGFX* g, int cx, int cy, float radius, float angle,
                                int sides, uint16_t color)
{
    if (sides < UI_TOMBOLA_MIN_SIDES) return;

    // One cos/sin pair per vertex rather than one per line end. The polygon is
    // drawn twice a frame (erase the old angle, paint the new one), so this
    // used to be the busiest trigonometry in the screen.
    int px[UI_TOMBOLA_MAX_SIDES];
    int py[UI_TOMBOLA_MAX_SIDES];
    float step = 2.0f * (float)M_PI / (float)sides;
    for (int i = 0; i < sides; i++) {
        float a = angle + (float)i * step;
        px[i] = cx + (int)(cosf(a) * radius);
        py[i] = cy + (int)(sinf(a) * radius);
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        g->drawLine(px[i], py[i], px[j], py[j], color);
    }
}

void ScreenTombola::renderFrame(LovyanGFX* g, int ox, int oy)
{
    int cx, cy;
    float radius;
    geometry(cx, cy, radius);
    cx -= ox;
    cy -= oy;

    // Snapshot under the lock, then draw: the pixel pushing must not hold up
    // the PicoRuby task's setters.
    Ball balls[UI_TOMBOLA_MAX_BALLS];
    int sides, ballCount = 0;
    float angle, ballSize, rotationRpm, gravity, bounce;
    int flashSide;

    modelLock();
    memcpy(balls, g_model.balls, sizeof(balls));
    sides = clampInt(g_model.sides, UI_TOMBOLA_MIN_SIDES, UI_TOMBOLA_MAX_SIDES);
    angle = g_model.angle;
    ballSize = g_model.ballSize;
    rotationRpm = g_model.rotationRpm;
    gravity = g_model.gravity;
    bounce = g_model.bounce;
    flashSide = (esp_timer_get_time() < g_model.flashUntilUs) ? g_model.flashSide : -1;
    modelUnlock();

    drawPolygon(g, cx, cy, radius, angle, sides,
                ui_tombola_running() ? UI_COLOR_CYAN : UI_COLOR_DARKGRAY);

    // Highlight the side that was just hit
    if (flashSide >= 0 && flashSide < sides) {
        int x0, y0, x1, y1;
        polygonPoint(cx, cy, radius, angle, flashSide, sides, x0, y0);
        polygonPoint(cx, cy, radius, angle, flashSide + 1, sides, x1, y1);
        g->drawLine(x0, y0, x1, y1, UI_COLOR_WHITE);
    }

    int ballR = (int)(ballSize * radius);
    if (ballR < 2) ballR = 2;

    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        if (!balls[i].active) {
            m_drawnBalls[i].active = false;
            continue;
        }
        ballCount++;
        int px = cx + (int)(balls[i].x * radius);
        int py = cy + (int)(balls[i].y * radius);
        g->fillCircle(px, py, ballR, balls[i].color);

        m_drawnBalls[i].active = true;
        m_drawnBalls[i].x = px + ox;   // Remember in screen coordinates: the
        m_drawnBalls[i].y = py + oy;   // fallback erase path draws on the panel
        m_drawnBalls[i].r = ballR;
    }

    // Parameter readout. Every knob an encoder can own gets one; without it,
    // bounce in particular has no feedback until you hear it. Drawn inside the
    // play area so it is part of the same buffer and cannot flicker on its own.
    char hud[48];
    snprintf(hud, sizeof(hud), "N%d %.0frpm G%.2f B%.2f x%d",
             sides, (double)rotationRpm, (double)gravity, (double)bounce, ballCount);

    int hudX = BOX_X + 4 - ox;
    int hudY = BOX_Y + 4 - oy;
    g->fillRect(hudX, hudY, BOX_SIZE - 8, 8 * HUD_TEXT_SIZE + 2, UI_COLOR_BLACK);
    g->setTextSize(HUD_TEXT_SIZE);
    g->setTextColor(UI_COLOR_GRAY, UI_COLOR_BLACK);
    g->setCursor(hudX, hudY);
    g->print(hud);

    m_drawnSides = sides;
    m_drawnAngle = angle;
    m_drawnRadius = radius;
    m_hasDrawnFrame = true;
}

// Paints the previous frame's marks black. Used on both paths -- inside the
// sprite it replaces a full-buffer fillScreen, which on Tab5 is 768KB of
// writes that only a few hundred pixels actually needed.
void ScreenTombola::eraseFrame(LovyanGFX* g, int ox, int oy)
{
    if (!m_hasDrawnFrame) return;

    int cx, cy;
    float radius;
    geometry(cx, cy, radius);

    drawPolygon(g, cx - ox, cy - oy, m_drawnRadius, m_drawnAngle, m_drawnSides,
                UI_COLOR_BLACK);
    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        if (!m_drawnBalls[i].active) continue;
        g->fillCircle(m_drawnBalls[i].x - ox, m_drawnBalls[i].y - oy,
                      m_drawnBalls[i].r, UI_COLOR_BLACK);
    }
}

void ScreenTombola::paintFrame(bool fullRepaint)
{
    bool clear = fullRepaint || !m_hasDrawnFrame;

    if (m_sprite) {
        if (clear) {
            m_sprite->fillScreen(UI_COLOR_BLACK);
        } else {
            eraseFrame(m_sprite, BOX_X, BOX_Y);
        }
        renderFrame(m_sprite, BOX_X, BOX_Y);
        m_sprite->pushSprite(BOX_X, BOX_Y);
        return;
    }

    M5.Lcd.startWrite();
    if (clear) {
        M5.Lcd.fillRect(BOX_X, BOX_Y, BOX_SIZE, BOX_SIZE, UI_COLOR_BLACK);
    } else {
        eraseFrame(&M5.Lcd, 0, 0);
    }
    renderFrame(&M5.Lcd, 0, 0);
    M5.Lcd.endWrite();
}

// True when anything the frame shows differs from what was last drawn.
bool ScreenTombola::frameChanged()
{
    FrameKey key;

    modelLock();
    key.sides = clampInt(g_model.sides, UI_TOMBOLA_MIN_SIDES, UI_TOMBOLA_MAX_SIDES);
    key.angle = g_model.angle;
    key.radius = g_model.radiusRatio;
    key.ballSize = g_model.ballSize;
    key.rotationRpm = g_model.rotationRpm;
    key.gravity = g_model.gravity;
    key.bounce = g_model.bounce;
    key.flash = (esp_timer_get_time() < g_model.flashUntilUs);
    key.running = g_model.running;
    key.ballCount = 0;
    for (int i = 0; i < UI_TOMBOLA_MAX_BALLS; i++) {
        if (g_model.balls[i].active) key.ballCount++;
    }
    modelUnlock();

    bool same = key.sides == m_lastKey.sides &&
                key.ballCount == m_lastKey.ballCount &&
                key.angle == m_lastKey.angle &&
                key.radius == m_lastKey.radius &&
                key.ballSize == m_lastKey.ballSize &&
                key.rotationRpm == m_lastKey.rotationRpm &&
                key.gravity == m_lastKey.gravity &&
                key.bounce == m_lastKey.bounce &&
                key.flash == m_lastKey.flash &&
                key.running == m_lastKey.running;

    m_lastKey = key;
    return !same;
}

void ScreenTombola::update()
{
    if (!m_isActive) return;

    int64_t now = esp_timer_get_time();
    if (now - m_lastDrawUs < DRAW_INTERVAL_US) return;

    // While the sequencer is stopped the balls and the polygon hold still, so
    // repaint only when something visible actually moved. A parked Tombola
    // screen then costs nothing. (frameChanged() must run either way: it is
    // what keeps the comparison baseline current.)
    bool changed = frameChanged();
    if (!changed && !ui_tombola_running()) return;

    m_lastDrawUs = now;
    paintFrame(false);
}

void ScreenTombola::draw()
{
    // Full repaint requested by the UIManager: nothing on screen is ours yet.
    m_hasDrawnFrame = false;
    m_lastDrawUs = esp_timer_get_time();
    frameChanged();
    paintFrame(true);
}
