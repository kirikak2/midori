#!/bin/bash
# Builds the C/C++ half of the simulator (LovyanGFX, main/ui, the gems'
# ESP32 ports over the shims) into out/<board>/libmidori_sim.a.
# picoruby-midi's core (src/*.c) comes with libmruby.a (sim/build_config).
set -euo pipefail
cd "$(dirname "$0")/.."
BOARD=${BOARD:-tab5}
OUT=sim/out/$BOARD
GFX=managed_components/m5stack__m5gfx/src
mkdir -p "$OUT/obj"
INC=(-Isim/shim/board/$BOARD -Isim/shim -Isim/port -I$GFX -Imain/ui -Imain/platform
     -Imrbgems/picoruby-midi/include -Imrbgems/picoruby-ui/include -Imrbgems/picoruby-usb_midi_host/include
     -Imrbgems/picoruby-usb_midi_device/include -Imrbgems/picoruby-ble_midi_device/include
     -Imrbgems/picoruby-ble_midi_host/include -Imrbgems/picoruby-uart_midi/include
     -Imrbgems/picoruby-amy/include -Icomponents/midori-runtime
     -DLGFX_USE_V1 -DMIDORI_SIM=1 -DMIDI_INPUT_POLLED=1)
OPT="${OPT:--O2} -fwasm-exceptions"
SRCS=(
  $GFX/lgfx/v1/LGFXBase.cpp $GFX/lgfx/v1/LGFX_Sprite.cpp $GFX/lgfx/v1/LGFX_Button.cpp
  $GFX/lgfx/v1/lgfx_fonts.cpp
  $GFX/lgfx/v1/panel/Panel_Device.cpp $GFX/lgfx/v1/panel/Panel_FrameBufferBase.cpp
  $GFX/lgfx/v1/misc/*.cpp $GFX/lgfx/v1/platforms/sdl/common.cpp $GFX/lgfx/utility/*.c
  main/ui/*.cpp main/ui/*.c
  mrbgems/picoruby-midi/ports/esp32/midi.c mrbgems/picoruby-midi/ports/esp32/midi_timed_ring.c
  mrbgems/picoruby-ui/ports/esp32/ui.c
  sim/port/*.c sim/port/*.cpp
)
OBJS=()
for s in "${SRCS[@]}"; do
  o="$OUT/obj/$(echo "$s" | sed 's|[/.]|_|g').o"
  if [[ ! -f "$o" || "$s" -nt "$o" ]]; then
    if [[ "$s" == *.c ]]; then emcc -std=gnu11 $OPT "${INC[@]}" -c "$s" -o "$o"
    else em++ -std=gnu++17 $OPT "${INC[@]}" -c "$s" -o "$o"; fi
  fi
  OBJS+=("$o")
done
rm -f "$OUT/libmidori_sim.a"
emar rcs "$OUT/libmidori_sim.a" "${OBJS[@]}"
echo "$OUT/libmidori_sim.a"
