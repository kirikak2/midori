# PicoRuby is built on the mruby VM (picoruby-mruby). The mruby/c build was
# dropped on 2026-10-11; the midori gems keep their mruby/c bindings for other
# hosts, but nothing here compiles them.
file(MAKE_DIRECTORY ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/lib/mbedtls/include)
file(MAKE_DIRECTORY ${COMPONENT_DIR}/picoruby/build/esp32-picoruby/include)
file(MAKE_DIRECTORY ${COMPONENT_DIR}/picoruby/build/prism/include)
set(MRUBY_CONFIG_STRING "${COMPONENT_DIR}/build_config/${IDF_TARGET_ARCH}-esp-picoruby.rb")
set(LIBMRUBY_PATH "esp32-picoruby")
set(CUSTOM_TARGET_NAME "picoruby")
set(MRUBY_GEM_DIR ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mruby)
set(ADDITIONAL_INCLUDE_DIRS
  ${COMPONENT_DIR}/picoruby/build/esp32-picoruby/include
  ${MRUBY_GEM_DIR}/include
  ${MRUBY_GEM_DIR}/lib/mruby/include
  ${MRUBY_GEM_DIR}/lib/mruby/mrbgems/mruby-task/include
)
# These must match what libmruby.a was compiled with (build_config/
# *-esp-picoruby.rb plus what picoruby-mruby's mrbgem.rake adds): several
# of them change the layout of mrb_state and mrb_value, and a mismatch
# corrupts memory silently instead of failing to link.
set(ADDITIONAL_DEFINITIONS
  -DPICORB_VM_MRUBY
  -DMRB_TICK_UNIT=10
  -DMRB_TIMESLICE_TICK_COUNT=1
  -DMRB_INT64
  -DMRB_NO_BOXING
  -DMRB_32BIT
  -DMRB_UTF8_STRING
  -DMRB_USE_TASK_SCHEDULER
  -DMRB_CONSTRAINED_BASELINE_PROFILE=1
  -DPICORB_ALLOC_ESTALLOC
  -DPICORB_ALLOC_ALIGN=8
)

# The AMY engine is an IDF component of its own (components/amy), built for
# the ESP32-P4 boards only. Keyed off IDF_TARGET, not a CONFIG_ symbol:
# requirements are collected in an early expansion pass where CONFIG_
# variables do not exist yet.
set(AMY_REQUIRES "")
if(IDF_TARGET STREQUAL "esp32p4")
  set(AMY_REQUIRES amy)
endif()

idf_component_register(
  SRCS
    ${COMPONENT_DIR}/picoruby-esp32.c
    ${COMPONENT_DIR}/picoruby_supervisor.c
    ${COMPONENT_DIR}/console_input.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-require/ports/esp32/platform.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-esp32/ports/esp32.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/esp32/machine.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/ports/common/machine.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-io-console/ports/esp32/io-console.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-littlefs/ports/esp32/flash_hal.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-env/ports/esp32/env.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-gpio/ports/esp32/gpio.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-adc/ports/esp32/adc.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-spi/ports/esp32/spi.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rng/ports/esp32/rng.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-pwm/ports/esp32/pwm.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-rmt/ports/esp32/rmt.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-watchdog/ports/esp32/watchdog.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/esp32/timing_alt.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/cipher.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/cmac.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/digest.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/hmac.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/pkey.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/ports/common/md.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/net_helpers.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/ssl_socket.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/tcp_server.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/tcp_socket.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-socket/ports/esp32/udp_socket.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-i2c/ports/esp32/i2c.c
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-uart/ports/esp32/uart.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_host/ports/esp32/usb_midi_host.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_host/ports/esp32/usb_host_driver.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_device/ports/esp32/usb_midi_device.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_device/ports/esp32/usb_descriptors.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-uart_midi/ports/esp32/uart_midi.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-midi/ports/esp32/midi.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-ui/ports/esp32/ui.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-amy/ports/esp32/amy_port.c
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-filesystem-fat/ports/esp32/sd_disk.c
  INCLUDE_DIRS
    ${COMPONENT_DIR}
    ${COMPONENT_DIR}/picoruby/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-machine/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-mbedtls/lib/mbedtls/include
    ${COMPONENT_DIR}/picoruby/mrbgems/picoruby-littlefs/ports/esp32
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/lib/prism/include
    ${COMPONENT_DIR}/picoruby/build/prism/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_host/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-usb_midi_device/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-uart_midi/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-midi/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-ui/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-amy/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-filesystem-fat/ports/esp32
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-filesystem-fat/include
    ${CMAKE_SOURCE_DIR}/mrbgems/picoruby-filesystem-fat/src/hal
    ${CMAKE_SOURCE_DIR}/main/ui
    ${ADDITIONAL_INCLUDE_DIRS}
  PRIV_REQUIRES
    esp_driver_gpio
    esp_driver_uart
    esp_driver_usb_serial_jtag
    esp_driver_spi
    esp_driver_sdmmc
    esp_driver_ledc
    esp_driver_rmt
    esp_driver_i2c
    esp_adc
    esp_timer
    esp_hw_support
    spi_flash
    nvs_flash
    esp_wifi
    usb
    esp_tinyusb
    esp_driver_i2s
    ${AMY_REQUIRES}
)

