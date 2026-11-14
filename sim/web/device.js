// The MIDoRI device in a page: one wasm instance = one boot of the device.
//
//   const dev = await MidoriDevice.start({ canvas, mode: 'ui' });             // as at power-up
//   const dev = await MidoriDevice.start({ canvas, script, onScriptEnd });    // a script
//   dev.stop();
//
// Running a script makes a new instance, as the device's Supervisor opens a
// fresh VM per script (docs/WEB_SIMULATOR.md §3.1). The compiled module is
// fetched once and reused.

import { toUsbPackets } from './usb_midi.js';

// The boards there is a build for (sim/build.sh): the screen's size and
// layout are compile-time, so each is a wasm of its own
export const BOARDS = {
  tab5:   { name: 'M5Stack Tab5',   width: 1280, height: 720 },
  cores3: { name: 'M5Stack CoreS3', width: 320,  height: 240 },
};
export const DEFAULT_BOARD = 'tab5';
const MRB_TICK_UNIT = 4;      // build_config: MRB_TICK_UNIT=4
const RUBY_SLICE_MS = 8;      // Ruby runs this long per turn, then the page gets the thread back
const MAX_CATCHUP_TICKS = 10;
const MAX_FINGERS = 5;

// sim/build.sh writes wasm/build.json with an id per build. Every file of a
// build is fetched with ?v=<id>: a browser that kept a file from an earlier
// build (a plain static server sends no cache headers) cannot mix it in --
// an old boot script with a new wasm said "AMY not available on this board".
let buildPromise = null;
function loadBuild() {
  if (!buildPromise) {
    buildPromise = fetch(new URL('./wasm/build.json', import.meta.url), { cache: 'no-store' })
      .then(r => (r.ok ? r.json() : {}))
      .then(j => String(j.id || Date.now()))
      .catch(() => String(Date.now()));
  }
  return buildPromise;
}
function wasmUrl(name, id) {
  return new URL(`./wasm/${name}?v=${encodeURIComponent(id)}`, import.meta.url).href;
}

const modulePromises = {};
function loadFactory(board, id) {
  if (!modulePromises[board]) {
    modulePromises[board] = import(wasmUrl(`midori-sim-${board}.js`, id)).then(m => m.default);
  }
  return modulePromises[board];
}

// Ruby run before and after the user's script (sim/build.sh makes them from
// sim/mrblib and the part of main_task_base.rb shared with the device)
const rubyPromises = {};
function loadRuby(board, id) {
  if (!rubyPromises[board]) {
    const get = name => fetch(wasmUrl(name, id)).then(r => {
      if (!r.ok) throw new Error(`${name}: ${r.status}`);
      return r.text();
    });
    rubyPromises[board] = Promise.all([get(`sim_boot-${board}.rb`), get('sim_ui.rb'),
                                       get('sim_script.rb'), get('sim_tail.rb')])
      .then(([boot, ui, script, tail]) => ({ boot, ui, script, tail }));
  }
  return rubyPromises[board];
}

// What the VM runs, as the device's main_task.rb: UI mode, or script mode
// with the user's script, wrapped so an error is reported and the device
// goes back to UI mode, as the Supervisor does on the device.
function vmSource(ruby, mode, user, bootAutorun) {
  if (mode === 'ui') return `${bootAutorun ? '$sim_boot_autorun = true\n' : ''}${ruby.boot}\n${ruby.ui}`;
  return `${ruby.boot}
${ruby.script}
begin
${user}
rescue Exception => e
  puts "Script error: #{e.class}: #{e.message}"
end
${ruby.tail}`;
}

export class MidoriDevice {
  static async start(opts) {
    const board = BOARDS[opts.board] ? opts.board : DEFAULT_BOARD;
    const id = await loadBuild();
    const [factory, ruby] = await Promise.all([loadFactory(board, id), loadRuby(board, id)]);
    const dev = new MidoriDevice(opts);
    dev.board = board;
    dev.buildId = id;
    dev.ruby = ruby;
    await dev._boot(factory);
    return dev;
  }

  // files: [{ path, data }] put in the device's FS before Ruby starts (the
  // connected MIDoRI's /plugins, /sd/plugins and /app.rb: the start-up reads
  // them as on the device); bootAutorun: UI mode runs /app.rb (the first
  // start, as after power-up), through onRequestScript(path)
  // scripts: the names the Scripts screen lists (the connected MIDoRI's
  // /sd/*.rb), or null for none; scriptPath: the running script's path (its
  // [Running] badge); onSdRefresh: the Scripts screen's reload
  constructor({ canvas, script, mode, onScriptEnd, onOutput, onMidiOut, webMidiPorts, onWebMidiOut, amyAudio,
                files, bootAutorun, onRequestScript, scripts, scriptPath, onSdRefresh }) {
    this.files = files || [];
    this.scripts = scripts || [];
    this.scriptPath = scriptPath || '';
    this.onSdRefresh = onSdRefresh || (() => {});
    this.bootAutorun = !!bootAutorun;
    this.onRequestScript = onRequestScript || (() => {});
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.script = script || '';
    this.mode = mode === 'ui' ? 'ui' : 'script';
    this.onScriptEnd = onScriptEnd || (() => {});   // ('end' | 'stop') -> the page starts UI mode
    this.onOutput = onOutput || (s => console.log(s));
    this.onMidiOut = onMidiOut || (() => {});
    this.webMidiPorts = webMidiPorts || [];          // [{ slot, name, input, output, connected }]
    this.onWebMidiOut = onWebMidiOut || (() => {});
    this.amyAudio = amyAudio || null;               // AmyAudio (amy_audio.js), or none
    this.running = false;
    this.fingers = new Map();     // pointerId -> { id, x, y }
    this.pinned = new Map();      // Alt+click fingers that stay down
    this._handlers = [];
  }

