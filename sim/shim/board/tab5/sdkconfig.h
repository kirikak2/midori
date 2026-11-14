// sdkconfig.h for the simulator's Tab5 build: the CONFIG_* values main/ui and
// the gems read, fixed as the device's Tab5 / midi_device build has them.
// CONFIG_IDF_TARGET_ESP32P4 stays undefined: it selects the DSI panel and
// the cache write-back, which the simulator's framebuffer panel replaces.
#pragma once

#define CONFIG_USB_MIDI_BOARD_M5STACK_TAB5 1
#define CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE 1
#define CONFIG_USB_MIDI_UI_ENABLED 1
#define CONFIG_USB_MIDI_HOST_ENABLED 1
#define CONFIG_USB_MIDI_CONSOLE_ON_CDC 1
#define CONFIG_USB_MIDI_SDCARD_ENABLED 1
#define CONFIG_USB_MIDI_SDCARD_MOUNT_POINT "/sd"
#define CONFIG_FREERTOS_HZ 100
#define CONFIG_LOG_MAXIMUM_LEVEL 3
