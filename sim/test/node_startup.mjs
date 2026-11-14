// Node test of the start-up shared with the device (main_task_base.rb
// PLUGIN_DIRS / APP_SCRIPT / SDLoader): files in Emscripten's FS as the page
// copies them from a connected MIDoRI; plugins load from /plugins, then
// /sd/plugins; UI mode with $sim_boot_autorun asks for /app.rb, without it
// does not.
//   node sim/test/node_startup.mjs
import fs from 'fs';

const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const FILES = {
  '/plugins/b_flash.rb': 'puts "plugin /plugins/b_flash"\n$order = ($order || []) << :flash\n',
  '/sd/plugins/a_sd.rb': 'puts "plugin /sd/plugins/a_sd (after #{$order.inspect})"\n',
  '/sd/plugins/_lib.rb': 'puts "must not load: starts with _"\n',
  '/sd/plugins/broken.rb': 'def oops(\n',
  '/app.rb': 'puts "the startup script"\n',
};

async function start(autorun) {
  const out = [];
  let requested = null;
  globalThis.__midoriSim = { scriptEnded() {}, requestScript: p => { requested = String(p); } };
  const M = await createMidoriSim({ print: s => { if (!/^[ID] \(|initialized/.test(s)) out.push(s); }, printErr: s => out.push(`[err] ${s}`) });
  for (const [p, t] of Object.entries(FILES)) { M.FS.mkdirTree(p.slice(0, p.lastIndexOf('/'))); M.FS.writeFile(p, t); }
  M.simPresent = () => {};
  M._sim_boot();
  M.ccall('picorb_init', 'number', [], []);
  const vm = `${autorun ? '$sim_boot_autorun = true\n' : ''}${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_ui.rb')}`;
  M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [vm, 'boot.rb']);
  let last = performance.now();
  const end = performance.now() + 1500;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    await new Promise(res => setTimeout(res, 4));
  }
  return { out, requested };
}

const ok = (c, w) => { console.log(`${c ? 'ok  ' : 'FAIL'} ${w}`); if (!c) process.exitCode = 1; };
const a = await start(true);
ok(a.requested === '/app.rb', `power-up runs ${a.requested}`);
ok(!a.out.some(l => l.startsWith('plugin')), 'the startup script comes before the plugins (they load in its script mode)');
const b = await start(false);
console.log(b.out.filter(l => /plugin|Plugin/.test(l)).join('\n'));
ok(b.requested === null, 'a later UI mode does not run it');
const i1 = b.out.findIndex(l => l.includes('plugin /plugins/b_flash')), i2 = b.out.findIndex(l => l.includes('plugin /sd/plugins/a_sd'));
ok(i1 >= 0 && i2 > i1, '/plugins load before /sd/plugins');
ok(!b.out.some(l => l.includes('must not load')), 'files starting with _ are left alone');
ok(b.out.some(l => l.includes('/sd/plugins/broken.rb:')), 'a broken plugin is reported and skipped');
process.exit(process.exitCode || 0);
