// Browser (and Node >= 18) port of build.py for byte patches: download stock R039 from Bryton, check it,
// apply patches, repack, and re-check everything the unit checks. Runs entirely on the user's machine.
// String patches (T1) are not supported here; use build.py for those.

export const FIRMWARE_URL = 'https://corp.brytonsport.com/device/riders510/img.bin';
export const SHA_IMG = '90db299247f4f201d8feb5230d14ab35a00bccd6d80f6602807e9ef1a94eb786';   // firmware/SHA256SUMS
export const SHA_RAW = 'c7f5e7140ac6ca1010e58407b627fa6e4483e714857690409675725ae9043a4e';
export const PRESETS = { workout: ['P1', 'P3', 'P4', 'P5'] };                                      // = build.py PRESETS
const MEMBER_LEN = 2634848;                  // gzip member inside stock img.bin (then zero pad + 4-byte sum)
const RAW_LEN = 4026368;
const BASE = 0xd0000000;
const GZ_HDR = [0x1f, 0x8b, 8, 8, 0xc4, 0x38, 0xb2, 0x6a, 0, 0x0b, ...new TextEncoder().encode('IMG.bin'), 0];

const hex = (buf) => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
export const sha256 = async (bytes) => hex(await crypto.subtle.digest('SHA-256', bytes));
const pipe = async (bytes, stream) => new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(stream)).arrayBuffer());

function addSum(bytes, end = bytes.length) {
  let s = 0;
  for (let i = 0; i < end; i++) s = (s + bytes[i]) >>> 0;
  return s;
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(bytes) {
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

const le32 = (v) => [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, v >>> 24];
const readLe32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

/** Check stock img.bin and return its decompressed image (IMG.raw). */
export async function unpackStock(img) {
  if ((await sha256(img)) !== SHA_IMG)
    throw new Error('This is not stock R039 (Bryton may have published a newer version). The patches only apply to R039.');
  const raw = await pipe(img.subarray(0, MEMBER_LEN), new DecompressionStream('gzip'));
  if ((await sha256(raw)) !== SHA_RAW) throw new Error('Decompressed firmware does not match R039.');
  return raw;
}

/** Parse a .patch file (same format as build.py). */
export function parsePatch(id, text) {
  const meta = {}, edits = [];
  for (const full of text.split('\n')) {
    const line = full.split('#')[0].trim();
    if (!line) continue;
    const m = line.match(/^(\w+):\s*(.*)$/);
    if (m && !line.startsWith('0x') && !line.startsWith('string')) { meta[m[1]] = m[2]; continue; }
    const f = line.split(/\s+/);
    if (f[0] === 'string') { meta.unsupported = 'text patch (use build.py)'; continue; }
    if (f.length !== 3 || !f[1].startsWith('sha256:') || !/^[0-9a-f]+$/i.test(f[2]) || f[2].length % 2)
      throw new Error(`${id}: bad line "${line}"`);
    edits.push({ addr: parseInt(f[0], 16), hash: f[1].slice(7), bytes: Uint8Array.from(f[2].match(/../g), (h) => parseInt(h, 16)) });
  }
  return { id, ...meta, edits };
}

/** Apply patches to a copy of raw, with the same refusals as build.py. */
export async function applyPatches(raw, patches) {
  const out = raw.slice(), owner = new Map();
  for (const p of patches) {
    if (p.unsupported) throw new Error(`${p.id}: ${p.unsupported}`);
    for (const e of p.edits) {
      const o = e.addr - BASE;
      if (o < 0 || o + e.bytes.length > out.length - 4) throw new Error(`${p.id}: 0x${e.addr.toString(16)} outside the image`);
      if ((await sha256(raw.subarray(o, o + e.bytes.length))).slice(0, 16) !== e.hash)
        throw new Error(`${p.id}: original bytes at 0x${e.addr.toString(16)} do not match; wrong firmware?`);
      for (let i = o; i < o + e.bytes.length; i++) {
        if (owner.has(i)) throw new Error(`${p.id}: overlaps ${owner.get(i)}`);
        owner.set(i, p.id);
      }
      out.set(e.bytes, o);
    }
  }
  out.set(le32(addSum(out, out.length - 4)), out.length - 4);
  for (let i = 0; i < out.length - 4; i++)
    if (out[i] !== raw[i] && !owner.has(i)) throw new Error('bytes changed outside the declared patches');
  return out;
}

/** IMG.raw -> IMG.bin, re-checked exactly like build.py / the unit. */
export async function pack(raw) {
  const deflated = await pipe(raw, new CompressionStream('deflate-raw'));
  const gzLen = GZ_HDR.length + deflated.length + 8;
  const total = Math.ceil((gzLen + 4) / 512) * 512;
  const img = new Uint8Array(total);
  img.set(GZ_HDR, 0);
  img.set(deflated, GZ_HDR.length);
  img.set([...le32(crc32(raw)), ...le32(raw.length)], GZ_HDR.length + deflated.length);
  img.set(le32(addSum(img, total - 4)), total - 4);

  const back = await pipe(img.subarray(0, gzLen), new DecompressionStream('gzip'));
  const same = back.length === raw.length && back.every((b, i) => b === raw[i]);
  if (!(total % 512 === 0 && total >= 0x100001 && same && addSum(raw, raw.length - 4) === readLe32(raw, raw.length - 4)
        && new TextDecoder().decode(raw.subarray(0x94, 0x98)) === 'R039'))
    throw new Error('Built package failed its own checks; not saving it.');
  return img;
}

export const updateIni = (img) => `[OS]\r\nName=IMG.bin\r\nVersion=R039\r\nSize=${img.length}\r\n`;

/** Download stock firmware from Bryton with progress callback (fraction 0..1). */
export async function download(onProgress = () => {}) {
  const r = await fetch(FIRMWARE_URL);
  if (!r.ok) throw new Error(`Bryton's server answered ${r.status}`);
  const total = +r.headers.get('Content-Length') || 0, parts = [];
  let got = 0;
  for (const reader = r.body.getReader(); ;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value);
    got += value.length;
    if (total) onProgress(got / total);
  }
  return new Uint8Array(await new Blob(parts).arrayBuffer());
}

export { RAW_LEN };
