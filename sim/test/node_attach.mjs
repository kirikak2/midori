// Node test of attach on the simulator (docs/ATTACH.md): code queued through
// the page's bridge (__midoriSim.takeAttach) runs inside the running VM -
// a script, and UI mode - and answers in the device's frames.
//   node sim/test/node_attach.mjs
const BOARD = process.env.BOARD || 'tab5';
const { default: createMidoriSim } = await import(`../web/wasm/midori-sim-${BOARD}.js`);
import fs from 'fs';

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const SCRIPT = `$count = 0
$dev = MIDI::Device.new(MIDIDevices.sam2695)
loop do
  $count += 1
  UI.process
  MIDI.sleep_ms 10
end`;
const scriptSrc = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_script.rb')}\nbegin\n${SCRIPT}\nrescue Exception => e\n  puts "Script error: #{e.message}"\nend\n${r('sim_tail.rb')}`;
const uiSrc = `${r(`sim_boot-${BOARD}.rb`)}\n${r('sim_ui.rb')}`;

let failures = 0;
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok ? '' : `  ${detail}`}`);
  if (!ok) failures++;
};
const sleep = ms => new Promise(res => setTimeout(res, ms));

async function boot(src) {
  const queue = [];
  let cancel = false;
  const frames = [];        // every "#<eval" / "#>eval" line and what came between
  globalThis.__midoriSim = {
    scriptEnded: () => {},
    takeAttach: () => queue.shift() ?? null,
    attachCancel: () => { const c = cancel; cancel = false; return c; },
  };
  const M = await createMidoriSim({
    print: s => { if (/^#[<>]eval /.test(s) || frames.inside) frames.push(s); if (/^#<eval /.test(s)) frames.inside = true; if (/^#>eval /.test(s)) frames.inside = false; },
    printErr: s => console.log('[err]', s),
  });
  M.simPresent = () => {};
  M._sim_boot();
  M.ccall('picorb_init', 'number', [], []);
  M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);
  let last = performance.now();
  const run = async ms => {
    const end = performance.now() + ms;
    while (performance.now() < end) {
      const now = performance.now();
      while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
      for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
      M._sim_rtos_poll(); M._sim_frame();
      await sleep(2);
    }
  };
  let seq = 0;
  // One evaluation: [ok, value, output lines]
  const evaluate = async (code, ms = 1500) => {
    const id = `t${++seq}`;
    frames.length = 0;
    queue.push(`E${id} ${code}`);
    const end = performance.now() + ms;
    while (performance.now() < end) {
      await run(20);
      const fin = frames.find(l => l.startsWith(`#>eval ${id} `));
      if (fin) {
        const m = /^#>eval \S+ (ok|err) ?(.*)$/.exec(fin);
        return [m[1] === 'ok', m[2], frames.filter(l => !/^#[<>]eval /.test(l))];
      }
    }
    return [null, 'timeout', []];
  };
  return { M, run, evaluate, queue, setCancel: () => { cancel = true; } };
}

// ---- script mode
{
  const vm = await boot(scriptSrc);
  await vm.run(800);
  let [ok, v] = await vm.evaluate('$count > 0');
  check('sees the script\'s globals', ok && v === 'true', v);
  [ok, v] = await vm.evaluate('$dev.class');
  check('sees the script\'s objects', ok && v === 'MIDI::Device', v);
  [ok, v] = await vm.evaluate('def twice(x)\n  x * 2  # a comment\nend\ntwice(21)');
  check('several lines (and a comment on one)', ok && v === '42', v);
  [ok, v] = await vm.evaluate('twice(5)');
  check('methods stay from one eval to the next', ok && v === '10', v);
  [ok, v] = await vm.evaluate('mine = 7');
  [ok, v] = await vm.evaluate('mine + 1');
  check('locals stay from one eval to the next', ok && v === '8', v);
  let out;
  [ok, v, out] = await vm.evaluate('puts "こんにちは"\n:done');
  check('output (UTF-8) comes between the frames', ok && v === ':done' && out.includes('こんにちは'), JSON.stringify([v, out]));
  [ok, v] = await vm.evaluate('raise ArgumentError, "boom"');
  check('an exception is an err answer', ok === false && v === 'ArgumentError: boom', v);
  [ok, v] = await vm.evaluate('(1 + ');
  check('code that does not compile', ok === false && /SyntaxError/.test(v), v);
  [ok, v] = await vm.evaluate('$count');
  const before = Number(v);
  await vm.run(300);
  [ok, v] = await vm.evaluate('$count');
  check('the script keeps running between evals', ok && Number(v) > before, `${before} -> ${v}`);

  // Code that never ends (but sleeps), then the page's Cancel
  vm.queue.push('Eloop1 loop { sleep_ms 10 }');   // (a loop that never sleeps would hold the page: wasm ticks only between steps)
  await vm.run(300);
  vm.setCancel();
  await vm.run(300);
  [ok, v] = await vm.evaluate('$count');
  const c1 = Number(v);
  await vm.run(300);
  [ok, v] = await vm.evaluate('$count');
  check('cancel stops a running eval and the script goes on', ok && Number(v) > c1, `${c1} -> ${v}`);
  [ok, v] = await vm.evaluate('twice(1)');
  check('a new sandbox after a cancel (its methods are global)', ok && v === '2', v);
}

// ---- UI mode
{
  const vm = await boot(uiSrc);
  await vm.run(1500);
  const [ok, v] = await vm.evaluate('$midori_mode');
  check('UI mode answers too', ok && v === ':ui', v);
}

console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
