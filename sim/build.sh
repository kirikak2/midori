#!/bin/bash
# Builds the simulator: PicoRuby (libmruby.a) + the C/C++ half
# (libmidori_sim.a) -> web/wasm/midori-sim-<board>.{js,wasm}
#
#   source ~/emsdk/emsdk_env.sh
#   sim/build.sh            # BOARD=tab5 (default)
set -euo pipefail
SIM=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SIM/.." && pwd)
BOARD=${BOARD:-tab5}
PICORUBY=$ROOT/components/midori-runtime/picoruby
LIBMRUBY=$PICORUBY/build/midori-sim/lib/libmruby.a

# Gem mrblib / bindings changed? rake only rebuilds what is out of date
(cd "$PICORUBY" && MRUBY_CONFIG=$SIM/build_config/midori-sim.rb rake -m -j8 "$LIBMRUBY" >/dev/null)
BOARD=$BOARD "$SIM/build_c.sh" >/dev/null

OUT=$SIM/web/wasm
mkdir -p "$OUT"
EXPORTS='["_picorb_init","_picorb_create_task","_picorb_create_task_with_filename","_mrb_tick_wasm","_mrb_run_step","_mrb_run_step_status","_mrb_gc_scheduler_pending_wasm","_malloc","_free",
"_sim_boot","_sim_frame","_sim_touch","_sim_rtos_poll","_sim_screen_width","_sim_screen_height",
"_sim_midi_in","_sim_midi_connect","_sim_scripts_clear","_sim_scripts_add","_sim_scripts_done","_sim_set_current_script"]'
em++ -g0 -O2 \
  -fwasm-exceptions -sSUPPORT_LONGJMP=wasm \
  -sWASM=1 -sEXPORT_ES6=1 -sMODULARIZE=1 -sEXPORT_NAME=createMidoriSim \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPU8","HEAPU16","HEAP32","FS"]' \
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
cp "$SIM/mrblib/sim_tail.rb" "$OUT/sim_tail.rb"
grep -q "$MARK" "$OUT/sim_boot-$BOARD.rb" || { echo "marker not found in main_task_base.rb" >&2; exit 1; }
ls -la "$OUT/midori-sim-$BOARD".* "$OUT"/sim_*.rb
