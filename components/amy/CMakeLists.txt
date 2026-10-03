# AMY synthesizer engine (https://github.com/shorepine/amy) as an ESP-IDF
# component.
#
# The sources live in the picoruby-amy gem's submodule
# (mrbgems/picoruby-amy/lib/amy); AMY ships no IDF component of its own.
# Kept apart from picoruby-esp32 so it gets its own compile options.
#
# Only the ESP32-P4 boards (Tab5 / CrowPanel) play audio through AMY; on
# other targets this is an empty component. Keyed off IDF_TARGET, not a
# CONFIG_ symbol: requirements are collected in an early expansion pass
# where CONFIG_ variables do not exist yet.

if(NOT IDF_TARGET STREQUAL "esp32p4")
  idf_component_register()
  return()
endif()

set(AMY_SRC_DIR ${COMPONENT_DIR}/../../mrbgems/picoruby-amy/lib/amy/src)

# AMY's own library sources (its Makefile's SOURCES), with the desktop
# audio backend (libminiaudio-audio.c) swapped for the MCU one (i2s.c,
# which also holds AMY's ESP32 render loop). Left out: usb.c (TinyUSB
# descriptors -- Midori runs its own USB stack), the desktop MIDI
# backends, the Python / example front ends.
set(AMY_SRCS
  algorithms.c amy.c envelope.c examples.c parse.c filters.c oscillators.c
  pcm.c interp_partials.c custom.c delay.c log2_exp2.c patches.c transfer.c
  sequencer.c instrument.c amy_midi.c api.c midi_mappings.c cv_trigger.c
  note_output.c i2s.c
)
list(TRANSFORM AMY_SRCS PREPEND ${AMY_SRC_DIR}/)

idf_component_register(
  SRCS ${AMY_SRCS}
  INCLUDE_DIRS ${AMY_SRC_DIR}
  PRIV_REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_uart esp_timer
)

# The render path is all integer DSP: optimise it for speed whatever the
# project-wide optimisation level is. AMY is third-party code built with
# its own warning set, so warnings are not promoted to errors here.
target_compile_options(${COMPONENT_LIB} PRIVATE
  -O2
  -Wno-error
  -Wno-unused-variable
  -Wno-unused-function
  -Wno-unused-but-set-variable
  -Wno-maybe-uninitialized
  -Wno-format
)
