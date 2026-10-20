// CI check: the web builder must produce the same patched image as build.py (and refuse what build.py refuses).
// Usage: node site/test.mjs   (needs firmware/R039/img.bin, build/recommended from ./build.py recommended and
// build/all from ./build.py -n all P1 P3 P5 P6 P7 P8: every user patch, including one that grows the image)
import { readFileSync, readdirSync } from 'node:fs';
import { unpackStock, parsePatch, applyPatches, pack, sha256, PRESETS } from './builder.js';

const patch = (id) => {
  const f = readdirSync('patches').find((n) => n.startsWith(id + '-') && n.endsWith('.patch'));
  return parsePatch(id, readFileSync('patches/' + f, 'utf8'));
};
const raw = await unpackStock(new Uint8Array(readFileSync('firmware/R039/img.bin')));
const built = (name) => readFileSync((process.env.BUILD_DIR || 'build') + `/${name}/MANIFEST.txt`, 'utf8').match(/IMG \(raw\).*sha256 (\w+)/)[1];
const all = await applyPatches(raw, ['P1', 'P3', 'P5', 'P6', 'P7', 'P8'].map(patch));
await pack(all);
if ((await sha256(all)) !== built('all')) throw new Error('web builder image (all patches) differs from build.py');
const out = await applyPatches(raw, PRESETS.recommended.map(patch));
const img = await pack(out);
const want = built('recommended');
if ((await sha256(out)) !== want) throw new Error('web builder image differs from build.py');
let refused = false;
try { await applyPatches(out, [patch('P1')]); } catch { refused = true; }   // already patched -> must refuse
if (!refused) throw new Error('applying a patch twice was not refused');
console.log(`OK: web build of "recommended" == build.py (raw sha256 ${want.slice(0, 12)}…), IMG.bin ${img.length} B`);
