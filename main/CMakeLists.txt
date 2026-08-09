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
)

# Platform-specific sources
if(CONFIG_USB_MIDI_BOARD_FREENOVE)
    list(APPEND SRCS "platform/platform_freenove.c")
elseif(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3 OR CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
    list(APPEND SRCS
        "platform/platform_m5stack.cpp"
        "lcd_console/lcd_console.cpp"
        "ui/ui_common.cpp"
        "ui/ui_manager.cpp"
        "ui/ui_ppa.cpp"
        "ui/screen_log.cpp"
        "ui/screen_main.cpp"
        "ui/screen_pad.cpp"
        "ui/screen_midi_info.cpp"
        "ui/screen_script.cpp"
        "ui/screen_settings.cpp"
        "ui/screen_tombola.cpp"
        "ui/screen_knob.cpp"
    )
endif()

# SD card access is handled by PicoRuby's FAT filesystem
# We cannot use ESP-IDF's fatfs because it conflicts with PicoRuby's FatFs library
# Instead, PicoRuby notifies C side about available scripts via API

# USB Host bring-up now lives inside picoruby-usb_midi_host (Phase 5a-2),
# so main only needs the legacy 'driver' component for whatever else it
# might pull in.
set(PRIV_REQUIRES_LIST driver esp_tinyusb)

# ui/ui_ppa.cpp drives the ESP32-P4's pixel accelerator (Tab5 only).
# Keyed off IDF_TARGET, not CONFIG_IDF_TARGET_ESP32P4: requirements are
# collected in an early expansion pass where the CONFIG_ variables do not
# exist yet, so a CONFIG_ conditional here would silently never fire.
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND PRIV_REQUIRES_LIST esp_driver_ppa)
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES picoruby-esp32
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