add_definitions(
  ${ADDITIONAL_DEFINITIONS}
  -DUSE_FAT_SD_DISK
  -DPICORB_PLATFORM_ESP32
  -DNDEBUG
)

set(PICORUBY_DIR ${COMPONENT_DIR}/picoruby)
set(LIBMRUBY_FILE ${PICORUBY_DIR}/build/${LIBMRUBY_PATH}/lib/libmruby.a)

# Build PicoRuby
add_custom_command(
  OUTPUT ${LIBMRUBY_FILE}
  COMMAND ${CMAKE_COMMAND} -E echo "MRUBY_CONFIG=${MRUBY_CONFIG_STRING} CONFIG_IDF_TARGET=${CONFIG_IDF_TARGET} rake"
  COMMAND ${CMAKE_COMMAND} -E env MRUBY_CONFIG=${MRUBY_CONFIG_STRING} CONFIG_IDF_TARGET=${CONFIG_IDF_TARGET} rake
  WORKING_DIRECTORY ${PICORUBY_DIR}
  COMMENT "PicoRuby build"
  VERBATIM
)

add_prebuilt_library(
  libmruby ${LIBMRUBY_FILE}
  REQUIRES ${COMPONENT_NAME}
)
target_link_libraries(${COMPONENT_LIB} PRIVATE libmruby)
target_include_directories(
  ${COMPONENT_LIB}
  PRIVATE
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/include
    ${COMPONENT_DIR}/picoruby/mrbgems/mruby-compiler/lib/prism/include
    ${COMPONENT_DIR}/picoruby/build/prism/include
    ${ADDITIONAL_INCLUDE_DIRS}
)

add_custom_target(
  ${CUSTOM_TARGET_NAME} DEPENDS ${LIBMRUBY_FILE}
)
add_dependencies(${COMPONENT_LIB} ${CUSTOM_TARGET_NAME})

# Generate board_config.rb from template based on Kconfig settings
#
# USB roles are derived from the "USB port mode" choice (see
# main/Kconfig.projbuild):
#   host          -> USB-OTG host, no USB device role
#   serial        -> USB-Serial/JTAG console (device)
#   midi_device   -> TinyUSB CDC + MIDI composite device
# On the single-port ESP32-S3 boards the host and the device roles are
# mutually exclusive, which CONFIG_USB_MIDI_HOST_ENABLED already encodes.
if(CONFIG_USB_MIDI_HOST_ENABLED)
  set(HAS_USB_MIDI_HOST true)
else()
  set(HAS_USB_MIDI_HOST false)
endif()

if(CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE)
  set(HAS_USB_MIDI_DEVICE true)
  set(USB_MODE_NAME "USB-MIDI device")
