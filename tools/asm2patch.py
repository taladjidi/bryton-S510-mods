#!/usr/bin/env python3
"""Assemble a code patch (patches/<id>-*.s) and print its lines for the .patch file.

  python3 tools/asm2patch.py patches/P5-centre-power-mode.s

The .s file declares where its code goes and which firmware instructions become calls to it:
  .equ CAVE, 0xd03d69d0                   load address of the code (unused space in the image)
  @hook 0xd01ab3fe pick_live  comment     replace the 4-byte instruction at 0xd01ab3fe by "bl pick_live"
Branch targets in the firmware are written relative to a label at the start of the code (see P5), so no
linker is needed; any relocation left in the object file is an error. Needs clang (Xcode or LLVM) and stock
firmware/R039/IMG.raw for the hashes. Check the result in Ghidra with ghidra/scripts/PatchCheck.java.
"""
import hashlib, os, re, struct, subprocess, sys, tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BASE = 0xd0000000


def elf(path):
    """(.text bytes, {symbol: offset}) of a relocatable ELF32 object."""
    o = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', o, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', o, 0x2e)
    sh = [struct.unpack_from('<IIIIIIIIII', o, shoff + i * shentsize) for i in range(shnum)]
    name = lambda tab, off: o[tab + off:o.index(b'\0', tab + off)].decode()
    names = {name(sh[shstrndx][4], s[0]): s for s in sh}
    if any(n.startswith('.rel') for n in names):
        sys.exit('asm2patch: the object has relocations; write firmware targets relative to the start label')
    text = names['.text']
    symtab, strtab = names['.symtab'], names['.strtab']
    syms = {}
    for i in range(symtab[5] // 16):
        st_name, st_value, _, _, _, st_shndx = struct.unpack_from('<IIIBBH', o, symtab[4] + 16 * i)
        if st_name and st_shndx == sh.index(text):
            syms[name(strtab[4], st_name)] = st_value & ~1
    return o[text[4]:text[4] + text[5]], syms


def bl(src, dst):
    off = dst - (src + 4)
    assert -(1 << 24) <= off < (1 << 24) and off % 2 == 0, 'bl out of range'
    s, i1, i2 = (off >> 24) & 1, (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (1 - i1) ^ s, (1 - i2) ^ s
    return struct.pack('<HH', 0xf000 | (s << 10) | ((off >> 12) & 0x3ff),
                       0xd000 | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7ff))


def main(src):
    text_src = open(src).read()
    cave = int(re.search(r'^\s*\.equ\s+CAVE\s*,\s*(0x[0-9a-fA-F]+)', text_src, re.M)[1], 16)
    hooks = re.findall(r'^@hook\s+(0x[0-9a-fA-F]+)\s+(\w+)\s*(.*)$', text_src, re.M)
    with tempfile.TemporaryDirectory() as d:
        obj = os.path.join(d, 'cave.o')
        subprocess.run(['clang', '--target=thumbv7em-none-eabi', '-mcpu=cortex-m7', '-c', src, '-o', obj], check=True)
        code, syms = elf(obj)
    raw = open(os.path.join(ROOT, 'firmware', 'R039', 'IMG.raw'), 'rb').read()
    h = lambda a, n: hashlib.sha256(raw[a - BASE:a - BASE + n]).hexdigest()[:16]
    if any(raw[cave - BASE:cave - BASE + len(code)]):
        sys.exit(f'asm2patch: 0x{cave:08x}..+{len(code)} is not zero padding in stock R039')
    for addr, label, comment in hooks:
        a = int(addr, 16)
        print(f'0x{a:08x}  sha256:{h(a, 4)}  {bl(a, cave + syms[label]).hex()}  # {comment or "bl " + label}'.rstrip())
    print(f'0x{cave:08x}  sha256:{h(cave, len(code))}  {code.hex()}  # code: {os.path.basename(src)} ({len(code)} B, '
          f'ends 0x{cave + len(code):08x})')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
