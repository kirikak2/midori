// Node test of the simulator's Scripts screen fed with a connected MIDoRI's
// list (device.js setScripts -> sim_scripts_*): it lists them, and picking
// one asks the page for "/sd/<name>" (simRequestScript), as the device's
// Supervisor would be asked.
//   node sim/test/node_scripts.mjs [shot.png] [tap x,y ...]
import fs from 'fs';
import { execFileSync } from 'child_process';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const [shot, ...taps] = process.argv.slice(2);
const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
globalThis.__midoriSim = { scriptEnded() {}, requestScript() {} };
const M = await createMidoriSim({ print: s => { if (!/^[IDW] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: () => {} });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
const requests = [];
M.simRequestScript = p => requests.push(p);
M.simRequestSdRefresh = () => requests.push("(refresh)");
M._sim_boot();
M._sim_scripts_clear();
for (const n of ['amy_fm.rb', 'keys.rb', 'tombola.rb']) M.ccall('sim_scripts_add', null, ['string'], [n]);
M._sim_scripts_done();
M.ccall('picorb_init', 'number', [], []);
const vm = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_ui.rb').replace('puts "UI Mode', 'UI.show(:scripts)\nputs "UI Mode')}`;
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, 'boot.rb']);
let last = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    await new Promise(res => setTimeout(res, 4));
  }
}
const tp = M._malloc(12);
const tap = async (x, y) => { M.HEAP32[tp >> 2] = 0; M.HEAP32[(tp >> 2) + 1] = x; M.HEAP32[(tp >> 2) + 2] = y; M._sim_touch(tp, 1); await run(120); M._sim_touch(tp, 0); await run(300); };
await run(1200);
for (const t of taps) { const [x, y] = t.split(',').map(Number); await tap(x, y); }
console.log('requests:', JSON.stringify(requests));
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