elseif(CONFIG_USB_MIDI_USB_MODE_SERIAL)
  set(HAS_USB_MIDI_DEVICE false)
  set(USB_MODE_NAME "USB serial")
else()
  set(HAS_USB_MIDI_DEVICE false)
  set(USB_MODE_NAME "USB-MIDI host")
endif()

# SDMMC host slot / bus clock. -1 lets the driver decide, which is what every
# board wants unless its wiring says otherwise (see the CrowPanel below).
set(SD_SLOT -1)
set(SD_FREQ_KHZ -1)

# Primary I2C bus for accessories (e.g. DFRobot rotary encoders). A board
# without a dedicated I2C connector wires this to the SAM2695 UART pins
# (Port A on Tab5), so there it is mutually exclusive with SAM2695 -- pick one
# use per script. CrowPanel has its own I2C connector and can run both at
# once. The underlying hardware unit varies (PRIMARY_I2C_UNIT): most boards
# have it free as I2C0, but CrowPanel already runs its touch controller on
# I2C0, so there it is I2C1.
set(HAS_PRIMARY_I2C false)
set(PRIMARY_I2C_SDA_PIN -1)
set(PRIMARY_I2C_SCL_PIN -1)
set(PRIMARY_I2C_UNIT "")

# AMY software synth on the board's speaker (picoruby-amy). Off unless the
# board block below wires up an I2S output; see docs/AMY_SYNTH.md.
set(HAS_AMY false)

if(CONFIG_USB_MIDI_BOARD_M5STACK_TAB5)
  # M5Stack Tab5 - ESP32-P4 with SPI SD card
  set(BOARD_NAME "M5Stack Tab5 (${USB_MODE_NAME})")
  set(SD_MODE "spi")
  # SPI pins for Tab5 (ESP32-P4)
  set(SD_SCK_PIN 43)
  set(SD_MISO_PIN 39)
  set(SD_MOSI_PIN 44)
  set(SD_CS_PIN 42)
  # SDMMC pins (not used for Tab5, set to -1)
  set(SD_CLK_PIN -1)
  set(SD_CMD_PIN -1)
  set(SD_D0_PIN -1)
  # MIDI devices
  # Tab5: SAM2695 is connected via Port A (UART)
  #   Port A pin1 (SCL) = GPIO 54 -> RX
  #   Port A pin2 (SDA) = GPIO 53 -> TX
  # NOTE: GPIO 31/32 are Tab5's INTERNAL I2C (IO expanders), do NOT use.
  set(SAM2695_TX_PIN 53)
  set(SAM2695_RX_PIN 54)
  set(HAS_SAM2695 true)
  # Port A can also be wired up as I2C for accessories (e.g. DFRobot rotary
  # encoders) instead of the SAM2695 UART above -- same physical pins, pick
  # one use per script.
  set(HAS_PRIMARY_I2C true)
  set(PRIMARY_I2C_SDA_PIN 53)
  set(PRIMARY_I2C_SCL_PIN 54)
  set(PRIMARY_I2C_UNIT "ESP32_I2C0")
  # AMY: I2S0 -> ES8388 codec -> speaker amplifier (M5Unified's wiring).
  # The codec needs MCLK and is set up for MCLK = 128 x fs (reg24 = 0x00,
  # the register sequence M5Unified uses); the two must match. The codec /
  # amplifier power-up is board code in main/platform/board_audio.cpp.
  set(HAS_AMY true)
  set(AMY_I2S_BCLK 27)
  set(AMY_I2S_WS   29)
  set(AMY_I2S_DOUT 26)
  set(AMY_I2S_MCLK 30)
  set(AMY_MCLK_MULTIPLE 128)
  # USB device identity (see the injection block below)
  set(USB_PRODUCT_NAME "Midori Tab5")
  set(USB_SERIAL_NUMBER "MIDORI-TAB5-001")
