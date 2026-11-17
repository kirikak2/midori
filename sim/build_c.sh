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
# AMY (picoruby-amy's engine): the device's list (components/amy/CMakeLists.txt)
# with i2s.c -- the device's audio loop -- replaced by sim/port/amy_platform_sim.c
AMY_DIR=mrbgems/picoruby-amy/lib/amy/src
AMY_SRCS=()
for f in algorithms amy envelope examples parse filters oscillators pcm interp_partials custom \
         delay log2_exp2 patches transfer sequencer instrument amy_midi api midi_mappings \
         cv_trigger note_output; do AMY_SRCS+=("$AMY_DIR/$f.c"); done
INC+=(-I$AMY_DIR -Imrbgems/picoruby-amy/include -DAMY_NO_MINIAUDIO
      -DAMY_GEM_ENABLED=1 -DAMY_GEM_POLLED=1 -DAMY_GEM_OVERLOAD_THRESHOLD=0.9f)
SRCS=(
  $GFX/lgfx/v1/LGFXBase.cpp $GFX/lgfx/v1/LGFX_Sprite.cpp $GFX/lgfx/v1/LGFX_Button.cpp
  $GFX/lgfx/v1/lgfx_fonts.cpp
  $GFX/lgfx/v1/panel/Panel_Device.cpp $GFX/lgfx/v1/panel/Panel_FrameBufferBase.cpp
  $GFX/lgfx/v1/misc/*.cpp $GFX/lgfx/v1/platforms/sdl/common.cpp $GFX/lgfx/utility/*.c
  main/ui/*.cpp main/ui/*.c
  mrbgems/picoruby-midi/ports/esp32/midi.c mrbgems/picoruby-midi/ports/esp32/midi_timed_ring.c
  mrbgems/picoruby-ui/ports/esp32/ui.c
  sim/port/*.c sim/port/*.cpp
  mrbgems/picoruby-amy/ports/esp32/amy_port.c
  "${AMY_SRCS[@]}"
)
# An object is rebuilt when its source or any header it included is newer.
# The compiler lists the headers in <obj>.d (-MMD). Only comparing the source
# left objects built against an old header in the library: when
# UI_SCREEN_COUNT grew, a stale screen_main.cpp.o read UIManager's BPM at the
# old offset and a swipe on the Tempo started from 20.
stale() {
  local o=$1 s=$2
  [[ -f "$o" && -f "$o.d" && ! "$s" -nt "$o" ]] || return 0
  local deps
  deps=$(sed -e 's/^[^:]*://' -e 's/\\$//' "$o.d" | tr -s ' \t' '\n' | grep -v '^$')
  # A header that is gone (renamed) counts as changed
  while IFS= read -r d; do [[ -f "$d" ]] || return 0; done <<< "$deps"
  [[ -n $(xargs -r -d '\n' -I{} find {} -maxdepth 0 -newer "$o" <<< "$deps" | head -1) ]]
}
OBJS=()
for s in "${SRCS[@]}"; do
  o="$OUT/obj/$(echo "$s" | sed 's|[/.]|_|g').o"
  if stale "$o" "$s"; then
    DEP=(-MMD -MF "$o.d")
    if [[ "$s" == $AMY_DIR/* ]]; then emcc -std=gnu11 $OPT -w "${INC[@]}" "${DEP[@]}" -c "$s" -o "$o"   # third party: its own warnings
    elif [[ "$s" == *.c ]]; then emcc -std=gnu11 $OPT "${INC[@]}" "${DEP[@]}" -c "$s" -o "$o"
    else em++ -std=gnu++17 $OPT "${INC[@]}" "${DEP[@]}" -c "$s" -o "$o"; fi
  fi
  OBJS+=("$o")
done
rm -f "$OUT/libmidori_sim.a"
emar rcs "$OUT/libmidori_sim.a" "${OBJS[@]}"
echo "$OUT/libmidori_sim.a"
