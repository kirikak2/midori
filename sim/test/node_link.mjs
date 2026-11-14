// Node test of web/midori_link.js against a fake MIDoRI that speaks the
// device's console protocol (console_input.c / main_task_base.rb: echo, "> "
// prompts, info, ls, rm, framed answers) and PicoModem as picomodem.rb does.
//   node sim/test/node_link.mjs
import { MidoriLink, frame, crc16, crc32 } from '../web/midori_link.js';

const enc = new TextEncoder(), dec = new TextDecoder();
Object.defineProperty(globalThis, 'navigator', {
  value: { serial: { addEventListener() {}, removeEventListener() {} } }, configurable: true,
});

class FakeMidori {
  constructor() {
    this.fs = new Map([['/app.rb', enc.encode('puts "app"\n')], ['/sd/a.rb', enc.encode('puts 1\n')], ['/plugins/p.rb', enc.encode('# p\n')]]);
    this.mode = 'ui';
    this.line = '';
    this.bin = null;          // PicoModem bytes
    this.session = null;      // generator of the running command
    let ctl;
    this.readable = new ReadableStream({ start: c => { ctl = c; } });
    this.out = b => ctl.enqueue(typeof b === 'string' ? enc.encode(b) : b);
    this.writable = new WritableStream({ write: chunk => this.input(chunk) });
  }
  async open() {}
  async close() {}

  input(bytes) {
    for (const b of bytes) {
      if (this.bin) { this.bin.push(b); continue; }
      if (b === 0x02) { this.bin = []; this.out(new Uint8Array([0x06])); this.modem(); continue; }
      if (b === 0x0d) { this.out('\r\n'); this.exec(this.line); this.line = ''; continue; }
      this.line += String.fromCharCode(b);
      this.out(String.fromCharCode(b));   // echo
    }
  }

  exec(line) {
    const [cmd, ...rest] = line.split(' ');
    const arg = rest.join(' ');
    if (cmd === 'info') this.out(`#info proto=${this.proto ?? 3} board=tab5 mode=${this.mode} script=\r\n`);
    else if (cmd === 'ls' && this.delay && this.delay[arg]) {
      // Answer late (ms per directory)
      const ms = this.delay[arg];
      delete this.delay[arg];
      setTimeout(() => this.exec(`ls ${arg}`), ms);
    } else if (cmd === 'ls') {
      this.out('> ');   // the C prompt comes before Ruby's answer
      const dir = arg === '/' ? '' : arg;
      const names = new Map();
      for (const [p, d] of this.fs) {
        if (!p.startsWith(dir + '/')) continue;
        const rest = p.slice(dir.length + 1);
        const [first, ...more] = rest.split('/');
        names.set(first, more.length ? ['d', 0] : ['f', d.length]);
      }
      if (!names.size && dir) { this.out(`#>ls ${arg} err no such directory\r\n> `); return; }
      this.out(`#<ls ${arg}\r\nI (123) noise: a log line\r\n`);
      for (const [n, [t, s]] of names) this.out(`${t}\t${s}\t${n}\r\n`);
      this.out(`#>ls ${arg} ok ${names.size}\r\n> `);
    } else if (cmd === 'mv') {
      const [from, to] = rest;
      if (!this.fs.has(from)) this.out(`#>mv ${arg} err no such file\r\n> `);
      else { this.fs.set(to, this.fs.get(from)); this.fs.delete(from); this.out(`#>mv ${arg} ok\r\n> `); }
    } else if (cmd === 'rm') {
      if (this.fs.delete(arg)) this.out(`#>rm ${arg} ok\r\n> `); else this.out(`#>rm ${arg} err no such file\r\n> `);
    } else this.out('> ');
  }

  // picomodem.rb, one command per session
  async modem() {
    const recv = async () => {
      for (let i = 0; i < 500; i++) {
        const b = this.bin;
        if (b.length >= 3) {
          const len = (b[1] << 8) | b[2];
          if (b.length >= len + 5) {
            const body = Uint8Array.from(b.slice(3, 3 + len));
            const c = (b[3 + len] << 8) | b[4 + len];
            this.bin = b.slice(len + 5);
            if (c !== crc16(body)) throw new Error('device: bad CRC');
            return [body[0], body.slice(1)];
          }
        }
        await new Promise(r => setTimeout(r, 2));
      }
      throw new Error('device: timeout');
    };
    const [cmd, payload] = await recv();
    if (cmd === 0x01) {
      const path = dec.decode(payload);
      const data = this.fs.get(path);
      if (!data) this.out(frame(0xfe, enc.encode(`File not found: ${path}`)));
      else {
        let crc = 0;
        for (let off = 0, first = true; off < data.length; off += 480, first = false) {
          const chunk = data.subarray(off, off + 480);
          crc = crc32(chunk, crc);
          const p = first ? new Uint8Array([...[24, 16, 8, 0].map(s => (data.length >>> s) & 0xff), ...chunk]) : chunk;
          this.out(frame(0x81, p));
          const [ack] = await recv();
          if (ack !== 0x84) throw new Error('device: no CHUNK_ACK');
        }
        this.out(frame(0x8f, new Uint8Array([0, ...[24, 16, 8, 0].map(s => (crc >>> s) & 0xff)])));
      }
    } else if (cmd === 0x02) {
      const total = new DataView(payload.buffer, payload.byteOffset).getUint32(0);
      const path = dec.decode(payload.subarray(4));
      this.out(frame(0x82, new Uint8Array([1])));
      const parts = [];
      let got = 0, crc = 0;
      while (got < total) {
        const [c, chunk] = await recv();
        if (c !== 0x04) throw new Error('device: expected CHUNK');
        parts.push(chunk); got += chunk.length; crc = crc32(chunk, crc);
        this.out(frame(0x84, new Uint8Array([0])));
      }
      const data = new Uint8Array(got);
      let off = 0; for (const p of parts) { data.set(p, off); off += p.length; }
      this.fs.set(path, data);
      this.out(frame(0x8f, new Uint8Array([0, ...[24, 16, 8, 0].map(s => (crc >>> s) & 0xff)])));
    }
    this.bin = null;
    setTimeout(() => this.out('[PicoModem] done\r\n> '), 20);
  }
}

