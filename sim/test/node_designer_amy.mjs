// Node test of the designer's AMY parts, Envelope and Sequencer (Tab5):
// generate, run, check the envelopes reached the parts, screenshot.
//   node sim/test/node_designer_amy.mjs [shot.png]
import { newProject, newWidget, newLayout, problems } from '../web/designer/model.js';
import { generate, moduleName } from '../web/designer/codegen.js';
import fs from 'fs';
import { execFileSync } from 'child_process';

const { default: createMidoriSim } = await import('../web/wasm/midori-sim-tab5.js');
const shot = process.argv[2];

const p = newProject('tab5');
p.name = 'AMY Stage';
p.devices.push({ id: 'amy', kind: 'amy' });
p.parts.push({ id: 'bass', type: 'synth', channel: 0, voices: 2, patch: 3 });
p.parts.push({ id: 'bell', type: 'fm', channel: 1, voices: 4, patch: 140 });
const l = p.layouts[0];
const e1 = newWidget('envelope', 0, 0, 2, 1); Object.assign(e1.props, { label: 'Bass amp', to: 'bass', stage: 'amp', attack: 20, release: 300 });
const e2 = newWidget('envelope', 2, 0, 2, 1); Object.assign(e2.props, { label: 'Bell op2', to: 'bell', stage: 'op2' });
e2.action = { type: 'ruby', body: 'puts "env #{e[:attack]}"' };
l.widgets.push(e1, e2);
const k = newWidget('keys', 0, 1, 4, 2); Object.assign(k.props, { device: 'amy', channel: 0 }); l.widgets.push(k);
const l2 = newLayout('Song'); l2.widgets.push(newWidget('sequencer', 0, 0, 4, 3)); p.layouts.push(l2);
console.log('problems', JSON.stringify(problems(p)));
const src = generate(p, { show: Number(process.env.SHOW || 0) }) + `\n`;
fs.writeFileSync(new URL('../out/designer_amy.rb', import.meta.url), src);
// After setup, report what the parts got (appended before the run loop)
const MOD = moduleName(p.name);
const probe = src.replace(new RegExp(`^${MOD}\\.run$`, 'm'), `puts "bass amp attack=#{${MOD}.bass.amp_attack} release=#{${MOD}.bass.amp_release}"\nputs "bell op2 attack=#{${MOD}.bell.op(2).attack}"\n${MOD}.run`);

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const vm = `${r('sim_boot-tab5.rb')}\n${r('sim_script.rb')}\nbegin\n${probe}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
globalThis.__midoriSim = { scriptEnded() {} };
const M = await createMidoriSim({ print: s => { if (!/^[ID] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
const W = M._sim_screen_width(), H = M._sim_screen_height();
const fb = new Uint16Array(W * H);
M.simPresent = (ptr, stride, x, y, w, h) => { const b = ptr >> 1; for (let j = y; j < y + h; j++) fb.set(M.HEAPU16.subarray(b + j * stride + x, b + j * stride + x + w), j * W + x); };
let peak = 0;
M.amyOut = (ptr, n) => { for (const v of M.HEAP16.subarray(ptr >> 1, (ptr >> 1) + n * 2)) peak = Math.max(peak, Math.abs(v)); };
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
    M._sim_rtos_poll(); if (M._sim_amy_running()) M._sim_amy_render(2); M._sim_frame();
    await sleep(4);
  }
}
const tp = M._malloc(12);
await run(800);
M.HEAP32[tp >> 2] = 0; M.HEAP32[(tp >> 2) + 1] = 60; M.HEAP32[(tp >> 2) + 2] = 600;   // a key: bass on channel 0
M._sim_touch(tp, 1); await run(200); M._sim_touch(tp, 0); await run(300);
console.log('amy peak', peak);
if (shot) {
  const rgb = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) { let v = fb[i]; v = ((v & 0xff) << 8) | (v >> 8); rgb[i*3] = ((v >> 11) & 31) * 255 / 31; rgb[i*3+1] = ((v >> 5) & 63) * 255 / 63; rgb[i*3+2] = (v & 31) * 255 / 31; }
  fs.writeFileSync(shot + '.ppm', Buffer.concat([Buffer.from(`P6 ${W} ${H} 255\n`), rgb]));
  execFileSync('python3', ['-c', `from PIL import Image; Image.open('${shot}.ppm').save('${shot}')`]);
  fs.unlinkSync(shot + '.ppm');
}
process.exit(0);