elseif(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
  # Elecrow CrowPanel Advanced 7.0" - ESP32-P4 with an SDMMC card slot
  set(BOARD_NAME "Elecrow CrowPanel 7in (${USB_MODE_NAME})")
  # The TF slot's CS line is tied to GND on this board, so there is no SPI
  # chip select to drive: it is wired for SDMMC and used in 1-bit mode.
  set(SD_MODE "sdmmc")
  set(SD_CLK_PIN 43)
  set(SD_CMD_PIN 44)
  set(SD_D0_PIN 39)
  # Those three are the ESP32-P4's dedicated slot 0 IOMUX pins (see
  # soc/sdmmc_pins.h), and slot 0 cannot be reached through the GPIO matrix,
  # so the card is only addressable on slot 0. Elecrow's own BSP does the same
  # and caps the bus at 10MHz.
  set(SD_SLOT 0)
  set(SD_FREQ_KHZ 10000)
  # SPI pins (not used here, set to -1)
  set(SD_SCK_PIN -1)
  set(SD_MISO_PIN -1)
  set(SD_MOSI_PIN -1)
  set(SD_CS_PIN -1)
  # MIDI devices
  # SAM2695 goes on the UART1 header (3.3V): IO47 = TX, IO48 = RX.
  # NOTE: GPIO 45/46 are the touch panel's I2C and 43/44 the SD card, so
  # neither pair is available here.
  set(SAM2695_TX_PIN 47)
  set(SAM2695_RX_PIN 48)
  set(HAS_SAM2695 true)
  # Accessories go on the connector marked I2C (PH2.0-4P): SDA = IO45 /
  # SCL = IO46. Those are the GT911's pins too -- that connector is a tap on
  # the touch bus, not a bus of its own. M5GFX drives the touch on hardware
  # I2C0, so ours is a second peripheral (I2C1) on the same pads; measured
  # good on hardware with a DFRobot encoder. Being a separate connector from
  # the UART1 header, this is the one board where SAM2695 and the accessory
  # I2C can be used at the same time.
  # M5GFX re-points the pads at I2C0 on every touch read
  # (lgfx::i2c::beginTransaction -> set_pin) and never points them back, which
  # left this bus dead from the first screen touch onwards. picoruby-i2c's
  # ESP32 port now re-asserts the routing before every transfer
  # (i2c_reclaim_pins); see docs/CROWPANEL.md.
  set(HAS_PRIMARY_I2C true)
  set(PRIMARY_I2C_SDA_PIN 45)
  set(PRIMARY_I2C_SCL_PIN 46)
  set(PRIMARY_I2C_UNIT "ESP32_I2C1")
  # AMY: I2S -> two NS4168 I2S amplifiers (L / R), no codec, no MCLK,
  # standard (Philips) I2S. GPIO 30 switches the amplifiers' supply through
  # a P-MOSFET (active low); that is board code in
  # main/platform/board_audio.cpp. Pins from Elecrow's example and the V1.0
  # schematic (docs/AMY_SYNTH.md).
  set(HAS_AMY true)
  set(AMY_I2S_BCLK 22)
  set(AMY_I2S_WS   21)
  set(AMY_I2S_DOUT 23)
  set(AMY_I2S_MCLK -1)
  set(AMY_MCLK_MULTIPLE 256)
  set(USB_PRODUCT_NAME "Midori CrowPanel")
  set(USB_SERIAL_NUMBER "MIDORI-CROWPANEL-001")
elseif(CONFIG_USB_MIDI_BOARD_M5STACK_CORES3)
  set(BOARD_NAME "M5Stack CoreS3 SE (${USB_MODE_NAME})")
  set(SD_MODE "spi")
  # SPI pins for M5Stack
  set(SD_SCK_PIN 36)
  set(SD_MISO_PIN 35)
  set(SD_MOSI_PIN 37)
  set(SD_CS_PIN 4)
  # SDMMC pins (not used for M5Stack, set to -1)
  set(SD_CLK_PIN -1)
  set(SD_CMD_PIN -1)
  set(SD_D0_PIN -1)
  # MIDI devices
  set(SAM2695_TX_PIN 17)
  set(SAM2695_RX_PIN 18)
  set(HAS_SAM2695 true)
  # Same pins can be rewired as I2C for accessories instead of the SAM2695
  # UART -- same physical pins, pick one use per script.
  set(HAS_PRIMARY_I2C true)
  set(PRIMARY_I2C_SDA_PIN 17)
  set(PRIMARY_I2C_SCL_PIN 18)
  set(PRIMARY_I2C_UNIT "ESP32_I2C0")
  set(USB_PRODUCT_NAME "Midori CoreS3")
  set(USB_SERIAL_NUMBER "MIDORI-CORES3-001")
else()
  # Default to Freenove - SDMMC mode (requires PSRAM disabled to avoid GPIO conflict)
  set(BOARD_NAME "Freenove ESP32-S3 (${USB_MODE_NAME})")
  set(SD_MODE "sdmmc")
  # SDMMC pins for Freenove (from official documentation)
  set(SD_CLK_PIN 39)
  set(SD_CMD_PIN 38)
  set(SD_D0_PIN 40)
  # SPI pins (not used for Freenove, set to -1)
  set(SD_SCK_PIN -1)
  set(SD_MISO_PIN -1)
  set(SD_MOSI_PIN -1)
  set(SD_CS_PIN -1)
  # MIDI devices
  set(SAM2695_TX_PIN 17)
  set(SAM2695_RX_PIN 18)
  set(HAS_SAM2695 true)
  # Same pins can be rewired as I2C for accessories instead of the SAM2695
  # UART -- same physical pins, pick one use per script.
  set(HAS_PRIMARY_I2C true)
  set(PRIMARY_I2C_SDA_PIN 17)
  set(PRIMARY_I2C_SCL_PIN 18)
  set(PRIMARY_I2C_UNIT "ESP32_I2C0")
  set(USB_PRODUCT_NAME "Midori Freenove")
  set(USB_SERIAL_NUMBER "MIDORI-FREENOVE-001")
endif()

# Configure picoruby-usb_midi_device for Midori.
#
# The gem itself carries no product identity: it is MIDI-only and named
# "PicoRuby MIDI" unless the host project injects these defines (see
# mrbgems/picoruby-usb_midi_device/include/usb_midi_device_config.h).
# Midori enables the optional CDC interface on the boards where the console
# (and `idf.py monitor`) has to ride on the same USB-C connector in this mode.
# The CrowPanel keeps its console on the CH343 UART bridge, so there it is a
# plain MIDI device with no CDC interface at all.
if(CONFIG_USB_MIDI_USB_MODE_MIDI_DEVICE)
  if(CONFIG_USB_MIDI_CONSOLE_ON_CDC)
    set(USB_MIDI_DEVICE_CDC 1)
  else()
    set(USB_MIDI_DEVICE_CDC 0)
  endif()
  # Which USB port the device connector is soldered to. The ESP32-P4 has two
  # (full-speed OTG1.1 and high-speed OTG2.0) and only one of them reaches a
  # given connector; the ESP32-S3 has a single full-speed port.
  #   Tab5     - USB-C on the FS pads that USB-Serial/JTAG owns by default,
  #              hence the PHY mux swap inside the gem.
  #   CrowPanel- the connector Elecrow labels "USB 2.0" is the HS port; their
  #              own TinyUSB example runs on esp_tinyusb's P4 default, which
  #              is TINYUSB_PORT_HIGH_SPEED_0.
  if(CONFIG_USB_MIDI_BOARD_ELECROW_CROWPANEL)
    set(USB_MIDI_DEVICE_HS 1)
  else()
    set(USB_MIDI_DEVICE_HS 0)
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    USB_MIDI_DEVICE_ENABLED=1
    USB_MIDI_DEVICE_WITH_CDC=${USB_MIDI_DEVICE_CDC}
    USB_MIDI_DEVICE_HIGH_SPEED=${USB_MIDI_DEVICE_HS}
    USB_MIDI_DEVICE_MANUFACTURER=\"Midori\"
    USB_MIDI_DEVICE_PRODUCT=\"${USB_PRODUCT_NAME}\"
    USB_MIDI_DEVICE_SERIAL=\"${USB_SERIAL_NUMBER}\"
  )
