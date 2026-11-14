// AMY's output in the page (docs/WEB_SIMULATOR.md §12.2).
//
// AMY renders on the page's thread, in the device's wasm (AMY_GEM_POLLED):
// pump() asks for as many blocks as keep about LEAD_MS of audio queued, and
// each block arrives through Module.amyOut, is copied out of the wasm heap
// and posted to an AudioWorklet, which plays the queue. No SharedArrayBuffer
// (GitHub Pages cannot send the headers it needs); the worklet reports what
// it took from the queue so the page knows how much is left.

const LEAD_MS = 60;          // audio kept queued ahead of the worklet
const MAX_BLOCKS_PER_PUMP = 32;

const WORKLET = `
class AmySink extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.offset = 0;
    this.taken = 0;
    this.port.onmessage = e => {
      if (e.data === 'reset') { this.queue = []; this.offset = 0; }
      else this.queue.push(e.data);
    };
  }
  process(inputs, outputs) {
    const out = outputs[0];
    const L = out[0], R = out[1] || out[0];
    for (let i = 0; i < L.length; i++) {
      const b = this.queue[0];
      if (!b) { L[i] = 0; R[i] = 0; continue; }
      L[i] = b[this.offset] / 32768;
      R[i] = b[this.offset + 1] / 32768;
      this.offset += 2;
      this.taken++;
      if (this.offset >= b.length) { this.queue.shift(); this.offset = 0; }
    }
    if (this.taken >= 1024) { this.port.postMessage(this.taken); this.taken = 0; }
    return true;
  }
}
registerProcessor('amy-sink', AmySink);
`;

export class AmyAudio {
  constructor(ctx) {
    this.ctx = ctx;
    this.node = null;
    this.queued = 0;       // frames posted and not yet taken by the worklet
    this.volume = 0.7;
    this.ready = null;
  }

  // Loads the worklet (once per AudioContext)
  init() {
    if (!this.ready) {
      const url = URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }));
      this.ready = this.ctx.audioWorklet.addModule(url).then(() => {
        this.node = new AudioWorkletNode(this.ctx, 'amy-sink', { outputChannelCount: [2] });
        this.node.port.onmessage = e => { this.queued = Math.max(0, this.queued - e.data); };
        this.gain = this.ctx.createGain();
        this.gain.gain.value = this.volume;
        this.node.connect(this.gain).connect(this.ctx.destination);
      });
    }
    return this.ready;
  }

  setVolume(percent) {
    this.volume = percent / 100;
    if (this.gain) this.gain.gain.value = this.volume;
  }

  // A new device: drop what the old one queued
  reset() {
    this.queued = 0;
    if (this.node) this.node.port.postMessage('reset');
  }

  // Module.amyOut: one block of interleaved stereo int16 in the wasm heap
  push(M, ptr, frames) {
    if (!this.node) return;
    const block = M.HEAP16.slice(ptr >> 1, (ptr >> 1) + frames * 2);
    this.node.port.postMessage(block, [block.buffer]);
    this.queued += frames;
  }

  // Render what keeps LEAD_MS queued (from the device's loop)
  pump(M) {
    if (!this.node || !M._sim_amy_running()) return;
    const rate = M._sim_amy_sample_rate(), block = M._sim_amy_block_size();
    const want = rate * LEAD_MS / 1000 - this.queued;
    if (want <= 0) return;
    M._sim_amy_render(Math.min(MAX_BLOCKS_PER_PUMP, Math.ceil(want / block)));
  }
}
