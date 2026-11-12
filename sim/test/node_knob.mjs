// Node smoke test: Ruby defines a knob, a drag turns it, the block sees it.
//   node sim/test/node_knob.mjs
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import fs from 'fs';

const out = [];
const M = await createMidoriSim({ print: s => { out.push(s); console.log('[rb]', s); }, printErr: s => console.log('[err]', s) });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, width, x, y, w, h) => {
  const base = ptr >> 1;
  for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(base + j * width + x, base + j * width + x + w), j * W + x);
};
M.simMidiOut = (role, cin, b1, b2, b3) => console.log('[midi out]', role, cin.toString(16), b1.toString(16), b2, b3);

M._sim_boot();
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task', 'number', ['string'], [`
UI._loading(nil, 0, 0)
dev = MIDI::Device.new(0x02) rescue nil
UI.knob(1, label: "Cutoff", color: :cyan, value: 64) do |v|
  puts "knob #{v.to_i}"
end
UI.show(:knobs)
loop do
  UI.process
  MIDI.sleep_ms 10
end
`]);

const sleep = ms => new Promise(r => setTimeout(r, ms));
let lastTick = performance.now();
async function run(ms, touch) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - lastTick >= 4) { M._mrb_tick_wasm(); lastTick += 4; }
    for (let i = 0; i < 50; i++) if (M._mrb_run_step_status() <= 0) break;
    if (touch !== undefined) touch();
    M._sim_frame();
    await sleep(8);
  }
}
const ptr = M._malloc(12);
function finger(x, y) { M.HEAP32[ptr >> 2] = 0; M.HEAP32[(ptr >> 2) + 1] = x; M.HEAP32[(ptr >> 2) + 2] = y; M._sim_touch(ptr, 1); }
await run(300);
// Knob 1's centre on Tab5 is around (160, 136): drag clockwise around it
const cx = 160, cy = 136, r = 60;
for (let a = -90; a <= 60; a += 10) {
  const t = a * Math.PI / 180;
  await run(30, () => finger(Math.round(cx + r * Math.cos(t)), Math.round(cy + r * Math.sin(t))));
}
M._sim_touch(ptr, 0);
await run(200);
const rgb = Buffer.alloc(W * H * 3);
for (let i = 0; i < W * H; i++) {
  let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8);
  rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31;
}
fs.writeFileSync(new URL('../out/knob.ppm', import.meta.url), Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
console.log('knob lines:', out.filter(s => s.startsWith('knob')).length);
process.exit(0);
