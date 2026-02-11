# Base sources (always included)
set(SRCS "usb_midi_host.c")

# Include directories
set(INCLUDE_DIRS "." "platform" "lcd_console")

# Platform-specific sources
if(CONFIG_USB_MIDI_BOARD_FREENOVE)
    list(APPEND SRCS "platform/platform_freenove.c")
elseif(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3)
    list(APPEND SRCS
        "platform/platform_m5stack.cpp"
        "lcd_console/lcd_console.cpp"
    )
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
)