endif()

# Configure picoruby-amy for this board. Like picoruby-usb_midi_device, the
# gem knows no board: pins and the scheduling knobs come in as defines (see
# mrbgems/picoruby-amy/include/amy_gem_config.h). Without them the gem's
# port compiles to stubs.
if(HAS_AMY)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AMY_GEM_ENABLED=1
    AMY_GEM_I2S_BCLK=${AMY_I2S_BCLK}
    AMY_GEM_I2S_WS=${AMY_I2S_WS}
    AMY_GEM_I2S_DOUT=${AMY_I2S_DOUT}
    AMY_GEM_I2S_MCLK=${AMY_I2S_MCLK}
    AMY_GEM_I2S_PHILIPS=1
    AMY_GEM_MCLK_MULTIPLE=${AMY_MCLK_MULTIPLE}
    # Core 0 above the UI loop (app_main, priority 1), away from the Ruby
    # VM on core 1 -- see "タスクとコア配置" in docs/AMY_SYNTH.md.
    AMY_GEM_TASK_CORE=0
    AMY_GEM_TASK_PRIORITY=3
    # The gem's 0.7 tripped the failsafe (silence + synths wiped) while
    # turning knobs. 0.9 still leaves the UI loop 10% of core 0.
    AMY_GEM_OVERLOAD_THRESHOLD=0.9f
  )
  # For measurements only: `idf.py -DAMY_REVERB_INTERNAL_MIN_FREE=0 build`
  # puts the reverb lines in internal RAM whatever is free (see
  # docs/AMY_SYNTH.md). Left unset, the gem's default applies.
  if(DEFINED AMY_REVERB_INTERNAL_MIN_FREE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
      AMY_GEM_REVERB_INTERNAL_MIN_FREE=${AMY_REVERB_INTERNAL_MIN_FREE})
  endif()
