#!/usr/bin/env python3
"""Build a flashable Rider S510 IMG.bin from the stock R039 image + patch files. Never modifies firmware/R039.

  ./build.py vanilla                 package the untouched firmware (normal way back to stock)
  ./build.py recommended             the recommended set (PRESETS below) -> build/recommended/
  ./build.py [-n NAME] P1 [P3 ...]   apply patches/<id>-*.patch in order -> build/NAME/

A package is IMG.bin + update.ini (the unit only checks that update.ini exists; see docs/how-it-works.md).
Checks the unit makes, all reproduced and re-verified here:
  IMG.raw : last 4 bytes = sum(all other bytes) mod 2^32, LE   (checked after unzip and after iNAND read-back)
  IMG.bin : gzip member + zero pad, last 4 bytes = same additive sum over the rest, total a multiple of 512
            (checked before unzip). Size must be >= 0x100001.
Patch file format (one change per line, '#' comments). Patches carry NO original firmware bytes: what is
being replaced is identified by a hash; the vanilla image itself is identified by SHA256SUMS.
  desc: ...           source: <where the change is documented>
  0xADDR  sha256:<first 16 hex of sha256(original bytes)>  <new hex bytes>
  string  0xBLOCK  0xINDEX  sha256:<16 hex of the original UTF-16LE text>  room=<bytes>  "new text"
    (Embedded Wizard string block, see tools/ewstr.py: the block is decoded from the user's image, the text at
     INDEX is replaced by one of the same length, and the block is re-encoded; it must fit in <room> bytes,
     the space up to the next block, which must be zero padding in vanilla)
"""
import hashlib, os, re, shlex, shutil, sys, zlib

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import ewstr  # noqa: E402

VAN, PATCHES, OUT = os.path.join(ROOT, 'firmware', 'R039'), os.path.join(ROOT, 'patches'), os.path.join(ROOT, 'build')
SUMS = os.path.join(ROOT, 'firmware', 'SHA256SUMS')
BASE = 0xd0000000
GZ_HDR = b'\x1f\x8b\x08\x08\xc4\x38\xb2\x6a\x00\x0bIMG.bin\x00'   # vanilla header (FNAME, same mtime/OS)
PRESETS = {'recommended': ['P1', 'P3', 'P5', 'P6', 'P7']}     # also offered by the web builder (site/)
PRESETS['workout'] = PRESETS['recommended']               # its old name


def die(msg):
    sys.exit('build.py: ' + msg)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def add_sum(b):
    return sum(b) & 0xffffffff


def verify_vanilla():
    for line in open(SUMS):
        h, name = line.split()
        path = os.path.join(VAN, name)
        if not os.path.isfile(path):
            if name == 'img.bin':
                continue                                  # optional: only IMG.raw is patched
            die(f'{path} missing; run tools/fetch_firmware.py first')
        if sha(open(path, 'rb').read()) != h:
            die(f'{path} does not match firmware/SHA256SUMS; these patches are for stock R039 only; stop')


# The bootloader copies 6 MB from the OS area to SDRAM 0xd0000000 and the firmware's own data starts at 0xd0600000
# (lowest SDRAM address its code uses), so new code may also go after the stock image, up to there. The new space
# counts as zeros (for the sha256 checks) and the additive-sum trailer moves to the new end.
GROW_END = 0xd0600000


