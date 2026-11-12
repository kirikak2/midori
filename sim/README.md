# MIDoRI Simulator

The MIDoRI firmware's screen and Ruby runtime, built for the browser with
Emscripten. Design: [docs/WEB_SIMULATOR.md](../docs/WEB_SIMULATOR.md).

The simulator does not reimplement the screens: `main/ui` (C++, LovyanGFX),
the gems' ESP32 ports and their Ruby are the device's own code, built over a
thin layer that stands in for ESP-IDF, FreeRTOS and M5Unified.

## Build and run

```bash
source ~/emsdk/emsdk_env.sh
sim/build.sh                       # -> sim/web/wasm/midori-sim-tab5.{js,wasm} + sim_*.rb
cd sim/web && python3 -m http.server 8080
# open http://localhost:8080/
```

`sim/build.sh` runs rake for PicoRuby (`build_config/midori-sim.rb` →
`components/midori-runtime/picoruby/build/midori-sim/lib/libmruby.a`), then
`build_c.sh` for the C/C++ half (`out/<board>/libmidori_sim.a`), and links
them. A changed gem `mrblib/*.rb` or binding is picked up by rake; a changed
port or `main/ui` file by `build_c.sh` (it rebuilds what is newer than its
object).

Node smoke tests (no browser needed):

```bash
node sim/test/node_knob.mjs                         # a Ruby knob turned by a drag
node sim/test/run_script.mjs script.rb shot.png 400,400   # run, tap, screenshot
```

## Layout

| Path | What |
|------|------|
| `build_config/midori-sim.rb` | PicoRuby build: picoruby-wasm + the MIDoRI gems (bindings and mrblib; their ports come from `build_c.sh`) |
| `build_c.sh` | LovyanGFX (from M5GFX), `main/ui`, picoruby-midi / picoruby-ui ESP32 ports, `port/` → `libmidori_sim.a` |
| `build.sh` | Both, linked into the page's wasm; also writes the boot Ruby |
| `shim/` | Headers in place of ESP-IDF / FreeRTOS / M5Unified / SDL2, and `board/<board>/sdkconfig.h` |
| `port/` | Their implementations: framebuffer panel, M5GFX, esp_timer / queues (`rtos_sim.c`), NVS on localStorage, transports to the page (`sim_devices.c`), midori-runtime's C API (`sim_runtime.c`), page entry points (`platform_sim.cpp`) |
| `mrblib/` | Boot Ruby: BoardConfig, then (by `build.sh`) the part of `main_task_base.rb` before its simulator marker, then `sim_boot.rb`; `sim_tail.rb` after the user's script |
| `web/` | The page: `device.js` (one wasm instance = one boot), `synth.js` (built-in synth = SAM2695), `webmidi.js` (Web MIDI ports in the device's roles) |
| `test/` | Node smoke tests |

## How the device's code runs on one thread

- **Display**: `Panel_WasmFB` is LovyanGFX's `Panel_FrameBufferBase` (Tab5's
  base too) over plain memory; `display()` hands the changed rectangle to the
  page, which converts RGB565 to RGBA on the canvas.
- **Touch**: the page's pointers (up to 5; Alt+click pins one) go to the
  `M5.Touch` shim with M5Unified's semantics.
- **Tasks**: none. FreeRTOS mutexes and critical sections do nothing. What is a
  task on the device runs a pass at a time from the page's loops:
  - the UI task → `sim_frame()` every animation frame;
  - esp_timer → a table run by `sim_rtos_poll()`;
  - picoruby-midi's input task → `MIDI_Input_poll()` (`MIDI_INPUT_POLLED`);
  - the Sequencer task → a step draining its queue (`MIDORI_SIM` in `ui_sequencer.cpp`).
- **Ruby**: picoruby-wasm's scheduler, pumped by the page between frames
  (`sleep_ms` yields to the browser).
- **MIDI**: the USB-MIDI Host / Device and UART (SAM2695) gems' C functions
  are `port/sim_devices.c`: sending calls the page (Web MIDI output or the
  built-in synth, stamped with the C side's time), receiving is a ring the
  page fills from Web MIDI input. BLE-MIDI is absent.

## Status

Stage 0 of docs/WEB_SIMULATOR.md §9: Tab5 only; Home, Knobs, Pads, Keys,
Layout run with their Ruby; MIDI out to the built-in synth / Web MIDI. Not yet:
virtual `/sd` and Scripts, `MIDIDevices.web_midi` (picoruby-web_midi), AMY,
the designer, SysEx, CI.
