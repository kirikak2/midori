#include "sdkconfig.h"
#include "ui_strip.h"
#include "ui_fb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char* TAG = "UI_STRIP";

namespace ui_strip {

namespace {

// Fallback row counts, tried in order after LINES. Fewer rows only means more
// bands per frame; it is still far better than a full-size PSRAM sprite.
constexpr int FALLBACK_LINES[] = { 48, 32 };

// Cache-line aligned, so a band never shares a cache line with anything else.
constexpr size_t ALIGN = 64;

void* g_buf = nullptr;
size_t g_bytes = 0;
bool g_tried = false;
LGFX_Sprite* g_sprite = nullptr;

void* alloc_internal(int lines)
{
    size_t bytes = (size_t)UI_SCREEN_WIDTH * lines * 2;
    return heap_caps_aligned_alloc(ALIGN, bytes,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
}

}  // namespace

void init(void)
{
    if (g_tried) return;
    g_tried = true;

    ESP_LOGI(TAG, "Internal RAM: %u free, largest block %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    int lines = LINES;
    g_buf = alloc_internal(lines);
    for (size_t i = 0; !g_buf && i < sizeof(FALLBACK_LINES) / sizeof(FALLBACK_LINES[0]); i++) {
        ESP_LOGW(TAG, "%dx%d strip does not fit in internal RAM, trying %d rows",
                 UI_SCREEN_WIDTH, lines, FALLBACK_LINES[i]);
        lines = FALLBACK_LINES[i];
        g_buf = alloc_internal(lines);
    }
    bool internal = (g_buf != nullptr);
    if (!g_buf) {
        // Slow, but still flicker-free; the screens' direct-draw fallback is not.
        lines = LINES;
        ESP_LOGW(TAG, "No room in internal RAM, putting the %dx%d strip in PSRAM",
                 UI_SCREEN_WIDTH, lines);
        g_buf = heap_caps_aligned_alloc(ALIGN, (size_t)UI_SCREEN_WIDTH * lines * 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!g_buf) {
        ESP_LOGE(TAG, "Failed to allocate the strip, screens draw directly");
        return;
    }
    g_bytes = (size_t)UI_SCREEN_WIDTH * lines * 2;

    g_sprite = new LGFX_Sprite(&M5.Lcd);
    ESP_LOGI(TAG, "Strip ready: %dx%d (%u bytes) in %s", UI_SCREEN_WIDTH, lines,
             (unsigned)g_bytes, internal ? "internal RAM" : "PSRAM");
}

int rows_for(int w)
{
    if (!g_buf || w <= 0 || w > UI_SCREEN_WIDTH) return 0;
    return (int)(g_bytes / ((size_t)w * 2));
}

LGFX_Sprite* shape(int w, int h)
{
    // setBuffer() wraps the memory without taking ownership (deleteSprite()
    // never frees it) and resets the clip rectangle to the new size.
    g_sprite->setBuffer(g_buf, w, h, 16);

    // Text state lives in the sprite object, not the buffer, so it would leak
    // from one screen to the next (Main turns wrapping off, for instance).
    // Start every band from what a freshly created sprite has.
    g_sprite->setFont(&fonts::Font0);
    g_sprite->setTextSize(1);
    g_sprite->setTextDatum(textdatum_t::top_left);
    g_sprite->setTextWrap(true, false);
    g_sprite->setTextPadding(0);
    g_sprite->setCursor(0, 0);
    return g_sprite;
}

void push(LGFX_Sprite* sprite, int x, int y)
{
    // Tab5 / CrowPanel copy straight into the DSI framebuffer; elsewhere
    // pushSprite() does it. Both are synchronous, so the strip is free again
    // when this returns.
    if (!ui_fb::blit(sprite, x, y)) {
        sprite->pushSprite(x, y);
    }
}

}  // namespace ui_strip
