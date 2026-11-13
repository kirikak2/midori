// Node test of the device's modes: UI mode (Home with the AMY tile, as the
// Tab5), a script ending (the hook the page uses to go back to UI mode).
//   node sim/test/node_modes.mjs [ui|script] [screenshot.png] [x,y ...]
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const [mode = 'ui', shot, ...taps] = process.argv.slice(2);
const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const user = 'puts "hello from the script"';
const src = mode === 'ui' ? `${r('sim_boot-tab5.rb')}\n${r('sim_ui.rb')}`
  : `${r('sim_boot-tab5.rb')}\n${r('sim_script.rb')}\nbegin\n${user}\nrescue Exception => e\n  puts "Script error: #{e.message}"\nend\n${r('sim_tail.rb')}`;
let ended = 0;
globalThis.__midoriSim = { scriptEnded: () => { ended++; } };
const M = await createMidoriSim({ print: s => { if (!/^[ID] \(/.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
let amyPeak = 0;
M.amyOut = (ptr, n) => { for (const v of M.HEAP16.subarray(ptr >> 1, (ptr >> 1) + n * 2)) amyPeak = Math.max(amyPeak, Math.abs(v)); };
M._sim_boot();
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);
const sleep = ms => new Promise(res => setTimeout(res, ms));
let last = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); if (M._sim_amy_running()) M._sim_amy_render(2); M._sim_frame();
    await sleep(4);
  }
}
const tp = M._malloc(12);
await run(600);
for (const t of taps) {
  const [x, y] = t.split(',').map(Number);
  M.HEAP32[tp >> 2] = 0; M.HEAP32[(tp >> 2) + 1] = x; M.HEAP32[(tp >> 2) + 2] = y;
  M._sim_touch(tp, 1); await run(120); M._sim_touch(tp, 0); await run(600);
}
console.log('script ended hook:', ended, ' amy peak:', amyPeak);
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
