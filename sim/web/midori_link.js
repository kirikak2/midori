// A connection to a real MIDoRI over its USB CDC console (Web Serial;
// docs/WEB_SIMULATOR.md §13). The device must be built in midi_device mode
// (the console is on the USB-C CDC) and be in UI mode for files: a running
// script has no loop to answer them.
//
//   const link = new MidoriLink({ onLine, onState });
//   await link.connect();                 // asks the browser for the port
//   await link.info();                    // { board, mode, script }
//   await link.list('/sd');               // [{ name, dir, size }]
//   await link.readFile('/app.rb');       // Uint8Array
//   await link.writeFile('/sd/x.rb', bytes);
//   await link.remove('/app.rb');  link.load('/sd/x.rb');  link.stop();
//
// Text from the device is split into lines (onLine). Commands the web page
// sends get framed answers from the device (console_input.c, main_task_base.rb):
//   info    -> "#info proto=1 board=tab5 mode=ui script="
//   ls dir  -> "#<ls dir", "f|d<TAB>size<TAB>name"..., "#>ls dir ok <n>" | "#>ls dir err <message>"
//   rm path -> "#>rm path ok" | "#>rm path err <message>"
// An answer names what it answers: one that comes after its command gave up
// waiting is not taken for the next command's.
// Files go by PicoModem (docs/PICOMODEM.md): a bare STX, the device's ACK,
// then frames STX | len(2) | cmd | payload | CRC-16/CCITT(2).

const STX = 0x02, ACK = 0x06;
const PM = {
  FILE_READ: 0x01, FILE_WRITE: 0x02, CHUNK: 0x04, ABORT: 0xff,
  FILE_DATA: 0x81, FILE_ACK: 0x82, CHUNK_ACK: 0x84, DONE_ACK: 0x8f, ERROR: 0xfe,
  OK: 0x00, READY: 0x01,
};
const CHUNK_SIZE = 480;            // the device's PicoModem::CHUNK_SIZE
// USB CDC writes in small pieces with a gap (the PicoRuby web terminal's
// values): the device's console buffers are small
const TX_PIECE = 32, TX_GAP_MS = 20;
const TIMEOUT_MS = 5000;
// The console protocol this page speaks (console_input.c "#info proto=")
export const PROTO = 2;
// A directory listing: the device stats every entry (a full SD card is slow)
const LIST_TIMEOUT_MS = 30000;
// "/sd/" -> "/sd", as the device names it in its answer
const normDir = d => { let s = String(d || '/').trim() || '/'; while (s.length > 1 && s.endsWith('/')) s = s.slice(0, -1); return s; };
// Espressif's USB vendor id: the port chooser shows these first
const FILTERS = [{ usbVendorId: 0x303a }];

const sleep = ms => new Promise(r => setTimeout(r, ms));
const enc = new TextEncoder(), dec = new TextDecoder();

export function crc16(bytes, crc = 0xffff) {
  for (const b of bytes) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

export function crc32(bytes, crc = 0) {
  crc = ~crc >>> 0;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = (crc & 1) ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1;
  }
  return ~crc >>> 0;
}

export function frame(cmd, payload = new Uint8Array(0)) {
  const body = new Uint8Array(1 + payload.length);
  body[0] = cmd;
  body.set(payload, 1);
  const out = new Uint8Array(3 + body.length + 2);
  out[0] = STX;
  out[1] = body.length >> 8; out[2] = body.length & 0xff;
  out.set(body, 3);
  const c = crc16(body);
  out[3 + body.length] = c >> 8; out[4 + body.length] = c & 0xff;
  return out;
}

// One framed answer's lines out of the console text ("> " prompts and the
// echo of the command may come first on a line)
const unprompt = line => line.replace(/^(> )+/, '');

export class MidoriLink {
  static get supported() { return typeof navigator !== 'undefined' && 'serial' in navigator; }

