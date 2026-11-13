// Node test for AMY in the simulator: start it from Ruby, play FM notes,
// render blocks as the page would and check there is sound; time the render.
//   node sim/test/node_amy.mjs
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import fs from 'fs';

const wasm = new URL('../web/wasm/', import.meta.url);
const boot = fs.readFileSync(new URL('sim_boot-tab5.rb', wasm), 'utf8');
const tail = fs.readFileSync(new URL('sim_tail.rb', wasm), 'utf8');
const user = `
amy = MIDIDevices.amy
puts "amy=#{amy.inspect} bit=#{amy.transport_id}"
fm  = amy.add_fm(channel: 0, voices: 6, patch: 128)
syn = amy.add_synth(channel: 1, voices: 4, patch: 3)
dev = MIDI::Device.new(amy)
dev.trigger(60, 110, duration: 400)
dev.trigger(64, 110, duration: 400)
dev.trigger(48, 110, duration: 400, channel: 1)
puts "parts: #{AMY.parts.size}"
loop do
  UI.process
  MIDI.sleep_ms 10
end
`;
const src = `${boot}\nbegin\n${user}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${tail}`;
const M = await createMidoriSim({ print: s => { if (!/^[D] \(/.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
M.simPresent = () => {};
let frames = 0, sumsq = 0, peak = 0;
M.amyOut = (ptr, n) => {
  const a = M.HEAP16.subarray(ptr >> 1, (ptr >> 1) + n * 2);
  for (let i = 0; i < a.length; i++) { sumsq += a[i] * a[i]; if (Math.abs(a[i]) > peak) peak = Math.abs(a[i]); }
  frames += n;
};
M._sim_boot();
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);
const rate = M._sim_amy_sample_rate(), block = M._sim_amy_block_size();
console.log('AMY', rate, 'Hz, block', block);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let last = performance.now(), rendered = 0, renderMs = 0, t0 = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    if (M._sim_amy_running()) {
      // keep up with real time, as the page's worklet would consume
      const want = Math.floor((performance.now() - t0) / 1000 * rate / block) - rendered;
      if (want > 0) { const s = performance.now(); rendered += M._sim_amy_render(Math.min(want, 16)); renderMs += performance.now() - s; }
    } else t0 = performance.now();
    await sleep(4);
  }
}
await run(1500);
const blockMs = block / rate * 1000;
console.log(`frames ${frames}, rms ${Math.sqrt(sumsq / Math.max(1, frames * 2)).toFixed(1)}, peak ${peak}`);
console.log(`render ${rendered} blocks, ${(renderMs / Math.max(1, rendered)).toFixed(3)} ms/block (budget ${blockMs.toFixed(2)} ms)`);
process.exit(0);
