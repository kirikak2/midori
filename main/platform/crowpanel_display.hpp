#ifndef CROWPANEL_DISPLAY_HPP
#define CROWPANEL_DISPLAY_HPP

#include "sdkconfig.h"

#if defined(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)

#include "esp_err.h"
#include <M5GFX.h>

namespace lgfx { inline namespace v1 { struct Panel_DSI; } }

namespace crowpanel {

// Brings up the 1024x600 MIPI-DSI panel, its GT911 touch controller and the
// backlight, and hands the result to M5.Lcd.
//
// M5GFX cannot autodetect this board -- it probes for M5Stack hardware and
// finds none -- so the panel is built here and installed with
// M5GFX::init(Panel_Device*), which skips autodetection entirely. Everything
// downstream (the whole of main/ui, lcd_console) then talks to M5.Lcd exactly
// as it does on a CoreS3 or a Tab5.
//
// M5.begin() is deliberately NOT called: it would go looking for a PMIC, an
// IMU and IO expanders that are not on this board. Touch is wired up by hand
// instead (M5.Touch.begin), which is all M5Unified contributes here.
esp_err_t display_init(void);

// The installed panel, or nullptr before display_init() succeeds. Used by
// ui_ppa to find the framebuffer the MIPI-DSI controller scans out.
lgfx::Panel_DSI* dsi_panel(void);

// Feeds M5.Touch from the panel's touch controller. Call once per UI tick;
// this is the part of M5.update() that this board actually needs.
void touch_update(void);

}  // namespace crowpanel

#endif // CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL

#endif // CROWPANEL_DISPLAY_HPP
