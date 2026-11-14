// Node test of the reference's "open in the simulator" links
// (tools/site_sim_links.rb): every linked code card runs in the simulator
// (Tab5) without an error.
//   node sim/test/node_reference.mjs
import fs from 'fs';
import os from 'os';
import path from 'path';
import { execFileSync } from 'child_process';

const root = new URL('../..', import.meta.url).pathname;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'midori-ref-'));
execFileSync('ruby', [path.join(root, 'tools/site_sim_links.rb'), '--dump', dir]);
const { default: createMidoriSim } = await import('../web/wasm/midori-sim-tab5.js');
const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
let failed = 0;
for (const f of fs.readdirSync(dir).sort()) {
  const src = fs.readFileSync(path.join(dir, f), 'utf8');
  const out = [];
  globalThis.__midoriSim = { scriptEnded() {}, requestScript() {} };
  const M = await createMidoriSim({ print: s => out.push(s), printErr: () => {} });
  M.simPresent = () => {}; M.simMidiOut = () => {}; M.amyOut = () => {};
  M._sim_boot(); M.ccall('picorb_init', 'number', [], []);
  const vm = `${r('sim_boot-tab5.rb')}\n${r('sim_script.rb')}\nbegin\n${src}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
  M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, f]);
  let last = performance.now();
  const end = performance.now() + 700;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    await new Promise(res => setTimeout(res, 4));
  }
  const err = out.find(l => /Script error|Error in|undefined method/.test(l));
  console.log(`${err ? 'FAIL' : 'ok  '} ${f}${err ? `: ${err}` : ''}`);
  if (err) failed++;
}
fs.rmSync(dir, { recursive: true });
process.exit(failed ? 1 : 0);
