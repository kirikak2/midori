#!/bin/bash
# Builds the simulator: PicoRuby (libmruby.a) + the C/C++ half
# (libmidori_sim.a) -> web/wasm/midori-sim-<board>.{js,wasm}
#
#   sim/build.sh                 # every board (tab5 cores3); loads emsdk itself if emcc is not on PATH
#   BOARDS=cores3 sim/build.sh   # one board
#
# The screen's size and layout are compile-time (main/ui/ui_common.h), so each
# board is a wasm of its own; libmruby.a (the Ruby side) is shared.
set -euo pipefail

# Emscripten: use the one on PATH, else load emsdk ($EMSDK, then ~/emsdk)
if ! command -v emcc >/dev/null 2>&1; then
  for d in "${EMSDK:-}" "$HOME/emsdk"; do
    if [[ -n "$d" && -f "$d/emsdk_env.sh" ]]; then
      # shellcheck disable=SC1091
      source "$d/emsdk_env.sh" >/dev/null 2>&1
      break
    fi
  done
  command -v emcc >/dev/null 2>&1 || {
    echo "emcc not found: install emsdk (https://emscripten.org) or set EMSDK" >&2
    exit 1
  }
fi
SIM=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SIM/.." && pwd)
BOARDS=${BOARDS:-${BOARD:-tab5 cores3}}
PICORUBY=$ROOT/components/midori-runtime/picoruby
LIBMRUBY=$PICORUBY/build/midori-sim/lib/libmruby.a

# rake does not notice a changed gem mrblib (the same as the device build,
# CLAUDE.md): drop the build of a MIDoRI gem whose mrblib or binding is newer
# than libmruby.a, so rake makes it again
if [[ -f "$LIBMRUBY" ]]; then
  for gem in "$ROOT"/mrbgems/picoruby-*; do
    name=$(basename "$gem")
    [[ -d "$PICORUBY/build/midori-sim/mrbgems/$name" ]] || continue
    if [[ -n $(find "$gem/mrblib" "$gem/src" -newer "$LIBMRUBY" -type f 2>/dev/null | head -1) ]]; then
      rm -rf "$PICORUBY/build/midori-sim/mrbgems/$name"
    fi
  done
fi
(cd "$PICORUBY" && MRUBY_CONFIG=$SIM/build_config/midori-sim.rb rake -m -j8 "$LIBMRUBY" >/dev/null)
OUT=$SIM/web/wasm
mkdir -p "$OUT"
for BOARD in $BOARDS; do
  BOARD=$BOARD "$SIM/build_c.sh" >/dev/null

  EXPORTS='["_picorb_init","_picorb_create_task","_picorb_create_task_with_filename","_mrb_tick_wasm","_mrb_run_step","_mrb_run_step_status","_mrb_gc_scheduler_pending_wasm","_malloc","_free",
  "_sim_boot","_sim_frame","_sim_touch","_sim_rtos_poll","_sim_screen_width","_sim_screen_height",
  "_sim_midi_in","_sim_midi_connect","_web_midi_port","_web_midi_connected","_web_midi_in","_sim_amy_render","_sim_amy_running","_sim_amy_sample_rate","_sim_amy_block_size","_sim_scripts_clear","_sim_scripts_add","_sim_scripts_done","_sim_set_current_script"]'
  em++ -g0 -O2 \
    -fwasm-exceptions -sSUPPORT_LONGJMP=wasm \
    -sWASM=1 -sEXPORT_ES6=1 -sMODULARIZE=1 -sEXPORT_NAME=createMidoriSim \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPU8","HEAPU16","HEAP16","HEAP32","FS"]' \
    -sEXPORTED_FUNCTIONS="$(echo $EXPORTS | tr -d '\n ')" \
    -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2MB \
    -sENVIRONMENT=web,node -sWASM_ASYNC_COMPILATION=1 \
    -sERROR_ON_UNDEFINED_SYMBOLS=1 \
    --no-entry \
    -Wl,--whole-archive "$SIM/out/$BOARD/libmidori_sim.a" -Wl,--no-whole-archive "$LIBMRUBY" \
    -o "$OUT/midori-sim-$BOARD.js"
  # Ruby the page runs before the user's script: BoardConfig, the part of
  # main_task_base.rb shared with the device, then the simulator's start-up
  MARK='# ---- The MIDoRI simulator (sim/build.sh) takes this file up to here'
  {
    cat "$SIM/mrblib/board_config_$BOARD.rb"
    sed "/^$(printf '%s' "$MARK" | sed 's/[][\/.*^$]/\\&/g')/q" "$ROOT/components/midori-runtime/mrblib/main_task_base.rb"
    cat "$SIM/mrblib/sim_boot.rb"
  } > "$OUT/sim_boot-$BOARD.rb"
  grep -q "$MARK" "$OUT/sim_boot-$BOARD.rb" || { echo "marker not found in main_task_base.rb" >&2; exit 1; }
  for f in sim_ui.rb sim_script.rb sim_tail.rb; do cp "$SIM/mrblib/$f" "$OUT/$f"; done
done
# The page fetches every file with ?v=<this id> (web/device.js)
printf '{"id":"%s"}\n' "$(date +%Y%m%d%H%M%S)-$$" > "$OUT/build.json"
ls -la "$OUT"/midori-sim-*.wasm "$OUT"/sim_*.rb
