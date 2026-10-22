#!/usr/bin/env python3
"""Build a code patch (patches/<id>-*.s or *.c) and print its lines for the .patch file.

  python3 tools/asm2patch.py patches/P5-centre-power-mode.s
  python3 tools/asm2patch.py --syms patches/P7-media-remote.c     also list where each function landed

The source declares, in comments ('@' in assembly, '//' in C):
  @cave 0xd03d69d0                        load address of the new code (unused space in the image)
  @sym  Ride_UpdateTss 0xd0281854         a firmware function or variable the code refers to by name
  @hook 0xd01ab3fe pick_live  comment     replace the 4-byte instruction at 0xd01ab3fe by "bl pick_live"
  @jump 0xd01a4a32 label  comment         same with "b.w label" (to replace a tail call)
  @word 0xd029e634 label  comment         replace the 4-byte word at 0xd029e634 by label's address (Thumb bit set
                                          for a function): a function pointer in a literal pool
  @bytes 0xd017a658 ff29  comment         replace bytes as given (e.g. one instruction's immediate)
The cave may lie after the stock image (build.py grows it, see GROW_END there); that space counts as zeros.
(assembly may also use ".equ CAVE, 0x..." and firmware targets written relative to a start label, as P5 does).
The object is linked here: sections are placed one after the other from the cave address and the few
relocation types clang emits are resolved (absolute words, bl, b.w). Anything else is an error.
Needs clang (Xcode or LLVM) and stock firmware/R039/IMG.raw for the hashes. Check the result in Ghidra with
ghidra/scripts/PatchCheck.java.
"""
import hashlib, os, re, struct, subprocess, sys, tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BASE = 0xd0000000
CLANG = ['clang', '--target=thumbv7em-none-eabihf', '-mcpu=cortex-m7', '-mfloat-abi=hard', '-mfpu=fpv5-d16']
CFLAGS = ['-Oz', '-ffreestanding', '-fno-builtin', '-fno-pic', '-mno-movt', '-fno-unwind-tables',
          '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-function-sections', '-fno-data-sections',
          '-std=c11', '-Wall', '-Wextra', '-Werror']
R_ARM_ABS32, R_ARM_THM_CALL, R_ARM_THM_JUMP24 = 2, 10, 30


def branch(src, dst, link):
    """Thumb-2 BL (link) or B.W from src to dst."""
    off = dst - (src + 4)
    assert -(1 << 24) <= off < (1 << 24) and off % 2 == 0, f'branch 0x{src:08x} -> 0x{dst:08x} out of range'
    s, i1, i2 = (off >> 24) & 1, (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (1 - i1) ^ s, (1 - i2) ^ s
    return struct.pack('<HH', 0xf000 | (s << 10) | ((off >> 12) & 0x3ff),
                       (0xd000 if link else 0x9000) | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7ff))


def branch_addend(ins):
    """Implicit addend of a REL Thumb-2 BL / B.W (usually -4)."""
    h1, h2 = struct.unpack('<HH', ins)
    s = (h1 >> 10) & 1
    i1, i2 = 1 - (((h2 >> 13) & 1) ^ s), 1 - (((h2 >> 11) & 1) ^ s)
    off = (s << 24) | (i1 << 23) | (i2 << 22) | ((h1 & 0x3ff) << 12) | ((h2 & 0x7ff) << 1)
    return off - (1 << 25) if s else off


