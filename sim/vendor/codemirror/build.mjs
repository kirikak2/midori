// Bundle src/editor.js into sim/web/vendor/codemirror.js (npm run build),
// with a notice naming every package in it and their licence.
import { build } from 'esbuild';
import fs from 'fs';
import path from 'path';

const out = '../../web/vendor/codemirror.js';
const result = await build({
  entryPoints: ['src/editor.js'], bundle: true, format: 'esm', minify: true,
  outfile: out, metafile: true, legalComments: 'none', write: false,
});
// The packages whose files went into the bundle
const pkgs = new Map();
for (const input of Object.keys(result.metafile.inputs)) {
  const m = /node_modules\/((?:@[^/]+\/)?[^/]+)\//.exec(input);
  if (!m || pkgs.has(m[1])) continue;
  const p = JSON.parse(fs.readFileSync(path.join('node_modules', m[1], 'package.json'), 'utf8'));
  pkgs.set(m[1], `${p.name}@${p.version} (${p.license})`);
}
const banner = `/*
 * CodeMirror 6 for the MIDoRI simulator (sim/vendor/codemirror: npm run build).
 * Bundled packages:
${[...pkgs.values()].sort().map(s => ` *   ${s}`).join('\n')}
 * MIT License. Copyright (C) 2018-2021 by Marijn Haverbeke <marijn@haverbeke.berlin>
 * and others (see each package's LICENSE).
 */
`;
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, banner + result.outputFiles[0].text);
console.log(`${out}: ${pkgs.size} packages, ${(fs.statSync(out).size / 1024).toFixed(0)} KB`);
