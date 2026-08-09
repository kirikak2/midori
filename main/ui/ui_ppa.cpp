#include "sdkconfig.h"
#include "ui_ppa.h"
#include "esp_log.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "driver/ppa.h"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"
#endif

static const char* TAG = "UI_PPA";

namespace ui_ppa {

#if defined(CONFIG_IDF_TARGET_ESP32P4)

namespace {

ppa_client_handle_t g_client = nullptr;
void* g_fb = nullptr;
int g_panelW = 0;       // PANEL coordinates, not the rotated view
int g_panelH = 0;
size_t g_fbSize = 0;
bool g_tried = false;    // Setup runs once; a failure is not retried per frame
bool g_ready = false;
bool g_warnedBusy = false;

// Transaction pool depth. One is not enough: UIManager::update() can call a
// screen's update() and then its draw() in the same pass, so two blits land
// microseconds apart, and the driver only returns a transaction to the pool
// *after* it releases the semaphore the caller is blocked on (see
// ppa_transaction_done_cb) -- with a pool of one the second blit finds it
// empty and fails. Borrowing uses a zero timeout, so it never waits.
constexpr uint32_t PENDING_TRANS = 4;

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

    if (M5.Lcd.getBoard() != lgfx::board_t::board_M5Tab5) {
        ESP_LOGI(TAG, "Not a Tab5 panel, using pushSprite");
        return false;
    }

    auto panel = static_cast<lgfx::Panel_DSI*>(M5.Lcd.getPanel());
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
    g_fb = panel->config_detail().buffer;
    if (!g_fb || g_panelW <= 0 || g_panelH <= 0) {
        ESP_LOGW(TAG, "Framebuffer not reachable");
        return false;
    }
    g_fbSize = (size_t)g_panelW * (size_t)g_panelH * 2;   // 16bpp, no line padding

    // block_offset() below reproduces LovyanGFX's rotation, which folds
    // offset_rotation into _internal_rotation. With a non-zero offset_rotation
    // getRotation() would no longer describe the mapping.
    if (pcfg.offset_rotation != 0) {
        ESP_LOGI(TAG, "offset_rotation %d is not handled, using pushSprite",
                 (int)pcfg.offset_rotation);
        return false;
    }

    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = PENDING_TRANS;
    esp_err_t err = ppa_register_client(&cfg, &g_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ppa_register_client failed: %s", esp_err_to_name(err));
        g_client = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "PPA blit ready: fb %p panel %dx%d (%u bytes)",
             g_fb, g_panelW, g_panelH, (unsigned)g_fbSize);
    g_ready = true;
    return true;
}

// Maps a block in the caller's (rotated) coordinates onto the panel buffer.
//
// LovyanGFX maps a single pixel like this (Panel_FrameBufferBase, with wl/hl
// the logical dimensions):
//     if ((1u << r) & 0b10010110) y = hl - (y + 1);
//     if (r & 2)                  x = wl - (x + 1);
//     if (r & 1)                  swap(x, y);
// Applying that to the corners of a block gives the origins below, and the
// leftover per-pixel term is exactly a counter-clockwise rotation -- which is
// what PPA's rotation_angle does. Checked pixel-by-pixel against the formula
// above for r = 0..3 before this was written.
bool block_offset(int r, int wl, int hl, int x, int y, int w, int h,
                  int* ox, int* oy, ppa_srm_rotation_angle_t* angle,
                  int* outW, int* outH)
{
    switch (r) {
        case 0:
            *ox = x;          *oy = y;
            *angle = PPA_SRM_ROTATION_ANGLE_0;   *outW = w; *outH = h;
            return true;
        case 1:
            *ox = hl - y - h; *oy = x;
            *angle = PPA_SRM_ROTATION_ANGLE_270; *outW = h; *outH = w;
            return true;
        case 2:
            *ox = wl - x - w; *oy = hl - y - h;
            *angle = PPA_SRM_ROTATION_ANGLE_180; *outW = w; *outH = h;
            return true;
        case 3:
            *ox = y;          *oy = wl - x - w;
            *angle = PPA_SRM_ROTATION_ANGLE_90;  *outW = h; *outH = w;
            return true;
        default:
            return false;   // Mirrored rotations (4-7) are not handled
    }
}

}  // namespace

bool available(void)
{
    return g_ready;
}

