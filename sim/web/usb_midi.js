// MIDI messages <-> USB-MIDI event packets [cable|CIN, b1, b2, b3], the form
// the device's transports carry (and picoruby-midi routes).
//
// SysEx goes as CIN 4 (start / continue, 3 bytes) and ends with CIN 5 / 6 / 7
// (1 / 2 / 3 bytes), as MIDI::Device#send_sysex sends it. Web MIDI hands over
// and takes a SysEx message whole (F0 ... F7), so the page splits what it
// receives and reassembles what the device sends, per port.

const MAX_SYSEX = 64 * 1024;   // a longer message is dropped

// One MIDI message -> its packets ([] for what USB-MIDI cannot carry)
export function toUsbPackets(bytes) {
  const s = bytes[0];
  if (s === undefined) return [];
  if (s === 0xF0) {
    const out = [];
    let i = 0;
    while (bytes.length - i > 3) {
      out.push([0x04, bytes[i], bytes[i + 1], bytes[i + 2]]);
      i += 3;
    }
    const n = bytes.length - i;   // 1..3: the last bytes, F7 among them
    out.push([0x04 + n, bytes[i], n > 1 ? bytes[i + 1] : 0, n > 2 ? bytes[i + 2] : 0]);
    return out;
  }
  if (s >= 0xF8) return [[0x0F, s, 0, 0]];
  const hi = s & 0xF0;
  if (hi >= 0x80 && hi <= 0xE0) {
    return [[hi >> 4, s, bytes[1] || 0, hi === 0xC0 || hi === 0xD0 ? 0 : (bytes[2] || 0)]];
  }
  if (s === 0xF2) return [[0x03, s, bytes[1] || 0, bytes[2] || 0]];
  if (s === 0xF3 || s === 0xF1) return [[0x02, s, bytes[1] || 0, 0]];
  if (s === 0xF6) return [[0x05, s, 0, 0]];
  return [];
}

// Packets -> messages, for one port: feed() returns a whole message, or null
// while a SysEx is still coming (realtime bytes may arrive in between)
export class UsbMidiAssembler {
  constructor() { this.sysex = null; }

  feed(cin, b1, b2, b3) {
    switch (cin & 0x0F) {
      case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: case 0x3: return [b1, b2, b3];
      case 0xC: case 0xD: case 0x2: return [b1, b2];
      case 0xF: return [b1];
      case 0x4:
        if (b1 === 0xF0 || !this.sysex) this.sysex = [];
        return this._add([b1, b2, b3], false);
      case 0x5:
        // Ends a SysEx, or a single-byte system common message (F6)
        if (!this.sysex) return b1 === 0xF7 ? null : [b1];
        return this._add([b1], true);
      // A SysEx of 2 or 3 bytes starts and ends in one packet
      case 0x6:
        if (b1 === 0xF0) this.sysex = [];
        return this.sysex ? this._add([b1, b2], true) : null;
      case 0x7:
        if (b1 === 0xF0) this.sysex = [];
        return this.sysex ? this._add([b1, b2, b3], true) : null;
      default: return null;
    }
  }

  _add(bytes, end) {
    if (this.sysex.length + bytes.length > MAX_SYSEX) { this.sysex = null; return null; }
    this.sysex.push(...bytes);
    if (!end) return null;
    const msg = this.sysex;
    this.sysex = null;
    return msg[0] === 0xF0 ? msg : null;   // a tail without its start is dropped
  }
}
