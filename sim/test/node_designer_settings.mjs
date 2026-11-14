// Node test of the designer's finer settings: a knob's range / step /
// sensitivity, Keys colours (a preset), XYPad per-finger overrides and
// Tempo's BPM at start; a knob block reports what the device has.
//   node sim/test/node_designer_settings.mjs [shot.png]
import { newProject, newWidget, presetNotes, upgrade } from '../web/designer/model.js';
import { generate } from '../web/designer/codegen.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const shot = process.argv[2];

const p = newProject(BOARD);
p.name = 'Settings';
const l = p.layouts[0];
const k = newWidget('knob', 0, 0, 1, 1);
Object.assign(k.props, { label: 'Detune', min: -12, max: 12, step: 0.5, value: 3.5, sensitivity: 0.5 });
k.action = { type: 'ruby', body: [
  'puts "knob v=#{v}"',
  'puts "bpm=#{UI::Tempo.bpm.to_i}"',
  'puts "slot5=#{Settings.xypad.slot(5).inspect}"',
  'puts "key 60 color=#{Settings.keys.color(60).inspect} key 61=#{Settings.keys.color(61).inspect}"',
].join('\n') };
l.widgets.push(k);
const tp = newWidget('tempo', 1, 0, 2, 1); tp.props.bpm = 96; l.widgets.push(tp);
const kb = newWidget('keys', 0, 1, 4, 1); kb.props.low = 48; kb.props.octaves = 2;
kb.props.colors = [{ color: 'blue', notes: presetNotes('Major scale', 0, 48, 2) }];
l.widgets.push(kb);
const xy = newWidget('xypad', 0, 2, 2, 1);
xy.props.slots = [{ slot: 5, x_mode: 'cc', x_cc: 70, y_cc: 71, note: 60, channel: 3, hold: true, device: '' }];
l.widgets.push(xy);

// An old project without the new settings opens with their defaults
const old = JSON.parse(JSON.stringify(p));
delete old.layouts[0].widgets[0].props.sensitivity; delete old.layouts[0].widgets[2].props.colors;
upgrade(old);
console.log('upgrade fills:', old.layouts[0].widgets[0].props.sensitivity === 1 && Array.isArray(old.layouts[0].widgets[2].props.colors) ? 'ok' : 'NG');

const src = generate(p);
fs.writeFileSync(new URL('../out/designer_settings.rb', import.meta.url), src);
console.log(src.split('\n').filter(x => /UI\.layout|knob area|set_color|pad\.slot|Tempo\.bpm|UI::Keys\.new/.test(x)).map(x => x.trim()).join('\n'));

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const vm = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_script.rb')}\nbegin\n${src}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
globalThis.__midoriSim = { scriptEnded() {}, requestScript() {} };
const M = await createMidoriSim({ print: s => { if (!/^[IDW] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: () => {} });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
M.simMidiOut = () => {};
M._sim_boot(); M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, 'script.rb']);
let last = performance.now();
const end = performance.now() + 1500;
while (performance.now() < end) {
  const now = performance.now();
  while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
  for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
  M._sim_rtos_poll(); M._sim_frame();
  await new Promise(res => setTimeout(res, 4));
}
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