bool blit(LGFX_Sprite* sprite, int x, int y)
{
    if (!g_tried) setup();
    if (!g_ready || !sprite) return false;

    void* src = sprite->getBuffer();
    int w = sprite->width();
    int h = sprite->height();
    if (!src || w <= 0 || h <= 0) return false;

    // Both sides must be 16bpp RGB565; the accelerator copies raw pixels and
    // the two byte orders are told apart below.
    auto srcDepth = sprite->getColorDepth();
    auto dstDepth = M5.Lcd.getColorDepth();
    if (!is_rgb565(srcDepth) || !is_rgb565(dstDepth)) return false;

    int r = (int)M5.Lcd.getRotation();
    int wl = (r & 1) ? g_panelH : g_panelW;   // logical (rotated) dimensions
    int hl = (r & 1) ? g_panelW : g_panelH;
    if (x < 0 || y < 0 || x + w > wl || y + h > hl) return false;

    int ox, oy, outW, outH;
    ppa_srm_rotation_angle_t angle;
    if (!block_offset(r, wl, hl, x, y, w, h, &ox, &oy, &angle, &outW, &outH)) {
        return false;
    }
    if (ox < 0 || oy < 0 || ox + outW > g_panelW || oy + outH > g_panelH) {
        return false;
    }

    // Scale 1:1 with the rotation the panel needs. Source and destination are
    // both RGB565; byte_swap covers the case where the sprite is the swapped
    // variant and the panel is not (which is exactly the case on Tab5, where
    // the panel is rgb565_nonswapped).
    ppa_srm_oper_config_t op = {};
    op.in.buffer = src;
    op.in.pic_w = w;
    op.in.pic_h = h;
    op.in.block_w = w;
    op.in.block_h = h;
    op.in.block_offset_x = 0;
    op.in.block_offset_y = 0;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.out.buffer = g_fb;
    op.out.buffer_size = g_fbSize;
    op.out.pic_w = g_panelW;
    op.out.pic_h = g_panelH;
    op.out.block_offset_x = ox;
    op.out.block_offset_y = oy;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    // Flush the destination scanlines before handing them to the accelerator.
    //
    // The driver invalidates the output window after the DMA, and that window
    // is every panel scanline the block touches, at full panel width -- not
    // just the block's own columns (ppa_srm.c: "note that the window content is
    // not continuous in the buffer"). For the tombola's play area that is
    // 720x620x2 = 872KB, 48% of the framebuffer, and in landscape coordinates
    // it is a 620px-wide band running the full height of the screen: the status
    // bar and the nav bar pass straight through it.
    //
    // Invalidating (M2C) discards dirty cache lines rather than writing them
    // back, and the DPI framebuffer is cached write-back PSRAM that LovyanGFX
    // draws into with the CPU. So anything drawn there and not yet written back
    // was being thrown away, leaving the panel showing whatever PSRAM held
    // before it.
    //
    // What makes this bite is that LovyanGFX defers its own writeback:
    // Panel_FrameBufferBase accumulates a modified range and flushes it in
    // display(), which UIManager::update() reaches only at the end of
    //     startWrite(); drawStatusBar(); screen->draw(); drawNavBar(); endWrite();
    // The blit happens in the middle of that, so the status bar it just drew is
    // still sitting dirty in cache when the accelerator's invalidate throws it
    // away.
    //
    // display() flushes exactly the range that was drawn, which leaves nothing
    // dirty for the invalidate to lose. Flushing the whole band instead would
    // also work but would walk 872KB of cache on every frame.
    M5.Lcd.display();

    op.rotation_angle = angle;
    op.scale_x = 1.0f;
    op.scale_y = 1.0f;
    op.mirror_x = false;
    op.mirror_y = false;
    op.rgb_swap = false;
    op.byte_swap = (srcDepth != dstDepth);
    op.mode = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(g_client, &op);
    if (err == ESP_OK) return true;

    if (err == ESP_ERR_INVALID_ARG) {
        // Geometry or alignment the accelerator will never accept. Retrying
        // every frame would only spend cycles and fill the log.
        ESP_LOGW(TAG, "PPA rejected the blit (%s), using pushSprite from now on",
                 esp_err_to_name(err));
        g_ready = false;
        return false;
    }

    // Anything else is transient -- an empty transaction pool, most likely.
    // Fall back for this frame only and try again on the next one.
    if (!g_warnedBusy) {
        g_warnedBusy = true;
        ESP_LOGI(TAG, "PPA busy (%s), falling back for this frame",
                 esp_err_to_name(err));
    }
    return false;
}

#else  // Not an ESP32-P4: no accelerator, always fall back

bool available(void)
{
    return false;
}

bool blit(LGFX_Sprite* sprite, int x, int y)
{
    (void)sprite; (void)x; (void)y;
    return false;
}

#endif

}  // namespace ui_ppa