  constructor({ onLine, onState } = {}) {
    this.onLine = onLine || (() => {});     // (text) every console line, prompts removed
    this.onState = onState || (() => {});   // () connected / info changed
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.connected = false;
    this.device = null;                     // the last info(): { board, mode, script }
    this._text = '';                        // the console's unfinished line
    this._binary = null;                    // PicoModem: bytes not yet taken
    this._waiters = [];                     // line watchers: { test, resolve }
    this._queue = Promise.resolve();        // one command at a time
    this.listTimeoutMs = LIST_TIMEOUT_MS;
    this._recent = [];                      // the last lines, for a timeout's message
  }

  // ---- connection

  async connect(port) {
    if (!MidoriLink.supported) throw new Error('このブラウザは Web Serial に対応していません（Chrome / Edge を使ってください）');
    this.port = port || await navigator.serial.requestPort({ filters: FILTERS });
    await this.port.open({ baudRate: 115200 });
    this.writer = this.port.writable.getWriter();
    this.connected = true;
    this._readLoop();
    navigator.serial.addEventListener('disconnect', this._onDisconnect = e => { if (e.target === this.port) this._closed(); });
    this.onState();
    try {
      await this.info();
    } catch (e) {
      await this.disconnect();
      throw new Error(`MIDoRI が応答しません（${e.message}）。midi_device モードの MIDoRI の USB-C か確かめてください`);
    }
    return this.device;
  }

  async disconnect() {
    const port = this.port;
    this._closed();
    try { await this.reader?.cancel(); } catch (e) {}
    try { this.writer?.releaseLock(); } catch (e) {}
    try { await port?.close(); } catch (e) {}
  }

  _closed() {
    if (!this.connected) return;
    this.connected = false;
    this.device = null;
    this.port = null;
    for (const w of this._waiters) w.reject(new Error('disconnected'));
    this._waiters = [];
    if (this._onDisconnect) navigator.serial.removeEventListener('disconnect', this._onDisconnect);
    this.onState();
  }