const dev = new FakeMidori();
const lines = [];
const link = new MidoriLink({ onLine: l => lines.push(l) });
const ok = (cond, what) => { console.log(`${cond ? 'ok  ' : 'FAIL'} ${what}`); if (!cond) process.exitCode = 1; };

const info = await link.connect(dev);
ok(info.board === 'tab5' && info.mode === 'ui', `info: ${JSON.stringify(info)}`);
const root = await link.list('/');
ok(root.some(e => e.name === 'app.rb' && !e.dir && e.size === 11) && root.some(e => e.name === 'sd' && e.dir), `ls /: ${JSON.stringify(root)}`);
ok((await link.list('/plugins')).map(e => e.name).join() === 'p.rb', 'ls /plugins');
let err = null;
try { await link.list('/nope'); } catch (e) { err = e.message; }
ok(err === 'no such directory', `ls error: ${err}`);
ok(dec.decode(await link.readFile('/app.rb')) === 'puts "app"\n', 'read /app.rb');
const big = 'x = 1 # é\n'.repeat(150);   // over one chunk, multi-byte text
await link.writeFile('/sd/big.rb', big);
ok(dec.decode(dev.fs.get('/sd/big.rb')) === big, `write /sd/big.rb (${enc.encode(big).length} bytes)`);
ok(dec.decode(await link.readFile('/sd/big.rb')) === big, 'read it back');
await link.writeFile('/sd/empty.rb', '');
ok(dev.fs.get('/sd/empty.rb')?.length === 0, 'write an empty file');
err = null;
try { await link.readFile('/missing.rb'); } catch (e) { err = e.message; }
ok(/File not found/.test(err), `read error: ${err}`);
await new Promise(r => setTimeout(r, 50));
ok((await link.info()).mode === 'ui', 'console works after PicoModem');
await link.remove('/app.rb');
ok(!dev.fs.has('/app.rb'), 'rm /app.rb');
ok(lines.some(l => l === 'I (123) noise: a log line'), 'other lines reach onLine');

// A late answer is not taken for the next command's (the /sd listing that
// timed out answered "ls /plugins" once)
dev.fs.set('/sd/x.rb', enc.encode('1'));
link.listTimeoutMs = 300;
// ls /sd answers at 400 ms, after the client gave up (300 ms) and while it
// waits for ls /plugins, which answers at 300 + 150 ms
dev.delay = { '/sd': 400, '/plugins': 150 };
err = null;
try { await link.list('/sd'); } catch (e) { err = e.message; }
ok(/答えがありません/.test(err), `a slow ls /sd times out: ${err}`);
const pl = await link.list('/plugins');     // the late /sd answer arrives while this waits
ok(pl.map(e => e.name).join() === 'p.rb', `ls /plugins after the late /sd answer: ${pl.map(e => e.name)}`);
link.listTimeoutMs = 30000;
// mv: a plugin to the flash
dev.fs.set('/sd/plugins/q.rb', enc.encode('# q\n'));
await link.move('/sd/plugins/q.rb', '/plugins/q.rb');
ok(dev.fs.has('/plugins/q.rb') && !dev.fs.has('/sd/plugins/q.rb'), 'mv /sd/plugins/q.rb /plugins/q.rb');
err = null;
try { await link.move('/sd/plugins/none.rb', '/plugins/none.rb'); } catch (e) { err = e.message; }
ok(err === 'no such file', `mv error: ${err}`);
dev.proto = 2;
await link.info();
err = null;
try { await link.move('/plugins/q.rb', '/sd/plugins/q.rb'); } catch (e) { err = e.message; }
ok(/ファームウェアが古く/.test(err || ''), 'mv needs proto 3');

// Old firmware (proto 1) is named rather than timed out on
dev.proto = 1;
await link.info();
err = null;
try { await link.list('/sd'); } catch (e) { err = e.message; }
ok(/ファームウェアが古く/.test(err || ''), `old firmware: ${err}`);
await link.disconnect();
process.exit(process.exitCode || 0);
