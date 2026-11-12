// Node test for picoruby-web_midi: two fake Web MIDI ports; Ruby finds one
// by name, routes its input to the SAM2695 role and sends to the other.
//   node sim/test/node_web_midi.mjs
import createMidoriSim from '../web/wasm/midori-sim-tab5.js';
import fs from 'fs';

const wasm = new URL('../web/wasm/', import.meta.url);
const boot = fs.readFileSync(new URL('sim_boot-tab5.rb', wasm), 'utf8');
const tail = fs.readFileSync(new URL('sim_tail.rb', wasm), 'utf8');
const user = `
puts MIDIDevices.web_midis.map(&:name).inspect
kb  = MIDIDevices.web_midi("keystation")
daw = MIDIDevices.web_midi("IAC Driver Bus 1")
puts "kb=#{kb.inspect} bit=#{kb.transport_id} daw=#{daw.inspect} bit=#{daw.transport_id}"
puts "none=#{MIDIDevices.web_midi("nothing").inspect}"
MIDI.route(kb, MIDIDevices.sam2695)
MIDI.on(kb, :note_on) { |m| puts "on #{m.note} #{m.velocity}" }
MIDI::Device.new(daw).note_on(64, 99, channel: 2)
puts "devices: #{UI::MIDIDevices.list.map { |d| d[:name] rescue d }.inspect}" rescue puts "list: #{$!.message}"
loop do
  UI.process
  MIDI.sleep_ms 10
end
`;
const src = `${boot}\nbegin\n${user}\nrescue Exception => e\n  puts "Script error: #{e.class}: #{e.message}"\nend\n${tail}`;
const lines = [];
const M = await createMidoriSim({ print: s => { if (!/^[ID] \(/.test(s)) { lines.push(s); console.log('[out]', s); } }, printErr: s => console.log('[err]', s) });
M.simPresent = () => {};
const outs = [];
M.simMidiOut = (role, cin, b1, b2, b3) => outs.push(`role${role} ${b1.toString(16)} ${b2} ${b3}`);
M.webMidiOut = (slot, cin, b1, b2, b3) => outs.push(`port${slot} ${b1.toString(16)} ${b2} ${b3}`);
M._sim_boot();
const ports = [['Keystation 49 MK3', 1, 0], ['IAC Driver Bus 1', 1, 1]];
ports.forEach(([name, i, o], slot) => { M.ccall('web_midi_port', null, ['number', 'string', 'number', 'number'], [slot, name, i, o]); M._web_midi_connected(slot, 1); });
M.ccall('picorb_init', 'number', [], []);
M.ccall('picorb_create_task_with_filename', 'number', ['string', 'string'], [src, 'script.rb']);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let last = performance.now();
async function run(ms) {
  const end = performance.now() + ms;
  while (performance.now() < end) {
    const now = performance.now();
    while (now - last >= 4) { M._mrb_tick_wasm(); last += 4; }
    for (let i = 0; i < 200; i++) if (M._mrb_run_step_status() <= 0) break;
    M._sim_rtos_poll(); M._sim_frame();
    await sleep(5);
  }
}
await run(500);
M._web_midi_in(0, 0x09, 0x90, 60, 100);   // the keyboard plays C4
await run(300);
M._web_midi_in(0, 0x08, 0x80, 60, 0);
await run(300);
console.log('midi out:', outs);
process.exit(0);
