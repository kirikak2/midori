# Base sources (always included)
set(SRCS "usb_midi_host.c")

# Include directories
set(INCLUDE_DIRS
    "."
    "platform"
    "lcd_console"
    "sd_card"
    "ui"
    "../mrbgems/picoruby-usb_midi_host/include"
    "../mrbgems/picoruby-usb_midi_device/include"
    "../mrbgems/picoruby-amy/include"
)

# Codec / amplifier power-up for the AMY synth (picoruby-amy); empty on
# boards without it.
list(APPEND SRCS "platform/board_audio.cpp")

# Platform-specific sources
if(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3 OR CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
    list(APPEND SRCS "platform/platform_m5stack.cpp")
elseif(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
    list(APPEND SRCS
        "platform/platform_crowpanel.cpp"
        "platform/crowpanel_display.cpp"
    )
else()
    list(APPEND SRCS "platform/platform_freenove.c")
endif()

# The on-device UI and the LCD boot console, on every board that has a display
# (CONFIG_USB_MIDI_UI_ENABLED is derived from the board choice in Kconfig).
if(CONFIG_USB_MIDI_UI_ENABLED)
    list(APPEND SRCS
        "lcd_console/lcd_console.cpp"
        "ui/ui_common.cpp"
        "ui/ui_manager.cpp"
        "ui/ui_fb.cpp"
        "ui/ui_strip.cpp"
        "ui/screen_log.cpp"
        "ui/screen_main.cpp"
        "ui/screen_pad.cpp"
        "ui/screen_midi_info.cpp"
        "ui/screen_script.cpp"
        "ui/screen_settings.cpp"
        "ui/screen_tombola.cpp"
        "ui/screen_knob.cpp"
        "ui/screen_xypad.cpp"
    )
endif()

# SD card access is handled by PicoRuby's FAT filesystem
# We cannot use ESP-IDF's fatfs because it conflicts with PicoRuby's FatFs library
# Instead, PicoRuby notifies C side about available scripts via API

# USB Host bring-up now lives inside picoruby-usb_midi_host (Phase 5a-2),
# so main only needs the legacy 'driver' component for whatever else it
# might pull in.
set(PRIV_REQUIRES_LIST driver esp_tinyusb)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES midori-runtime
    PRIV_REQUIRES ${PRIV_REQUIRES_LIST}
)

# Let GCC emit the FPU's sqrt instruction instead of calling libm's sqrtf,
# which it otherwise cannot do because the default -fmath-errno requires
# sqrtf to set errno for negative inputs. Nothing here reads errno after a
# math call. Measured on ui/screen_tombola.cpp: 4 sqrtf calls become fsqrt.s,
# and the object shrinks slightly.
#
# Deliberately NOT -ffast-math. Measuring the same file showed it buys nothing
# beyond this flag -- the remaining library calls are cosf/sinf, which it does
# not inline either -- while it also implies -ffinite-math-only and, at link
# time, can switch the FPU to flush-to-zero for the whole program.
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-math-errno)
