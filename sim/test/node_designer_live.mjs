// Node test of the designer's live patching (§6.3): the preview keeps
// running while a change is patched in. A knob turned on the device keeps
// its value across a patch unless its starting value changes; a pad's new
// note plays at once; live_values hands the knobs' values to the page.
//   node sim/test/node_designer_live.mjs
import { newProject, newWidget, newSlot } from '../web/designer/model.js';
import { generate, structureKey, moduleName } from '../web/designer/codegen.js';
import fs from 'fs';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const p = newProject(BOARD);
p.name = 'Live';
const l = p.layouts[0];
const knob = newWidget('knob', 0, 0, 1, 1); knob.action.cc = 74; l.widgets.push(knob);
const pad = newWidget('pad', 1, 0, 1, 1); pad.props.label = 'Kick'; pad.action.note = 36; pad.action.device = 'synth'; l.widgets.push(pad);
const sk = newSlot('knob', 0, 0); sk.action.cc = 71; sk.props.value = 10; p.knobs.push(sk);
p.sendInitial = false;
const M = moduleName(p.name);

const queue = [], failures = [];
let values = null;
globalThis.__midoriSim = {
  scriptEnded() {}, requestScript() {},
  takeLive: () => queue.shift() ?? null,
  liveFailed: m => failures.push(String(m)),
  liveValues: j => { values = JSON.parse(String(j)); },
};
const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const src = generate(p, { show: 0, preview: true });
const vm = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_script.rb')}\nbegin\n${src}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
const S = await createMidoriSim({ print: s => { if (!/^[IDW] \(|initialized/.test(s)) console.log('[out]', s); }, printErr: () => {} });
const W = S._sim_screen_width();
S.simPresent = () => {};
const midi = [];
S.simMidiOut = (role, cin, b1, b2, b3) => midi.push(`${b1.toString(16)} ${b2} ${b3}`);
S._sim_boot(); S.ccall('picorb_init', 'number', [], []);
S.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, 'script.rb']);
let last = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { S._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (S._mrb_run_step_status() <= 0) break;
    S._sim_rtos_poll(); S._sim_frame();
    await new Promise(res => setTimeout(res, 4));
  }
}
const tp = S._malloc(12);
const tap = async (x, y) => { S.HEAP32[tp >> 2] = 0; S.HEAP32[(tp >> 2) + 1] = x; S.HEAP32[(tp >> 2) + 2] = y; S._sim_touch(tp, 1); await run(120); S._sim_touch(tp, 0); await run(300); };
const ok = (c, w) => { console.log(`${c ? 'ok  ' : 'FAIL'} ${w}`); if (!c) process.exitCode = 1; };
const getValues = async () => { values = null; queue.push(`${M}.live_values`); await run(200); return values; };

await run(800);
// "Turn" both knobs on the device (as a finger would)
queue.push(`${M}.instance_variable_get(:@layouts)[0][:${knob.id}].set_value(100, notify: false); UI.knob_set(1, 99, bank: 1, notify: false)`);
await run(200);
let v = await getValues();
ok(v && v[knob.id] === 100 && v[sk.id] === 99, `live_values: ${JSON.stringify(v)}`);

// A patch: the pad plays another note, the knob another CC
const snap = structuredClone(p);
pad.props.label = 'Snare'; pad.action.note = 38; knob.action.cc = 75;
ok(structureKey(p) === structureKey(snap), 'the change keeps the structure (patched, not restarted)');
queue.push(generate(p, { live: { fresh: [] }, show: 0 }));
await run(300);
midi.length = 0;
await tap(Math.round(W / 4 * 1.5), 120);
ok(midi.includes('90 38 110') && !midi.some(m => / 36 /.test(m)), `the patched pad plays note 38 (not 36): ${midi.join(' | ')}`);
v = await getValues();
ok(v && v[knob.id] === 100 && v[sk.id] === 99, `knob values kept: ${JSON.stringify(v)}`);

// A new starting value: that knob takes it
knob.props.value = 20;
queue.push(generate(p, { live: { fresh: [knob.id] }, show: 0 }));
await run(300);
v = await getValues();
ok(v && v[knob.id] === 20 && v[sk.id] === 99, `a fresh knob takes its new value: ${JSON.stringify(v)}`);

// A mistake in the patch is reported (the page restarts the preview)
queue.push(`${M}.no_such_method`);
await run(200);
ok(failures.length === 1 && /NoMethodError/.test(failures[0]), `an error is reported: ${failures[0]}`);
process.exit(process.exitCode || 0);
