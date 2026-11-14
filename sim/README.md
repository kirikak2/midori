# MIDoRI Simulator

The MIDoRI firmware's screen and Ruby runtime, built for the browser with
Emscripten. Design: [docs/WEB_SIMULATOR.md](../docs/WEB_SIMULATOR.md).

The simulator does not reimplement the screens: `main/ui` (C++, LovyanGFX),
the gems' ESP32 ports and their Ruby are the device's own code, built over a
thin layer that stands in for ESP-IDF, FreeRTOS and M5Unified.

## Build and run

```bash
sim/build.sh                       # -> sim/web/wasm/midori-sim-{tab5,cores3}.{js,wasm} + sim_*.rb
BOARDS=cores3 sim/build.sh         # one board
ruby sim/serve.rb                  # http://localhost:8080/ (sends Cache-Control: no-store; gem install webrick)
                                   # the designer: http://localhost:8080/designer.html
```

Each build writes `web/wasm/build.json` (an id) and the page fetches the wasm,
its JS and the boot Ruby with `?v=<id>`, so a browser never mixes files of two
builds. `serve.rb` keeps the page's own scripts fresh too; with another server,
reload with the cache bypassed after changing `web/*.js`.

`sim/build.sh` runs rake for PicoRuby (`build_config/midori-sim.rb` →
`components/midori-runtime/picoruby/build/midori-sim/lib/libmruby.a`), then
`build_c.sh` for the C/C++ half (`out/<board>/libmidori_sim.a`), and links
them. rake does not notice a changed gem `mrblib/*.rb` (as on the device), so
`build.sh` first drops the build of a MIDoRI gem whose mrblib or binding is
newer than libmruby.a. A changed port or `main/ui` file is picked up by
`build_c.sh` (it rebuilds what is newer than its object).

Node smoke tests (no browser needed):

```bash
node sim/test/node_knob.mjs                         # a Ruby knob turned by a drag
node sim/test/run_script.mjs script.rb shot.png 400,400   # run, tap, screenshot
PORTS='Keystation:i,IAC Driver Bus 1:io' node sim/test/run_script.mjs ...   # with fake Web MIDI ports
node sim/test/node_web_midi.mjs                     # MIDIDevices.web_midi: route, MIDI.on, send
node sim/test/node_amy.mjs                          # AMY: notes, sound level, render time
node sim/test/node_modes.mjs ui shot.png 385,350    # UI mode (Home -> AMY), taps, screenshot
BOARD=cores3 node sim/test/node_modes.mjs ui shot.png   # the same on CoreS3 (also run_script.mjs)
node sim/test/node_sysex.mjs                        # SysEx: routed dump, send_sysex, reassembly
node sim/test/node_designer.mjs shot.png            # designer: model -> project script -> run, taps
node sim/test/node_designer_amy.mjs shot.png        # designer: AMY parts, envelopes, sequencer (Tab5)
node sim/test/node_designer_screens.mjs shot.png    # designer: Pads / Knobs screens, routes, Home's menu
node sim/test/node_link.mjs                         # midori_link.js against a fake MIDoRI (console, PicoModem)
node sim/test/node_startup.mjs                      # start-up shared with the device: /app.rb, /plugins then /sd/plugins
node sim/test/node_scripts.mjs shot.png 200,108 640,690   # the Scripts screen with a device's list: pick, Run
```

## Layout

| Path | What |
|------|------|
| `build_config/midori-sim.rb` | PicoRuby build: picoruby-wasm + the MIDoRI gems (bindings and mrblib; their ports come from `build_c.sh`) |
| `build_c.sh` | LovyanGFX (from M5GFX), `main/ui`, picoruby-midi / picoruby-ui ESP32 ports, `port/` → `libmidori_sim.a` |
| `build.sh` | Both, linked into the page's wasm; also writes the boot Ruby |
| `shim/` | Headers in place of ESP-IDF / FreeRTOS / M5Unified / SDL2, and `board/<board>/sdkconfig.h` (tab5, cores3) |
| `port/` | Their implementations: framebuffer panel, M5GFX, esp_timer / queues (`rtos_sim.c`), NVS on localStorage, transports to the page (`sim_devices.c`), midori-runtime's C API (`sim_runtime.c`), page entry points (`platform_sim.cpp`) |
| `mrblib/` | Boot Ruby: BoardConfig, then (by `build.sh`) the part of `main_task_base.rb` before its simulator marker, then `sim_boot.rb`; then `sim_ui.rb` (UI mode: devices, Sequencer, AMY panel) or `sim_script.rb` + the user's script + `sim_tail.rb` (script mode) |
| `web/` | The page: `device.js` (one wasm instance = one boot), `usb_midi.js` (messages <-> USB-MIDI packets, SysEx included), `synth.js` (built-in synth = SAM2695), `amy_audio.js` (AMY's AudioWorklet), `webmidi.js` (Web MIDI ports in the device's roles and by name) |
| `web/midori_link.js`, `web/device_panel.js` | The real MIDoRI over USB CDC (Web Serial): console commands, PicoModem, the page's panel (docs §13) |
| `web/designer.html`, `web/designer/` | The designer (docs §12.6): `model.js` (project model, widget settings), `codegen.js` (project script), `app.js` (the page) |
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
- **Modes**: as the device's Supervisor. The page opens in UI mode (Home,
  with the AMY panel); Run starts the script in a fresh VM; when it ends or is
  stopped, UI mode starts again in a fresh VM.
- **Ruby**: picoruby-wasm's scheduler, pumped by the page between frames
  (`sleep_ms` yields to the browser).
- **MIDI**: the USB-MIDI Host / Device and UART (SAM2695) gems' C functions
  are `port/sim_devices.c`: sending calls the page (Web MIDI output or the
  built-in synth, stamped with the C side's time), receiving is a ring the
  page fills from Web MIDI input. BLE-MIDI is absent.
- **AMY**: its engine (the device's sources, with `port/amy_platform_sim.c` in
  place of `i2s.c`) and picoruby-amy's port with `AMY_GEM_POLLED`: the page
  asks for blocks (`sim_amy_render`) to keep ~60 ms queued in an AudioWorklet
  (`web/amy_audio.js`). 48 kHz, as AMY's web build.

## Status

Stage 0 of docs/WEB_SIMULATOR.md §9: Tab5 and CoreS3 (§12.5); Home, Knobs, Pads, Keys,
Layout run with their Ruby; MIDI out to the built-in synth / Web MIDI; every
Web MIDI port is also `MIDIDevices.web_midi("name")` and a MIDI Devices row /
column ([picoruby-web_midi](../mrbgems/picoruby-web_midi/), §12.1); AMY plays
through an AudioWorklet (§12.2). SysEx passes both ways (§12.4). The designer
(`designer.html`, §12.6) builds layouts and generates the project script. A connected MIDoRI (USB CDC) gives the page its scripts, and the
simulator its plugins and /app.rb (§13). Not yet: the designer's live
patching and device panel, CI.