def link(obj, cave, fw_syms):
    """Place the allocatable sections from cave, resolve relocations. Returns (bytes, {symbol: address})."""
    o = open(obj, 'rb').read()
    shoff, = struct.unpack_from('<I', o, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', o, 0x2e)
    sh = [dict(zip(('name', 'type', 'flags', 'addr', 'off', 'size', 'link', 'info', 'align', 'entsize'),
                   struct.unpack_from('<IIIIIIIIII', o, shoff + i * shentsize))) for i in range(shnum)]
    cstr = lambda tab, off: o[tab + off:o.index(b'\0', tab + off)].decode()
    for s in sh:
        s['name'] = cstr(sh[shstrndx]['off'], s['name'])
    keep = [i for i, s in enumerate(sh) if s['flags'] & 2 and s['name'] != '.ARM.exidx']   # SHF_ALLOC
    keep.sort(key=lambda i: (not sh[i]['flags'] & 4, sh[i]['type'] == 8))   # code, then data, then bss
    blob, where = bytearray(), {}
    for i in keep:
        blob += bytes((-(cave + len(blob))) % max(sh[i]['align'], 1))
        where[i] = cave + len(blob)
        blob += bytes(sh[i]['size']) if sh[i]['type'] == 8 else o[sh[i]['off']:sh[i]['off'] + sh[i]['size']]
    symtab = next(s for s in sh if s['type'] == 2)
    syms, syms_und, addr_of, funcs = [], [], {}, set()
    for k in range(symtab['size'] // 16):
        st_name, value, _, info, _, shndx = struct.unpack_from('<IIIBBH', o, symtab['off'] + 16 * k)
        name = cstr(sh[symtab['link']]['off'], st_name) if st_name else ''
        if shndx in where:
            a = where[shndx] + value
        elif shndx == 0 and name:
            if name not in fw_syms:
                sys.exit(f'asm2patch: {name} is not defined: add "@sym {name} 0xADDR"')
            a = fw_syms[name]
        else:
            a = None
        syms.append(a)
        syms_und.append(shndx == 0 and bool(name))
        if name and a is not None:
            addr_of[name] = a
            if info & 15 == 2 and shndx in where:                        # STT_FUNC placed here
                funcs.add(name)
    for rs in sh:
        if rs['type'] != 9 or rs['info'] not in where:         # SHT_REL for a placed section
            continue
        tgt = where[rs['info']]
        for k in range(rs['size'] // 8):
            r_off, r_info = struct.unpack_from('<II', o, rs['off'] + 8 * k)
            S, typ, P = syms[r_info >> 8], r_info & 0xff, tgt + r_off
            at = P - cave
            if S is None:
                sys.exit(f'asm2patch: relocation against an unplaced symbol at 0x{P:08x}')
            if typ == R_ARM_ABS32:
                # A firmware function taken by address (clang loads it from a literal pool and calls with blx)
                # needs its Thumb bit; @sym gives even addresses, and undefined symbols carry no type to tell.
                # ponytail: every @sym in the image is code; one naming data there would need a type tag
                if syms_und[r_info >> 8] and BASE <= S < BASE + 0x800000:
                    S |= 1
                blob[at:at + 4] = struct.pack('<I', (S + struct.unpack_from('<I', blob, at)[0]) & 0xffffffff)
            elif typ in (R_ARM_THM_CALL, R_ARM_THM_JUMP24):
                is_bl = struct.unpack_from('<H', blob, at + 2)[0] & 0x5000 == 0x5000
                blob[at:at + 4] = branch(P, ((S & ~1) + branch_addend(blob[at:at + 4]) + 4), is_bl)
            else:
                sys.exit(f'asm2patch: unsupported relocation type {typ} at 0x{P:08x}')
    return bytes(blob), addr_of, funcs


def main(src, show_syms=False):
    text = open(src).read()
    tag = lambda t: re.findall(rf'^\s*(?://\s*)?@{t}\s+(.*)$', text, re.M)
    cave = tag('cave') or re.findall(r'^\s*\.equ\s+CAVE\s*,\s*(0x[0-9a-fA-F]+)', text, re.M)
    cave = int(cave[0].split()[0], 16)
    fw_syms = {f[0]: int(f[1], 16) for f in (l.split() for l in tag('sym'))}
    hooks = [['bl'] + l.split(None, 2) for l in tag('hook')] + [['b.w'] + l.split(None, 2) for l in tag('jump')] + \
            [['word'] + l.split(None, 2) for l in tag('word')]
    with tempfile.TemporaryDirectory() as d:
        obj = os.path.join(d, 'patch.o')
        extra = CFLAGS if src.endswith('.c') else []
        subprocess.run(CLANG + extra + ['-c', src, '-o', obj], check=True)
        code, addr_of, funcs = link(obj, cave, fw_syms)
    raw = open(os.path.join(ROOT, 'firmware', 'R039', 'IMG.raw'), 'rb').read()
    raw += bytes(max(0, cave - BASE + len(code) + 4 - len(raw)))      # past the stock end: zeros, as build.py
    h = lambda a, n: hashlib.sha256(raw[a - BASE:a - BASE + n]).hexdigest()[:16]
    if any(raw[cave - BASE:cave - BASE + len(code)]):
        sys.exit(f'asm2patch: 0x{cave:08x}..+{len(code)} is not zero padding in stock R039')
    for kind, addr, label, *comment in hooks:
        a = int(addr, 16)
        new = struct.pack('<I', addr_of[label]) if kind == 'word' else branch(a, addr_of[label] & ~1, kind == 'bl')
        print(f'0x{a:08x}  sha256:{h(a, 4)}  {new.hex()}  # {comment[0] if comment else kind + " " + label}')
    for addr, new, *comment in (l.split(None, 2) for l in tag('bytes')):
        a, new = int(addr, 16), bytes.fromhex(new)
        print(f'0x{a:08x}  sha256:{h(a, len(new))}  {new.hex()}  # {comment[0] if comment else "bytes"}')
    for name, a in sorted(addr_of.items(), key=lambda x: x[1]):
        if cave <= a < cave + len(code) and (name in funcs or show_syms):
            print(f'# sym 0x{a:08x} {name}')                         # for tools/patchsim.py (comment for build.py)
    print(f'0x{cave:08x}  sha256:{h(cave, len(code))}  {code.hex()}  # code: {os.path.basename(src)} '
          f'({len(code)} B, ends 0x{cave + len(code):08x})')


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--syms']
    if len(args) != 1:
        sys.exit(__doc__)
    main(args[0], '--syms' in sys.argv)