def grow(raw, end):
    """Make room for a patch ending at image offset end (before the trailer)."""
    if end + 4 > len(raw):
        if BASE + end + 4 > GROW_END:
            die(f'0x{BASE + end:08x}: past 0x{GROW_END:08x}, where the firmware keeps its data')
        raw.extend(bytes((end + 4 + 15) // 16 * 16 - len(raw)))


def h16(b):
    return hashlib.sha256(b).hexdigest()[:16]


def load_patch(pid):
    hits = [f for f in sorted(os.listdir(PATCHES)) if f.endswith('.patch') and (f == pid or f.startswith(pid + '-'))]
    if len(hits) != 1:
        die(f'patch id {pid!r}: expected exactly one patches/{pid}-*.patch, found {hits}')
    meta, edits = {}, []
    for n, line in enumerate(open(os.path.join(PATCHES, hits[0])), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        m = re.match(r'(\w+):\s*(.*)', line)
        if m and not line.startswith(('0x', 'string')):
            meta[m[1]] = m[2]
            continue
        try:
            f = shlex.split(line)
            if f[0] == 'string':
                assert f[3].startswith('sha256:') and f[4].startswith('room=') and len(f) == 6
                edits.append(('string', int(f[1], 16), int(f[2], 16), f[3][7:], int(f[4][5:], 0), f[5]))
            else:
                assert f[1].startswith('sha256:') and len(f) == 3
                edits.append(('bytes', int(f[0], 16), f[1][7:], bytes.fromhex(f[2])))
        except (ValueError, AssertionError, IndexError):
            die(f'{hits[0]}:{n}: expected "0xADDR sha256:H newhex" or "string 0xBLOCK 0xIDX sha256:H room=N \"text\""')
    if 'desc' not in meta or 'source' not in meta or not edits:
        die(f'{hits[0]}: needs desc:, source: and at least one change')
    return hits[0], meta, edits


def string_edit(fname, raw, block, idx, want, room, text):
    """Re-encode one Embedded Wizard string block with the text at idx replaced (same length)."""
    o = block - BASE
    data, _, used = ewstr.decode(bytes(raw), o)
    t = data.decode('utf-16-le')
    end = t.index('\0', idx)
    if h16(t[idx:end].encode('utf-16-le')) != want:
        die(f'{fname}: string 0x{block:08x}+0x{idx:x} does not match sha256:{want}')
    if len(text) != end - idx:
        die(f'{fname}: new text must keep the length ({end - idx} chars) so other string indexes stay valid')
    if any(raw[o + used:o + room]):
        die(f'{fname}: room={room} reaches past zero padding after block 0x{block:08x}')
    enc = ewstr.encode((t[:idx] + text + t[end:]).encode('utf-16-le'))
    if len(enc) > room:
        die(f'{fname}: re-encoded block is {len(enc)} B, only {room} B available')
    assert ewstr.decode(enc, 0)[0].decode('utf-16-le') == t[:idx] + text + t[end:]
    return block, enc + bytes(room - len(enc)), f'  (string block, text -> {text!r}, {used} -> {len(enc)} B)'


def package(img_bin, raw, name, manifest):
    d = os.path.join(OUT, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    open(os.path.join(d, 'IMG.bin'), 'wb').write(img_bin)
    ver = raw[0x94:0x98].decode()
    open(os.path.join(d, 'update.ini'), 'w', newline='\r\n').write(
        f'[OS]\nName=IMG.bin\nVersion={ver}\nSize={len(img_bin)}\n')
    manifest += [f'IMG.bin     {len(img_bin)} B  sha256 {sha(img_bin)}',
                 f'IMG (raw)   {len(raw)} B  sha256 {sha(raw)}  trailer {raw[-4:].hex()}']
    open(os.path.join(d, 'MANIFEST.txt'), 'w').write('\n'.join(manifest) + '\n')
    print('\n'.join(manifest), f'\n-> {d}')
    return d


def pack(raw):
    """IMG.raw -> IMG.bin: gzip member + zero pad to 512 B + additive sum, re-checked like the unit does."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    gz = GZ_HDR + c.compress(raw) + c.flush() + (zlib.crc32(raw).to_bytes(4, 'little') + len(raw).to_bytes(4, 'little'))
    body = gz + b'\0' * ((-(len(gz) + 4)) % 512)
    img_bin = body + add_sum(body).to_bytes(4, 'little')
    assert len(img_bin) % 512 == 0 and len(img_bin) >= 0x100001
    assert add_sum(img_bin[:-4]) == int.from_bytes(img_bin[-4:], 'little')
    assert zlib.decompressobj(16 + 15).decompress(img_bin) == raw
    assert add_sum(raw[:-4]) == int.from_bytes(raw[-4:], 'little')
    assert raw[0x94:0x98] == b'R039'
    return img_bin


def resolve(argv):
    """Command line -> (package name, patch ids). 'vanilla' -> ('vanilla', [])."""
    if argv == ['vanilla']:
        return 'vanilla', []
    if len(argv) == 1 and argv[0] in PRESETS:
        return argv[0], PRESETS[argv[0]]
    name = None
    if argv[:1] == ['-n']:
        name, argv = argv[1], argv[2:]
    if not argv:
        die(__doc__)
    return name or '+'.join(argv), argv


def main(argv):
    """Build and return the package directory."""
    verify_vanilla()
    name, argv = resolve(argv)
    if name == 'vanilla':
        raw = open(os.path.join(VAN, 'IMG.raw'), 'rb').read()
        img = os.path.join(VAN, 'img.bin')
        if os.path.isfile(img):
            return package(open(img, 'rb').read(), raw, name, ['vanilla R039, Bryton\'s img.bin unchanged'])
        return package(pack(raw), raw, name, ['vanilla R039, repacked from IMG.raw (no img.bin)'])
    raw, manifest, _ = apply(argv)
    return package(pack(raw), raw, name, manifest)


def apply(pids):
    """The patched raw image, its manifest lines, and {patch file: [(address, new bytes)]}."""
    raw = bytearray(open(os.path.join(VAN, 'IMG.raw'), 'rb').read())
    van = bytes(raw)
    owned, manifest, changes = {}, ['vanilla R039 sha256 ' + sha(van)], {}
    for pid in pids:
        fname, meta, edits = load_patch(pid)
        manifest.append(f'{fname}: {meta["desc"]}  [{meta["source"]}]')
        for e in edits:
            if e[0] == 'string':
                a, new, note = string_edit(fname, raw, *e[1:])
            else:
                _, a, want, new = e
                o = a - BASE
                if o >= len(van):
                    grow(raw, o + len(new))
                if 0 <= o <= len(raw) - 4 - len(new) and h16(raw[o:o + len(new)]) != want:
                    die(f'{fname}: 0x{a:08x} original bytes do not match sha256:{want}; wrong firmware/addr, '
                        f'or already patched?')
                note = ''
            o = a - BASE
            if not (0 <= o and o + len(new) <= len(raw) - 4):
                die(f'{fname}: 0x{a:08x} outside the image')
            for i in range(o, o + len(new)):
                if i in owned:
                    die(f'{fname}: 0x{a:08x} overlaps {owned[i]}')
                owned[i] = fname
            raw[o:o + len(new)] = new
            changes.setdefault(fname, []).append((a, bytes(new)))
            manifest.append(f'  0x{a:08x}  {len(new)} B -> {new.hex()}{note}')
    raw[-4:] = add_sum(raw[:-4]).to_bytes(4, 'little')
    raw = bytes(raw)
    diff = [i for i in range(len(raw) - 4) if raw[i] != (van[i] if i < len(van) else 0)]
    if any(i not in owned for i in diff):
        die('bytes changed outside the declared patches')   # cannot happen; belt and braces
    return raw, manifest, changes


if __name__ == '__main__':
    main(sys.argv[1:])
