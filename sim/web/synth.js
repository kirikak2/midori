// The simulator's built-in synth: stands in for the SAM2695 (General MIDI
// over UART on the device). A small polyphonic subtractive synth on Web
// Audio: enough to hear notes, velocity, CC 7 / 10 / 74 and pitch bend.
// Channel 10 plays short noise / sine hits instead of pitches.

export class BuiltinSynth {
  constructor() {
    this.ctx = null;
    this.voices = new Map();   // "ch:note" -> voice
    this.channels = Array.from({ length: 16 }, () => ({ volume: 100 / 127, pan: 0, cutoff: 1, bend: 0 }));
    this.volume = 0.7;
  }

  // AudioContext starts suspended until a user gesture: call from one
  resume() {
    if (!this.ctx) {
      this.ctx = new AudioContext();
      this.master = this.ctx.createGain();
      this.master.gain.value = this.volume * 0.3;
      this.master.connect(this.ctx.destination);
    }
    if (this.ctx.state === 'suspended') this.ctx.resume();
  }

  setVolume(percent) {
    this.volume = percent / 100;
    if (this.master) this.master.gain.value = this.volume * 0.3;
  }

  // One MIDI message; `when` is an AudioContext time (or now)
  message(bytes, when) {
    if (!this.ctx) return;
    const t = Math.max(when || 0, this.ctx.currentTime);
    const status = bytes[0], hi = status & 0xF0, ch = status & 0x0F;
    if (hi === 0x90 && bytes[2] > 0) this.noteOn(ch, bytes[1], bytes[2], t);
    else if (hi === 0x80 || hi === 0x90) this.noteOff(ch, bytes[1], t);
    else if (hi === 0xB0) this.control(ch, bytes[1], bytes[2], t);
    else if (hi === 0xE0) {
      this.channels[ch].bend = (((bytes[2] << 7) | bytes[1]) - 8192) / 8192 * 2;   // +-2 semitones
      for (const [k, v] of this.voices) if (v.ch === ch) v.osc.detune.setValueAtTime(this.channels[ch].bend * 100, t);
    }
  }

  allOff(t) {
    for (const v of this.voices.values()) this._release(v, t || this.ctx.currentTime, 0.02);
    this.voices.clear();
  }

  noteOn(ch, note, vel, t) {
    this.noteOff(ch, note, t);
    const c = this.channels[ch];
    const g = this.ctx.createGain();
    const pan = this.ctx.createStereoPanner();
    pan.pan.value = c.pan;
    const level = (vel / 127) * c.volume;
    let osc, filter = null;
    if (ch === 9) {
      // Drums: low notes are a pitched thump, the rest noise bursts
      if (note < 40) {
        osc = this.ctx.createOscillator();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(140, t);
        osc.frequency.exponentialRampToValueAtTime(45, t + 0.15);
      } else {
        osc = this.ctx.createBufferSource();
        osc.buffer = this._noise();
        filter = this.ctx.createBiquadFilter();
        filter.type = 'highpass';
        filter.frequency.value = 800 + (note - 40) * 120;
      }
      g.gain.setValueAtTime(level, t);
      g.gain.exponentialRampToValueAtTime(0.001, t + (note < 40 ? 0.3 : 0.12));
      (filter ? osc.connect(filter).connect(g) : osc.connect(g)).connect(pan).connect(this.master);
      osc.start(t);
      osc.stop(t + 0.4);
      return;
    }
    osc = this.ctx.createOscillator();
    osc.type = 'sawtooth';
    osc.frequency.value = 440 * Math.pow(2, (note - 69) / 12);
    osc.detune.value = c.bend * 100;
    filter = this.ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = 300 + 8000 * c.cutoff * c.cutoff;
    filter.Q.value = 2;
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(level, t + 0.005);
    g.gain.setTargetAtTime(level * 0.6, t + 0.005, 0.2);
    osc.connect(filter).connect(g).connect(pan).connect(this.master);
    osc.start(t);
    this.voices.set(`${ch}:${note}`, { ch, osc, g, filter });
  }

  noteOff(ch, note, t) {
    const k = `${ch}:${note}`;
    const v = this.voices.get(k);
    if (!v) return;
    this.voices.delete(k);
    this._release(v, t, 0.15);
  }

  control(ch, cc, val, t) {
    const c = this.channels[ch];
    if (cc === 7) c.volume = val / 127;
    else if (cc === 10) c.pan = (val - 64) / 64;
    else if (cc === 74) {
      c.cutoff = val / 127;
      for (const v of this.voices.values()) {
        if (v.ch === ch && v.filter) v.filter.frequency.setTargetAtTime(300 + 8000 * c.cutoff * c.cutoff, t, 0.01);
      }
    } else if (cc === 120 || cc === 123) {
      for (const [k, v] of this.voices) if (v.ch === ch) { this._release(v, t, cc === 120 ? 0.01 : 0.1); this.voices.delete(k); }
    }
  }

  _release(v, t, time) {
    v.g.gain.cancelScheduledValues(t);
    v.g.gain.setTargetAtTime(0, t, time / 3);
    v.osc.stop(t + time * 2);
  }

  _noise() {
    if (!this._noiseBuf) {
      const n = this.ctx.sampleRate * 0.4;
      this._noiseBuf = this.ctx.createBuffer(1, n, this.ctx.sampleRate);
      const d = this._noiseBuf.getChannelData(0);
      for (let i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
    }
    return this._noiseBuf;
  }
}
