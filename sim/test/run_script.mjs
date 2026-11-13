// Runs a Ruby script in the simulator under Node, as the page does, taps
// the given points and saves a screenshot.
//   node sim/test/run_script.mjs script.rb out.png [x,y ...]
// PORTS='Keystation:i,IAC Driver Bus 1:io' adds fake Web MIDI ports.
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const [scriptPath, shot, ...taps] = process.argv.slice(2);
const wasm = new URL('../web/wasm/', import.meta.url);
const boot = fs.readFileSync(new URL('sim_boot-tab5.rb', wasm), 'utf8');
const tail = fs.readFileSync(new URL('sim_tail.rb', wasm), 'utf8');
const user = fs.readFileSync(scriptPath, 'utf8');
const src = `${boot}\nbegin\n${user}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${tail}`;

const M = await createMidoriSim({
  print: s => { if (!/^[ID] \(/.test(s)) console.log('[out]', s); },
  printErr: s => console.log('[err]', s),
});
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => {
  const b = ptr >> 1;
  for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x);
};
const midi = [];
M.simMidiOut = (role, cin, b1, b2, b3) => midi.push([role, b1.toString(16), b2, b3].join(' '));
M._sim_boot();
(process.env.PORTS || '').split(',').filter(Boolean).forEach((e, slot) => {
  const [name, io = 'io'] = e.split(':');
  M.ccall('web_midi_port', null, ['number', 'string', 'number', 'number'], [slot, name, io.includes('i') ? 1 : 0, io.includes('o') ? 1 : 0]);
  M._web_midi_connected(slot, 1);
});
let amyFrames = 0, amyPeak = 0;
M.amyOut = (ptr, n) => { const a = M.HEAP16.subarray(ptr >> 1, (ptr >> 1) + n * 2); for (const v of a) amyPeak = Math.max(amyPeak, Math.abs(v)); amyFrames += n; };
M.webMidiOut = (slot, cin, b1, b2, b3) => midi.push(['port' + slot, b1.toString(16), b2, b3].join(' '));
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);

const sleep = ms => new Promise(r => setTimeout(r, ms));
let lastTick = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - lastTick >= 4) { M._mrb_tick_wasm(); lastTick += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll();
    if (M._sim_amy_running()) M._sim_amy_render(2);
    M._sim_frame();
    await sleep(5);
  }
}
const tp = M._malloc(12);
await run(500);
for (const t of taps) {
  const [x, y] = t.split(',').map(Number);
  M.HEAP32[tp >> 2] = 0; M.HEAP32[(tp >> 2) + 1] = x; M.HEAP32[(tp >> 2) + 2] = y;
  M._sim_touch(tp, 1); await run(120); M._sim_touch(tp, 0); await run(300);
}
console.log('midi out:', midi.length, midi.slice(0, 8));
console.log('amy frames:', amyFrames, 'peak:', amyPeak);
const rgb = Buffer.alloc(W * H * 3);
for (let i = 0; i < W * H; i++) {
  let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8);
  rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31;
}
fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
fs.unlinkSync(shot + '.ppm');
process.exit(0);
