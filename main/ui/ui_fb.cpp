#include "sdkconfig.h"
#include "ui_fb.h"
#include "esp_log.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp_cache.h"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"
#if defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
#include "crowpanel_display.hpp"
#endif
#endif

static const char* TAG = "UI_FB";

namespace ui_fb {

#if defined(CONFIG_IDF_TARGET_ESP32P4)

namespace {

uint16_t* g_fb = nullptr;
int g_panelW = 0;       // PANEL coordinates, not the rotated view
int g_panelH = 0;
bool g_tried = false;    // Setup runs once; a failure is not retried per frame
bool g_ready = false;

// Columns copied per pass. Under rotation 1/3 each sprite column becomes a
// framebuffer line, so a tile keeps this many lines' worth of cache in play
// while the rows of the tile stream through. 32 pixels is one 64-byte line of
// the source row.
constexpr int TILE = 32;

bool is_rgb565(lgfx::color_depth_t depth)
{
    return depth == lgfx::color_depth_t::rgb565_2Byte ||
           depth == lgfx::color_depth_t::rgb565_nonswapped;
}

// The framebuffer address is not exposed as such, but Panel_DSI keeps it in
// its public config_detail() -- it is the buffer it handed to
// esp_lcd_dpi_panel_get_frame_buffer() at init.
bool setup()
{
    g_tried = true;

    // Only a Panel_DSI has the framebuffer this reaches into, and there is no
    // way to ask an IPanel what it is -- so each board says so for itself.
#if defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
    auto panel = crowpanel::dsi_panel();
#else
    if (M5.Lcd.getBoard() != lgfx::board_t::board_M5Tab5) {
        ESP_LOGI(TAG, "Not a DSI panel, using pushSprite");
        return false;
    }
    auto panel = static_cast<lgfx::Panel_DSI*>(M5.Lcd.getPanel());
#endif
    if (!panel) {
        ESP_LOGW(TAG, "No panel");
        return false;
    }

    // Panel coordinates, which are NOT the coordinates callers draw in: Tab5's
    // framebuffer is 720x1280 portrait and the 1280x720 landscape view comes
    // from LovyanGFX's rotation. Taking the geometry from M5.Lcd.width() would
    // address the buffer with a 1280-pixel stride that does not exist.
    const auto& pcfg = panel->config();
    g_panelW = pcfg.panel_width;
    g_panelH = pcfg.panel_height;
    g_fb = static_cast<uint16_t*>(panel->config_detail().buffer);
    if (!g_fb || g_panelW <= 0 || g_panelH <= 0) {
        ESP_LOGW(TAG, "Framebuffer not reachable");
        return false;
    }

    // The strides in blit() reproduce LovyanGFX's rotation, which folds
    // offset_rotation into _internal_rotation. With a non-zero offset_rotation
    // getRotation() would no longer describe the mapping.
    if (pcfg.offset_rotation != 0) {
        ESP_LOGI(TAG, "offset_rotation %d is not handled, using pushSprite",
                 (int)pcfg.offset_rotation);
        return false;
    }

    ESP_LOGI(TAG, "Framebuffer blit ready: fb %p panel %dx%d", g_fb, g_panelW, g_panelH);
    g_ready = true;
    return true;
}

}  // namespace

bool blit(LGFX_Sprite* sprite, int x, int y)
{
    if (!g_tried) setup();
    if (!g_ready || !sprite) return false;

    const uint16_t* src = static_cast<const uint16_t*>(sprite->getBuffer());
    int w = sprite->width();
    int h = sprite->height();
    if (!src || w <= 0 || h <= 0) return false;

    // Both sides must be 16bpp RGB565. The sprite is rgb565_2Byte (byte
    // swapped) and Tab5's panel is rgb565_nonswapped, so the bytes of every
    // pixel are swapped on the way.
    auto srcDepth = sprite->getColorDepth();
    auto dstDepth = M5.Lcd.getColorDepth();
    if (!is_rgb565(srcDepth) || !is_rgb565(dstDepth)) return false;
    bool swap = (srcDepth != dstDepth);

    int r = (int)M5.Lcd.getRotation();
    if (r > 3) return false;   // Mirrored rotations (4-7) are not handled
    int wl = (r & 1) ? g_panelH : g_panelW;   // logical (rotated) dimensions
    int hl = (r & 1) ? g_panelW : g_panelH;
    if (x < 0 || y < 0 || x + w > wl || y + h > hl) return false;

    // LovyanGFX maps a single pixel like this (Panel_FrameBufferBase):
    //     if ((1u << r) & 0b10010110) y = hl - (y + 1);
    //     if (r & 2)                  x = wl - (x + 1);
    //     if (r & 1)                  swap(x, y);
    // and stores it at fb[y * panelW + x]. Every step is affine, so the
    // buffer index of screen pixel (X, Y) is base + X * dX + Y * dY.
    int32_t ax = 1, bx = 0, ay = 1, by = 0;      // X' = ax*X + bx, Y' = ay*Y + by
    if ((1u << r) & 0b10010110) { ay = -1; by = hl - 1; }
    if (r & 2)                  { ax = -1; bx = wl - 1; }
    int32_t dX, dY, base;
    if (r & 1) {    // panel x = Y', panel y = X'
        dX = ax * g_panelW;
        dY = ay;
        base = bx * g_panelW + by;
    } else {        // panel x = X', panel y = Y'
        dX = ax;
        dY = ay * g_panelW;
        base = by * g_panelW + bx;
    }

    // Copy in tiles of TILE columns, rows inner. Under rotation 3 (Tab5) a
    // sprite row is a column of the framebuffer, so walking a full row would
    // touch w different framebuffer lines; a tile touches TILE of them and
    // fills each one sequentially as the rows go by.
    for (int tx = 0; tx < w; tx += TILE) {
        int tw = (w - tx < TILE) ? (w - tx) : TILE;
        for (int sy = 0; sy < h; sy++) {
            const uint16_t* s = src + sy * w + tx;
            int32_t d = base + (x + tx) * dX + (y + sy) * dY;
            if (swap) {
                for (int i = 0; i < tw; i++, d += dX) g_fb[d] = __builtin_bswap16(s[i]);
            } else {
                for (int i = 0; i < tw; i++, d += dX) g_fb[d] = s[i];
            }
        }
    }

    // The DSI controller scans PSRAM, not the cache: write back the lines just
    // filled. The block's corners bound the touched index range (for rotation
    // 3 that is whole framebuffer lines, w of them). Only a writeback --
    // nothing is invalidated, so whatever LovyanGFX has drawn elsewhere and
    // not flushed yet stays intact for its own display() call. dX and dY can
    // have opposite signs (rotation 3), so the extremes are not necessarily on
    // the main diagonal: take each axis' contribution separately.
    int32_t ex = (w - 1) * dX;
    int32_t ey = (h - 1) * dY;
    int32_t lo = base + x * dX + y * dY + (ex < 0 ? ex : 0) + (ey < 0 ? ey : 0);
    int32_t hi = base + x * dX + y * dY + (ex > 0 ? ex : 0) + (ey > 0 ? ey : 0);
    esp_cache_msync(g_fb + lo, (size_t)(hi - lo + 1) * 2,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    return true;
}

#else  // Not an ESP32-P4: no framebuffer panel, always fall back

bool blit(LGFX_Sprite* sprite, int x, int y)
{
    (void)sprite; (void)x; (void)y;
    return false;
}

#endif

}  // namespace ui_fb
