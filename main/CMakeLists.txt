# Base sources (always included)
set(SRCS "usb_midi_host.c")

# Include directories
set(INCLUDE_DIRS
    "."
    "platform"
    "lcd_console"
    "sd_card"
    "ui"
    "../components/picoruby-esp32/picoruby/mrbgems/picoruby-usb_midi/include"
)

# Platform-specific sources
if(CONFIG_USB_MIDI_BOARD_FREENOVE)
    list(APPEND SRCS "platform/platform_freenove.c")
elseif(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3)
    list(APPEND SRCS
        "platform/platform_m5stack.cpp"
        "lcd_console/lcd_console.cpp"
        "ui/ui_common.cpp"
        "ui/ui_manager.cpp"
        "ui/screen_log.cpp"
        "ui/screen_main.cpp"
        "ui/screen_pad.cpp"
        "ui/screen_midi_info.cpp"
        "ui/screen_script.cpp"
        "ui/screen_settings.cpp"
    )
endif()

# SD card access is handled by PicoRuby's FAT filesystem
# We cannot use ESP-IDF's fatfs because it conflicts with PicoRuby's FatFs library
# Instead, PicoRuby notifies C side about available scripts via API

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES picoruby-esp32
    PRIV_REQUIRES usb driver
)
