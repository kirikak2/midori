// Node test of SysEx through the simulator: a Web MIDI port's SysEx is
// routed to another port, MIDI::Device#send_sysex reaches a port and the
// USB-MIDI Device role, and the page's assemblers put the messages back
// together (sim/web/usb_midi.js).
//   node sim/test/node_sysex.mjs
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import { toUsbPackets, UsbMidiAssembler } from '../web/usb_midi.js';
import fs from 'fs';

const wasm = new URL('../web/wasm/', import.meta.url);
const r = n => fs.readFileSync(new URL(n, wasm), 'utf8');
const user = `
src = MIDIDevices.web_midi("Synth")
dst = MIDIDevices.web_midi("Editor")
MIDI.route(src, dst)
MIDI::Device.new(dst).send_sysex([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7])
MIDI::Device.new(MIDIDevices.usb_midi_device).send_sysex([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7])
loop do
  UI.process
  MIDI.sleep_ms 10
end
`;
const src = `${r('sim_boot-tab5.rb')}\n${r('sim_script.rb')}\nbegin\n${user}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${r('sim_tail.rb')}`;
globalThis.__midoriSim = { scriptEnded() {} };
const M = await createMidoriSim({ print: s => { if (/error/i.test(s)) console.log('[out]', s); }, printErr: s => console.log('[err]', s) });
M.simPresent = () => {};
const got = [];
const asm = {};
const take = (key, cin, b1, b2, b3) => {
  const m = (asm[key] ||= new UsbMidiAssembler()).feed(cin, b1, b2, b3);
  if (m) got.push(`${key}: ${m.map(b => b.toString(16).padStart(2, '0')).join(' ')}`);
};
M.webMidiOut = (slot, cin, b1, b2, b3) => take(`port${slot}`, cin, b1, b2, b3);
M.simMidiOut = (role, cin, b1, b2, b3) => take(`role${role}`, cin, b1, b2, b3);
M._sim_boot();
[['Synth', 1, 0], ['Editor', 0, 1]].forEach(([n, i, o], slot) => {
  M.ccall('web_midi_port', null, ['number', 'string', 'number', 'number'], [slot, n, i, o]);
  M._web_midi_connected(slot, 1);
});
M._sim_midi_connect(1, 1);   // the USB-MIDI Device role is assigned
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);
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
await run(500);
// A 1 KB dump from the "Synth" port, then a short one
const dump = [0xF0, 0x43, 0x00, 0x09, 0x20, 0x00];
for (let i = 0; i < 1024; i++) dump.push(i & 0x7F);
dump.push(0xF7);
for (const p of toUsbPackets(dump)) M._web_midi_in(0, ...p);
for (const p of toUsbPackets([0xF0, 0x7D, 0xF7])) M._web_midi_in(0, ...p);
await run(500);
for (const g of got) console.log(g.length > 90 ? `${g.slice(0, 90)}... (${g.split(' ').length - 1} bytes)` : g);
const ok = got.some(g => g.startsWith('port1: f0 41')) && got.some(g => g.startsWith('role1: f0 7e')) &&
  got.some(g => g.startsWith('port1: f0 43') && g.split(' ').length - 1 === dump.length) && got.some(g => g === 'port1: f0 7d f7');
console.log(ok ? 'SYSEX OK' : 'SYSEX FAIL');
process.exit(ok ? 0 : 1);
