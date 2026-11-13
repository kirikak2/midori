// sdkconfig.h for the simulator's CoreS3 build: the CONFIG_* values main/ui and
// the gems read, as the device's CoreS3 build in host mode (the default:
// USB-C is the USB-MIDI host, the console is on UART) has them.
#pragma once

#define CONFIG_USB_MIDI_BOARD_M5STACK_CORES3 1
#define CONFIG_USB_MIDI_USB_MODE_HOST 1
#define CONFIG_USB_MIDI_UI_ENABLED 1
#define CONFIG_USB_MIDI_HOST_ENABLED 1
#define CONFIG_USB_MIDI_SDCARD_ENABLED 1
#define CONFIG_USB_MIDI_SDCARD_MOUNT_POINT "/sd"
#define CONFIG_FREERTOS_HZ 100
#define CONFIG_LOG_MAXIMUM_LEVEL 3
