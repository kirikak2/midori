// node sim/test/node_share.mjs
// share.js in Node (CompressionStream / DecompressionStream are global in Node 18+),
// and Node's zlib producing the same fragment the page reads
import zlib from 'zlib';
const { encodeScript, decodeScript, shareFragment, readFragment } = await import('../web/share.js');
const text = 'UI.knob(1, label: "カットオフ") { |v| p v }\n'.repeat(20);
const code = await encodeScript(text);
console.log('round trip:', (await decodeScript(code)) === text ? 'ok' : 'NG', code.length, 'chars for', text.length);
const z = zlib.deflateRawSync(Buffer.from(text)).toString('base64url');
console.log('zlib-made link reads:', (await decodeScript(z)) === text ? 'ok' : 'NG');
const f = await readFragment(await shareFragment(text, 'cores3'));
console.log('fragment:', f.script === text && f.board === 'cores3' ? 'ok' : 'NG');