  async _readLoop() {
    while (this.connected && this.port?.readable) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value) this._received(value);
        }
      } catch (e) {
        // a lost device ends the read; the disconnect event follows
      } finally {
        try { this.reader.releaseLock(); } catch (e) {}
      }
      if (!this.connected) break;
      await sleep(50);
    }
    this._closed();
  }

  _received(bytes) {
    if (this._binary) {
      const b = new Uint8Array(this._binary.length + bytes.length);
      b.set(this._binary); b.set(bytes, this._binary.length);
      this._binary = b;
      this._wake();
      return;
    }
    this._text += dec.decode(bytes, { stream: true });
    let i;
    while ((i = this._text.search(/\r?\n/)) >= 0) {
      const line = unprompt(this._text.slice(0, i).replace(/\r$/, ''));
      this._text = this._text.slice(this._text[i] === '\r' ? i + 2 : i + 1);
      this._line(line);
    }
  }

  _line(line) {
    this._recent.push(line);
    if (this._recent.length > 8) this._recent.shift();
    for (const w of [...this._waiters]) {
      if (w.test(line)) { this._waiters.splice(this._waiters.indexOf(w), 1); w.resolve(line); }
    }
    this.onLine(line);
  }

  // The next line passing test (within ms)
  // (the timeout's error carries what the device said last: lines .recent)
  _waitLine(test, ms = TIMEOUT_MS) {
    return new Promise((resolve, reject) => {
      const w = { test, resolve, reject };
      this._waiters.push(w);
      setTimeout(() => {
        const i = this._waiters.indexOf(w);
        if (i >= 0) {
          this._waiters.splice(i, 1);
          const e = new Error('timeout');
          e.recent = [...this._recent];
          reject(e);
        }
      }, ms);
    });
  }

  async _write(bytes) {
    if (!this.connected) throw new Error('MIDoRI に接続していません');
    for (let off = 0; off < bytes.length; off += TX_PIECE) {
      await this.writer.write(bytes.subarray(off, off + TX_PIECE));
      if (off + TX_PIECE < bytes.length) await sleep(TX_GAP_MS);
    }
  }

  // Run fn after the commands before it (the console is one conversation)
  _serial(fn) {
    const p = this._queue.then(fn, fn);
    this._queue = p.catch(() => {});
    return p;
  }

  // ---- console commands

  // Type a console line (the console's own commands: load, stop, ...)
  send(line) { return this._serial(() => this._write(enc.encode(`${line}\r`))); }

  info() {
    return this._serial(async () => {
      const got = this._waitLine(l => l.includes('#info '));
      await this._write(enc.encode('info\r'));
      const line = await got;
      const kv = {};
      for (const m of line.slice(line.indexOf('#info ') + 6).matchAll(/(\w+)=(\S*)/g)) kv[m[1]] = m[2];
      this.device = { board: kv.board, mode: kv.mode, script: kv.script || '', proto: Number(kv.proto) || 1 };
      this.onState();
      return this.device;
    });
  }

  // The firmware answers ls / rm in this page's protocol
  _checkProto() {
    if (this.device && this.device.proto < PROTO) {
      throw new Error(`MIDoRI のファームウェアが古く、このページと答え方が違います（proto ${this.device.proto}、このページは ${PROTO}）。ファームウェアを書き込み直してください`);
    }
  }

  // The entries of a directory on the device (UI mode)
  list(dir) {
    dir = normDir(dir);
    return this._serial(async () => {
      this._checkProto();
      const lines = [];
      let inside = false;
      const start = `#<ls ${dir}`, end = `#>ls ${dir} `;
      const done = this._waitLine(l => {
        if (l === start) { inside = true; lines.length = 0; return false; }
        if (l.startsWith(end)) return true;
        if (inside && !l.startsWith('#')) lines.push(l);
        return false;
      }, this.listTimeoutMs);
      await this._write(enc.encode(`ls ${dir}\r`));
      let line;
      try { line = await done; } catch (e) {
        if (e.recent) e.message = `ls ${dir}: 実機から答えがありません（${this.listTimeoutMs / 1000} 秒）。最後に届いた行: ${e.recent.map(l => JSON.stringify(l)).join(' / ') || 'なし'}`;
        throw e;
      }
      const answer = line.slice(end.length);
      const m = /^(ok|err) ?(.*)$/.exec(answer);
      if (!m || m[1] === 'err') throw new Error(m ? m[2] : answer);
      return lines.map(l => l.split('\t')).filter(p => p.length >= 3 && (p[0] === 'f' || p[0] === 'd'))
        .map(([t, size, ...name]) => ({ name: name.join('\t'), dir: t === 'd', size: Number(size) || 0 }));
    });
  }

  remove(path) {
    return this._serial(async () => {
      this._checkProto();
      const end = `#>rm ${path} `;
      const done = this._waitLine(l => l.startsWith(end));
      await this._write(enc.encode(`rm ${path}\r`));
      const m = /^(ok|err) ?(.*)$/.exec((await done).slice(end.length));
      if (!m || m[1] === 'err') throw new Error(m ? m[2] : 'rm failed');
    });
  }

  load(path) { return this.send(`load ${path}`); }
  stop() { return this.send('stop'); }
  rescan() { return this.send('rescan'); }

  // ---- PicoModem

  readFile(path) {
    return this._modem(async () => {
      await this._sendFrame(PM.FILE_READ, enc.encode(path));
      const parts = [];
      let total = -1, got = 0, crc = 0;
      for (;;) {
        const [cmd, payload] = await this._recvFrame();
        if (cmd === PM.ERROR) throw new Error(dec.decode(payload));
        if (cmd === PM.DONE_ACK) {
          const sum = ((payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4]) >>> 0;
          if (payload[0] !== PM.OK || sum !== crc) throw new Error(`${path}: CRC が一致しません`);
          break;
        }
        if (cmd !== PM.FILE_DATA) throw new Error(`unexpected frame 0x${cmd.toString(16)}`);
        let data = payload;
        if (total < 0) {
          total = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) >>> 0;
          data = payload.subarray(4);
        }
        parts.push(data);
        got += data.length;
        crc = crc32(data, crc);
        await this._sendFrame(PM.CHUNK_ACK, new Uint8Array([PM.OK]));
      }
      const out = new Uint8Array(got);
      let off = 0;
      for (const p of parts) { out.set(p, off); off += p.length; }
      return out;
    });
  }

  writeFile(path, data) {
    const bytes = typeof data === 'string' ? enc.encode(data) : data;
    return this._modem(async () => {
      const p = enc.encode(path);
      const head = new Uint8Array(4 + p.length);
      new DataView(head.buffer).setUint32(0, bytes.length);
      head.set(p, 4);
      await this._sendFrame(PM.FILE_WRITE, head);
      let [cmd, payload] = await this._recvFrame();
      if (cmd === PM.ERROR) throw new Error(dec.decode(payload));
      if (cmd !== PM.FILE_ACK || payload[0] !== PM.READY) throw new Error('the device did not accept the file');
      for (let off = 0; off < bytes.length; off += CHUNK_SIZE) {
        await this._sendFrame(PM.CHUNK, bytes.subarray(off, off + CHUNK_SIZE));
        [cmd, payload] = await this._recvFrame();
        if (cmd === PM.ERROR) throw new Error(dec.decode(payload));
        if (cmd !== PM.CHUNK_ACK) throw new Error(`unexpected frame 0x${cmd.toString(16)}`);
      }
      // An empty file has no chunks: the device answers DONE_ACK at once
      [cmd, payload] = await this._recvFrame();
      if (cmd === PM.ERROR) throw new Error(dec.decode(payload));
      const sum = ((payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4]) >>> 0;
      if (cmd !== PM.DONE_ACK || payload[0] !== PM.OK || sum !== crc32(bytes)) throw new Error(`${path}: CRC が一致しません`);
    });
  }

  // One PicoModem session: STX, the ACK, fn's frames; then text again
  _modem(fn) {
    return this._serial(async () => {
      this._binary = new Uint8Array(0);
      try {
        await this._write(new Uint8Array([STX]));
        // Console text may come before the ACK
        await this._take(b => {
          const i = b.indexOf(ACK);
          return i < 0 ? { used: b.length, done: false } : { used: i + 1, done: true };
        });
        return await fn();
      } finally {
        this._binary = null;
        // The device prints "[PicoModem] ..." once it is back in text
        this._text = '';
      }
    });
  }

  async _sendFrame(cmd, payload) { await this._write(frame(cmd, payload)); }

  // The next frame: [cmd, payload] (bytes before its STX are dropped)
  async _recvFrame() {
    let result = null;
    await this._take(b => {
      const s = b.indexOf(STX);
      if (s < 0) return { used: b.length, done: false };
      if (b.length < s + 3) return { used: s, done: false };
      const len = (b[s + 1] << 8) | b[s + 2];
      if (b.length < s + 5 + len) return { used: s, done: false };
      const body = b.subarray(s + 3, s + 3 + len);
      const c = (b[s + 3 + len] << 8) | b[s + 4 + len];
      if (c !== crc16(body)) throw new Error('PicoModem: CRC error');
      result = [body[0], body.slice(1)];
      return { used: s + 5 + len, done: true };
    });
    return result;
  }

  // Feed the binary buffer to parse until it says done; parse returns how
  // many bytes from the front it used ({ used, done })
  async _take(parse) {
    const end = Date.now() + TIMEOUT_MS;
    for (;;) {
      const { used, done } = parse(this._binary);
      if (used > 0) this._binary = this._binary.slice(used);
      if (done) return;
      if (Date.now() > end) throw new Error('PicoModem: timeout');
      await new Promise(r => { this._wakeUp = r; setTimeout(r, 50); });
    }
  }

  _wake() { if (this._wakeUp) { const w = this._wakeUp; this._wakeUp = null; w(); } }
}
