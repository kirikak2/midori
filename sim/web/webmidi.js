// Web MIDI for the simulator.
//
// Two ways a port reaches the device (docs/WEB_SIMULATOR.md §5.1):
//   - a role: the port stands in for one of the device's own connectors
//       role 0  USB-MIDI Host   (e.g. a keyboard on the computer)
//       role 1  USB-MIDI Device (e.g. a DAW, through IAC / loopMIDI)
//       role 2  SAM2695         (the built-in synth unless an output is chosen)
//   - by name: every port is also MIDIDevices.web_midi("name") in Ruby and a
//     row / column of the MIDI Devices screen (picoruby-web_midi). Web MIDI
//     lists inputs and outputs apart; one name = one port (slot).

export const ROLES = [
  { id: 0, name: 'USB-MIDI Host' },
  { id: 1, name: 'USB-MIDI Device' },
  { id: 2, name: 'SAM2695' },
];

export const MAX_NAMED_PORTS = 8;   // WEB_MIDI_MAX_PORTS

export class WebMidi {
  constructor() {
    this.access = null;
    this.sysex = false;                     // granted SysEx
    this.roleInput = [null, null, null];    // MIDIInput id per role
    this.roleOutput = [null, null, null];   // MIDIOutput id per role
    this.onRoleInput = () => {};            // (role, bytes, timeStamp)
    this.onPortInput = () => {};            // (slot, bytes, timeStamp)
    this.onChange = () => {};
    this._slots = [];                       // names, in slot order (kept while the page lives)
  }

  get supported() { return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess; }

  // Asks for permission: call after a user gesture. With SysEx where the
  // browser grants it (the device's SysEx: MIDI::Device#send_sysex, routes),
  // else without (sysex says which it got).
  async enable() {
    if (!this.supported) throw new Error('Web MIDI is not available in this browser');
    try {
      this.access = await navigator.requestMIDIAccess({ sysex: true });
      this.sysex = true;
    } catch (e) {
      this.access = await navigator.requestMIDIAccess({ sysex: false });
      this.sysex = false;
    }
    this.access.onstatechange = () => { this._listen(); this.onChange(); };
    this._listen();
    this.onChange();
  }

  get inputPorts() { return this.access ? [...this.access.inputs.values()] : []; }
  get outputPorts() { return this.access ? [...this.access.outputs.values()] : []; }

  // The named ports, one per name, with a stable slot each:
  // [{ slot, name, input, output, connected }]
  get namedPorts() {
    const byName = new Map();
    for (const p of this.inputPorts) byName.set(p.name, { ...(byName.get(p.name) || {}), input: p });
    for (const p of this.outputPorts) byName.set(p.name, { ...(byName.get(p.name) || {}), output: p });
    for (const name of byName.keys()) {
      if (!this._slots.includes(name) && this._slots.length < MAX_NAMED_PORTS) this._slots.push(name);
    }
    return this._slots.map((name, slot) => {
      const e = byName.get(name) || {};
      const connected = !!((e.input && e.input.state === 'connected') || (e.output && e.output.state === 'connected'));
      return { slot, name, input: e.input || null, output: e.output || null, connected };
    });
  }

  _listen() {
    for (const p of this.inputPorts) {
      p.onmidimessage = ev => {
        for (const role of [0, 1]) if (this.roleInput[role] === p.id) this.onRoleInput(role, ev.data, ev.timeStamp);
        const slot = this._slots.indexOf(p.name);
        if (slot >= 0) this.onPortInput(slot, ev.data, ev.timeStamp);
      };
    }
  }

  setRoleInput(role, id) { this.roleInput[role] = id || null; this.onChange(); }
  setRoleOutput(role, id) { this.roleOutput[role] = id || null; this.onChange(); }
  roleConnected(role) { return !!(this.roleInput[role] || this.roleOutput[role]); }

  // Sends at a page time (performance.now() milliseconds). true when the
  // port took it (a port is there); a SysEx without the permission is dropped.
  sendRole(role, bytes, atMs) {
    const id = this.roleOutput[role];
    const out = id && this.access ? this.access.outputs.get(id) : null;
    return out ? this._send(out, bytes, atMs) : false;
  }

  sendPort(slot, bytes, atMs) {
    const name = this._slots[slot];
    const out = this.outputPorts.find(p => p.name === name);
    return out ? this._send(out, bytes, atMs) : false;
  }

  _send(out, bytes, atMs) {
    try {
      out.send(bytes, atMs);
    } catch (e) {
      // InvalidAccessError: SysEx without the permission (or a malformed message)
      if (!this._warned) { console.warn(`Web MIDI: ${e.message}`); this._warned = true; }
    }
    return true;
  }
}