endif()

configure_file(
  ${COMPONENT_DIR}/mrblib/board_config.rb.in
  ${COMPONENT_DIR}/mrblib/board_config.rb
  @ONLY
)

# Concatenate board_config.rb and main_task_base.rb into main_task.rb
# This ensures BoardConfig module is defined before main_task uses it
file(READ ${COMPONENT_DIR}/mrblib/board_config.rb BOARD_CONFIG_CONTENT)
file(READ ${COMPONENT_DIR}/mrblib/main_task_base.rb MAIN_TASK_CONTENT)
file(WRITE ${COMPONENT_DIR}/mrblib/main_task.rb "${BOARD_CONFIG_CONTENT}\n${MAIN_TASK_CONTENT}")

# Compile Ruby files
set(RUBY_FILES main_task)
# The host compiler the cross build makes for itself (mruby builds an mrbc
# for every cross target; R2P2-ESP32 instead runs a separate host build that
# links it as bin/mrbc).
set(PICORBC ${COMPONENT_DIR}/picoruby/build/mrbc/default/bin/mrbc)
set(GENERATED_C_FILES "")

foreach(rb ${RUBY_FILES})
  set(C_FILE ${COMPONENT_DIR}/mrb/${rb}.c)
  set(RB_FILE ${COMPONENT_DIR}/mrblib/${rb}.rb)
  add_custom_command(
    OUTPUT ${C_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling ${RB_FILE}..."
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COMPONENT_DIR}/mrb
    COMMAND ${PICORBC} -B${rb} -o${C_FILE} ${RB_FILE}
    DEPENDS ${CUSTOM_TARGET_NAME} ${RB_FILE}
    COMMENT "Compiling ${RB_FILE}"
    VERBATIM
  )
  list(APPEND GENERATED_C_FILES ${C_FILE})
endforeach(rb)

target_sources(${COMPONENT_LIB} PRIVATE ${GENERATED_C_FILES})
