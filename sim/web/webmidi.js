// Web MIDI ports in the device's roles (docs/WEB_SIMULATOR.md §5.1):
//   role 0  USB-MIDI Host   (e.g. a keyboard on the computer)
//   role 1  USB-MIDI Device (e.g. a DAW, through IAC / loopMIDI)
//   role 2  SAM2695         (the built-in synth unless a Web MIDI output is chosen)

export const ROLES = [
  { id: 0, name: 'USB-MIDI Host' },
  { id: 1, name: 'USB-MIDI Device' },
  { id: 2, name: 'SAM2695' },
];

export class WebMidiRoles {
  constructor() {
    this.access = null;
    this.inputs = [null, null, null];    // MIDIInput per role
    this.outputs = [null, null, null];   // MIDIOutput per role
    this.onInput = () => {};             // (role, bytes, timeStamp)
    this.onChange = () => {};
  }

  get supported() { return typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess; }

  // Asks for permission: call after a user gesture
  async enable() {
    if (!this.supported) throw new Error('Web MIDI is not available in this browser');
    this.access = await navigator.requestMIDIAccess({ sysex: false });
    this.access.onstatechange = () => this.onChange();
    this.onChange();
  }

  get inputPorts() { return this.access ? [...this.access.inputs.values()] : []; }
  get outputPorts() { return this.access ? [...this.access.outputs.values()] : []; }

  setInput(role, id) {
    const old = this.inputs[role];
    if (old) old.onmidimessage = null;
    const port = id ? this.inputPorts.find(p => p.id === id) : null;
    this.inputs[role] = port || null;
    if (port) port.onmidimessage = ev => this.onInput(role, ev.data, ev.timeStamp);
    this.onChange();
  }

  setOutput(role, id) {
    this.outputs[role] = id ? this.outputPorts.find(p => p.id === id) || null : null;
    this.onChange();
  }

  connected(role) { return !!(this.inputs[role] || this.outputs[role]); }

  // Sends at a page time (performance.now() milliseconds)
  send(role, bytes, atMs) {
    const out = this.outputs[role];
    if (!out) return false;
    out.send(bytes, atMs);
    return true;
  }
}
