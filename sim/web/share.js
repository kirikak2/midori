// Share links for the simulator (docs/WEB_SIMULATOR.md §15): the script in
// the URL's fragment, so it never reaches a server.
//
//   #code=<base64url(deflate-raw(UTF-8))>&board=tab5
//
// The same encoding comes from Node's zlib.deflateRawSync (tools/site_sim_links.rb
// makes the reference's "open in the simulator" links with Ruby's Zlib), so
// links can be made without a browser.

const b64url = bytes => {
  let bin = '';
  for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
};
const unb64url = s => {
  const b = atob(s.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((s.length + 3) % 4));
  return Uint8Array.from(b, c => c.charCodeAt(0));
};

async function pipe(bytes, stream) {
  const out = new Response(new Blob([bytes]).stream().pipeThrough(stream));
  return new Uint8Array(await out.arrayBuffer());
}

export async function encodeScript(text) {
  return b64url(await pipe(new TextEncoder().encode(text), new CompressionStream('deflate-raw')));
}

export async function decodeScript(code) {
  return new TextDecoder().decode(await pipe(unb64url(code), new DecompressionStream('deflate-raw')));
}

// The fragment of a share link
export async function shareFragment(text, board) {
  const p = new URLSearchParams();
  p.set('code', await encodeScript(text));
  if (board) p.set('board', board);
  return `#${p.toString()}`;
}

// { script, board } from location.hash, or null
export async function readFragment(hash = location.hash) {
  const p = new URLSearchParams(hash.replace(/^#/, ''));
  if (!p.has('code')) return null;
  return { script: await decodeScript(p.get('code')), board: p.get('board') || null };
}
