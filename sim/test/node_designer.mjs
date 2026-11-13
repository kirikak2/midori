// Node test of the designer's code generation: build a project, generate
// the project script, check the model round-trips through its header, run
// it in the simulator, tap, and save a screenshot.
//   node sim/test/node_designer.mjs [shot.png]
import { newProject, newWidget, newLayout, problems } from '../web/designer/model.js';
import { generate, decodeModel, moduleName, fileName } from '../web/designer/codegen.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const shot = process.argv[2];

const p = newProject(BOARD);
p.name = 'Test Stage #1 "quotes"';
const l = p.layouts[0];
const k = newWidget('knob', 0, 0, 1, 1); k.props.label = 'Cutoff'; l.widgets.push(k);
const pad = newWidget('pad', 1, 0, 1, 1); pad.props.label = 'Kick #{x}'; pad.props.color = 'red';
pad.action.note = 36; pad.action.channel = 9; l.widgets.push(pad);
const pad2 = newWidget('pad', 2, 0, 1, 1); pad2.props.type = 'momentary'; pad2.props.label = 'Hold'; l.widgets.push(pad2);
const rb = newWidget('pad', 3, 0, 1, 1); rb.action = { type: 'ruby', body: 'puts "ruby pad"' }; rb.props.label = 'Ruby'; l.widgets.push(rb);
l.widgets.push(newWidget('keys', 0, 1, 4, 2));
const l2 = newLayout('XY'); l2.cols = 2; l2.rows = 1;
l2.widgets.push(newWidget('xypad', 0, 0, 1, 1)); l2.widgets.push(newWidget('tempo', 1, 0, 1, 1));
p.layouts.push(l2);

console.log('module', moduleName(p.name), 'file', fileName(p.name), 'problems', JSON.stringify(problems(p)));
const src = generate(p);
const back = decodeModel(src);
console.log('round trip:', JSON.stringify(back) === JSON.stringify(p) ? 'ok' : 'DIFFERENT');
fs.writeFileSync(new URL('../out/designer_gen.rb', import.meta.url), src);

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const vm = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_script.rb')}\nbegin\n${src}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
globalThis.__midoriSim = { scriptEnded() {} };
const M = await createMidoriSim({ print: s => { if (!/^[ID] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
const midi = [];
M.simMidiOut = (role, cin, b1, b2, b3) => midi.push(`${role} ${b1.toString(16)} ${b2} ${b3}`);
M._sim_boot();
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, 'script.rb']);
const sleep = ms => new Promise(res => setTimeout(res, ms));
let last = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    await sleep(4);
  }
}
const tp = M._malloc(12);
const tap = async (x, y) => { M.HEAP32[tp >> 2] = 0; M.HEAP32[(tp >> 2) + 1] = x; M.HEAP32[(tp >> 2) + 2] = y; M._sim_touch(tp, 1); await run(120); M._sim_touch(tp, 0); await run(300); };
await run(600);
const cw = W / 4;
await tap(Math.round(cw * 1.5), 120);   // Kick
await tap(Math.round(cw * 3.5), 120);   // Ruby
await tap(Math.round(cw * 2.5), 120);   // Hold (momentary)
console.log('midi:', midi.join(' | '));
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