  async _boot(factory) {
    const M = await factory({
      locateFile: name => wasmUrl(name, this.buildId),
      print: s => this.onOutput(s, 'out'),
      printErr: s => this.onOutput(s, 'err'),
    });
    this.M = M;
    this.width = M._sim_screen_width();
    this.height = M._sim_screen_height();
    this.canvas.width = this.width;
    this.canvas.height = this.height;
    this.image = this.ctx.createImageData(this.width, this.height);

    M.simPresent = (ptr, stride, x, y, w, h) => this._present(ptr, stride, x, y, w, h);
    M.simMidiOut = (role, cin, b1, b2, b3, timeUs) => this.onMidiOut(role, cin, b1, b2, b3, timeUs);
    M.amyOut = (ptr, frames) => { if (this.amyAudio) this.amyAudio.push(M, ptr, frames); };
    if (this.amyAudio) this.amyAudio.reset();
    M.webMidiOut = (slot, cin, b1, b2, b3, timeUs) => this.onWebMidiOut(slot, cin, b1, b2, b3, timeUs);
    // The Scripts screen's Stop: back to UI mode, as on the device
    M.simStopScript = () => { if (this.mode === 'script') this.onScriptEnd('stop'); };
    // The Scripts screen: run a script (the page fetches it), reload the list
    M.simRequestScript = path => { if (this.running) this.onRequestScript(String(path)); };
    M.simRequestSdRefresh = () => { if (this.running) this.onSdRefresh(); };

    this._touchBuf = M._malloc(MAX_FINGERS * 12);
    M._sim_boot();
    // Before Ruby starts: its start-up lists these on the MIDI Devices screen
    for (const p of this.webMidiPorts) {
      M.ccall('web_midi_port', null, ['number', 'string', 'number', 'number'],
              [p.slot, p.name, p.input ? 1 : 0, p.output ? 1 : 0]);
      M._web_midi_connected(p.slot, p.connected ? 1 : 0);
    }
    this.setScripts(this.scripts);
    if (this.scriptPath) M.ccall('sim_set_current_script', null, ['string'], [this.scriptPath]);
    for (const f of this.files) {
      try {
        M.FS.mkdirTree(f.path.slice(0, f.path.lastIndexOf('/')) || '/');
        M.FS.writeFile(f.path, f.data);
      } catch (e) { this.onOutput(`[sim] ${f.path}: ${e.message}`, 'err'); }
    }
    if (M.ccall('picorb_init', 'number', [], []) !== 0) {
      this.onOutput('[sim] PicoRuby failed to start', 'err');
      return;
    }
    M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'],
            [vmSource(this.ruby, this.mode, this.script, this.bootAutorun), 'script.rb']);
    // Ruby's sim_script_ended / sim_request_script (sim/mrblib/sim_boot.rb) call these
    globalThis.__midoriSim = {
      scriptEnded: () => { if (this.running) this.onScriptEnd('end'); },
      requestScript: path => { if (this.running) this.onRequestScript(String(path)); },
    };
    this._attachPointer();
    this.running = true;
    this._lastTick = performance.now();
    this._runRuby();
    this._frame();
  }

  stop() {
    this.running = false;
    for (const [type, fn, target] of this._handlers) target.removeEventListener(type, fn);
    this._handlers = [];
    if (this._raf) cancelAnimationFrame(this._raf);
    clearTimeout(this._timer);
    this.M = null;   // the instance and its heap go with it
  }

  // Midi from the page into the device: role 0 = USB-MIDI Host, 1 = USB-MIDI Device
  midiIn(role, bytes) {
    if (!this.M) return;
    for (const p of toUsbPackets(bytes)) this.M._sim_midi_in(role, p[0], p[1], p[2], p[3]);
  }

  // Midi from a named Web MIDI port (MIDIDevices.web_midi)
  webMidiIn(slot, bytes) {
    if (!this.M) return;
    for (const p of toUsbPackets(bytes)) this.M._web_midi_in(slot, p[0], p[1], p[2], p[3]);
  }

  setWebMidiConnected(slot, on) {
    if (this.M) this.M._web_midi_connected(slot, on ? 1 : 0);
  }

  // The Scripts screen's list (names under /sd); it redraws when it changes
  setScripts(names) {
    if (!this.M) return;
    this.scripts = names || [];
    this.M._sim_scripts_clear();
    for (const n of this.scripts) this.M.ccall('sim_scripts_add', null, ['string'], [n]);
    this.M._sim_scripts_done();
  }

  setConnected(role, on) {
    if (this.M) this.M._sim_midi_connect(role, on ? 1 : 0);
  }

  // ---- loops

  _runRuby() {
    if (!this.running) return;
    const M = this.M;
    const now = performance.now();
    let ticks = 0;
    while (now - this._lastTick >= MRB_TICK_UNIT && ticks < MAX_CATCHUP_TICKS) {
      M._mrb_tick_wasm();
      this._lastTick += MRB_TICK_UNIT;
      ticks++;
    }
    if (now - this._lastTick >= MRB_TICK_UNIT) this._lastTick = now;
    const start = performance.now();
    let progressed = false;
    while (performance.now() - start < RUBY_SLICE_MS) {
      const status = M._mrb_run_step_status();
      if (status <= 0) break;
      progressed = true;
    }
    // Timers, the sequencer and MIDI input run between Ruby's turns too
    M._sim_rtos_poll();
    // AMY renders here too, to keep its audio queued ahead
    if (this.amyAudio) this.amyAudio.pump(M);
    const idle = !progressed && M._mrb_gc_scheduler_pending_wasm() !== 1;
    this._timer = setTimeout(() => this._runRuby(), idle ? 2 : 0);
  }

  _frame() {
    if (!this.running) return;
    this._pushTouches();
    this.M._sim_frame();
    this._raf = requestAnimationFrame(() => this._frame());
  }

  // ---- display

  _present(ptr, stride, x, y, w, h) {
    const src = this.M.HEAPU16;
    const dst = this.image.data;
    const base = ptr >> 1;
    for (let j = y; j < y + h; j++) {
      let s = base + j * stride + x;
      let d = (j * this.width + x) * 4;
      for (let i = 0; i < w; i++, s++, d += 4) {
        // LovyanGFX keeps RGB565 byte-swapped (swap565_t)
        const v = ((src[s] & 0xff) << 8) | (src[s] >> 8);
        const r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        dst[d] = (r << 3) | (r >> 2);
        dst[d + 1] = (g << 2) | (g >> 4);
        dst[d + 2] = (b << 3) | (b >> 2);
        dst[d + 3] = 255;
      }
    }
    this.ctx.putImageData(this.image, 0, 0, x, y, w, h);
  }

  // ---- touch

  _toScreen(ev) {
    const r = this.canvas.getBoundingClientRect();
    return {
      x: Math.max(0, Math.min(this.width - 1, Math.round((ev.clientX - r.left) * this.width / r.width))),
      y: Math.max(0, Math.min(this.height - 1, Math.round((ev.clientY - r.top) * this.height / r.height))),
    };
  }

  _freeId() {
    const used = new Set([...this.fingers.values(), ...this.pinned.values()].map(f => f.id));
    for (let i = 0; i < MAX_FINGERS; i++) if (!used.has(i)) return i;
    return -1;
  }

  _attachPointer() {
    const c = this.canvas;
    const on = (type, fn, target = c) => { target.addEventListener(type, fn); this._handlers.push([type, fn, target]); };
    on('pointerdown', ev => {
      ev.preventDefault();
      const p = this._toScreen(ev);
      // Alt+click: a finger that stays down (to try several fingers with a mouse);
      // Alt+click on a pinned finger lifts it
      if (ev.altKey) {
        for (const [k, f] of this.pinned) {
          if (Math.hypot(f.x - p.x, f.y - p.y) < 24) { this.pinned.delete(k); return; }
        }
        const id = this._freeId();
        if (id >= 0) this.pinned.set(`pin${performance.now()}`, { id, ...p });
        return;
      }
      const id = this._freeId();
      if (id < 0) return;
      c.setPointerCapture(ev.pointerId);
      this.fingers.set(ev.pointerId, { id, ...p });
    });
    on('pointermove', ev => {
      const f = this.fingers.get(ev.pointerId);
      if (!f) return;
      Object.assign(f, this._toScreen(ev));
    });
    const up = ev => { this.fingers.delete(ev.pointerId); };
    on('pointerup', up);
    on('pointercancel', up);
    on('contextmenu', ev => ev.preventDefault());
  }

  _pushTouches() {
    const all = [...this.pinned.values(), ...this.fingers.values()].slice(0, MAX_FINGERS);
    const h = this.M.HEAP32, b = this._touchBuf >> 2;
    all.forEach((f, i) => { h[b + i * 3] = f.id; h[b + i * 3 + 1] = f.x; h[b + i * 3 + 2] = f.y; });
    this.M._sim_touch(this._touchBuf, all.length);
  }

  get pinnedFingers() { return [...this.pinned.values()]; }
}

// The packet helpers live in usb_midi.js (SysEx included); kept importable
// from here for the page
export { toUsbPackets, UsbMidiAssembler } from './usb_midi.js';
