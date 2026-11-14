// Node test of the designer's Pads / Knobs screens, routes and Home's menu:
// generate a project with pads and knobs (two banks), MIDI routes and hidden
// menu tiles, run it, feed USB-MIDI host input, tap a pad, check MIDI.
//   node sim/test/node_designer_screens.mjs [shot.png]
import { newProject, newSlot, problems } from '../web/designer/model.js';
import { generate, decodeModel } from '../web/designer/codegen.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const shot = process.argv[2];

const p = newProject(BOARD);
p.name = 'Screens';
p.devices.push({ id: 'kb', kind: 'usb_midi_host' });
// Pad 5 (the second row's first): a note on the synth, channel 9
const pad = newSlot('pad', 4); pad.props.label = 'Kick'; pad.props.color = 'red';
pad.action.note = 36; pad.action.channel = 9; pad.action.device = 'synth';
p.pads.push(pad);
// Knob 1 in bank A: CC 74; knob 2 in bank B: a Ruby block that reports the menu
const k1 = newSlot('knob', 0, 0); k1.action.cc = 74; k1.props.value = 40; p.knobs.push(k1);
const k2 = newSlot('knob', 1, 1); k2.action = { type: 'ruby', body: 'puts "knob B2 v=#{v.to_i} hidden=#{UI._menu_hidden}"' };
p.knobs.push(k2);
// Routes: the USB host's channel 1 to the synth; the clock to the synth
p.routes.push({ from: 'kb', to: 'synth', channel: 0 });
p.routes.push({ from: 'clock', to: 'synth', channel: 'all' });
p.menu.hide = ['settings', 'logs'];
p.start = 'pads';

console.log('problems', JSON.stringify(problems(p)));
const src = generate(p);
console.log('round trip:', JSON.stringify(decodeModel(src)) === JSON.stringify(p) ? 'ok' : 'DIFFERENT');
fs.writeFileSync(new URL('../out/designer_screens.rb', import.meta.url), src);

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const vm = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_script.rb')}\nbegin\n${src}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
globalThis.__midoriSim = { scriptEnded() {} };
const M = await createMidoriSim({ print: s => { if (!/^[ID] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
const midi = [];
M.simMidiOut = (role, cin, b1, b2, b3) => { if (b1 !== 0xf8) midi.push(`${role} ${b1.toString(16)} ${b2} ${b3}`); };
M._sim_boot();
M._sim_midi_connect(0, 1);
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
await run(800);
console.log('start midi:', midi.splice(0).join(' | '));
// From the USB host: channel 1 passes, channel 2 does not
M._sim_midi_in(0, 0x09, 0x90, 60, 100);
M._sim_midi_in(0, 0x09, 0x91, 61, 100);
await run(200);
console.log('routed:', midi.splice(0).join(' | '));
await tap(Math.round(W / 8), Math.round(H * 0.5));   // pad 5
console.log('pad:', midi.splice(0).join(' | '));
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
