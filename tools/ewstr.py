#!/usr/bin/env python3
"""Embedded Wizard const-string blocks (R039): decode / encode the LZW used by EwDecompress 0xd015e9d8.
Block = u32 decompressed byte size, then LSB-first LZW codes (9..12 bit, 0x100 clear, 0x101 end, first free 0x102).
  ewstr.py 0xBLOCK            dump the UTF-16 strings of a block (index = char offset, as in {block, index} refs)
"""
import struct, sys

IMG = __file__.rsplit('/', 2)[0] + '/R039/IMG.raw'
BASE = 0xd0000000


def decode(data, off):
    size = struct.unpack_from('<I', data, off)[0]
    pos, bitbuf, nbits = off + 4, 0, 0
    width, nxt, prev, table, out = 9, 0x102, None, {}, bytearray()
    while True:
        while nbits < width:
            bitbuf |= data[pos] << nbits; pos += 1; nbits += 8
        code = bitbuf & ((1 << width) - 1); bitbuf >>= width; nbits -= width
        if code == 0x101:
            break
        if code == 0x100:
            width, nxt, prev, table = 9, 0x102, None, {}
            continue
        if code < 0x100:
            entry = bytes([code])
        elif code in table:
            entry = table[code]
        elif code == nxt and prev is not None:
            entry = prev + prev[:1]
        else:
            raise ValueError(f'bad code {code:#x}')
        out += entry
        if prev is not None:
            table[nxt] = prev + entry[:1]; nxt += 1
            if nxt > (1 << width) - 1 and width < 12:
                width += 1
        prev = entry
    return bytes(out), size, pos - off


def encode(raw):
    """Greedy LZW, same rules as decode(); returns u32 size + code stream (byte-exact with EW's compressor
    is checked by roundtrip_check())."""
    out, bitbuf, nbits = bytearray(struct.pack('<I', len(raw))), 0, 0
    def emit(code, width):
        nonlocal bitbuf, nbits
        bitbuf |= code << nbits; nbits += width
        while nbits >= 8:
            out.append(bitbuf & 0xff); bitbuf >>= 8; nbits -= 8
    width, nxt, table, w = 9, 0x102, {}, b''
    emit(0x100, 9)                      # EW streams start with a clear code
    for c in raw:
        wc = w + bytes([c])
        if len(wc) == 1 or wc in table:
            w = wc; continue
        emit(w[0] if len(w) == 1 else table[w], width)
        if nxt < 4096:
            table[wc] = nxt; nxt += 1
            if nxt > (1 << width) and width < 12:
                width += 1
        w = bytes([c])
    if w:
        emit(w[0] if len(w) == 1 else table[w], width)
    emit(0x101, width)
    if nbits:
        out.append(bitbuf & 0xff)
    return bytes(out)


if __name__ == '__main__':
    d = open(IMG, 'rb').read()
    out, size, used = decode(d, int(sys.argv[1], 16) - BASE)
    print(f'decompressed {len(out)} B (header says {size}), compressed {used} B')
    t = out.decode('utf-16-le', 'replace')
    i = 0
    for s in t.split('\0'):
        print(f'{i:#06x}  {s!r}'); i += len(s) + 1
