#!/usr/bin/env python3
"""Copy of a ride file that the Bryton app and server accept: developer data removed.

  python3 tools/fitstrip.py RIDE.fit [OUT.fit]      (default OUT: RIDE_bryton.fit next to it)

With P9 every record carries Alpha1 as a developer field. Bryton's server refuses such a ride (tested 2026-10-24,
docs/how-it-works.md "How a ride reaches the Bryton app"): the app downloads it, tells the unit it has it, then the
upload fails and the ride never shows. The original stays the file for Intervals.icu; this copy is for Bryton
(upload it on the Bryton Active website). It drops the developer_data_id / field_description messages and the
developer bytes of every message, and sets the header back to FIT protocol 1.0. A file without developer data
comes out byte for byte the same.
"""
import os, struct, sys

CRC_TABLE = [0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
             0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400]


def crc(data):
    """FIT CRC-16 (FIT protocol document, section 3.3.2)."""
    c = 0
    for x in data:
        for v in (x & 15, x >> 4):
            t = CRC_TABLE[c & 15]
            c = ((c >> 4) & 0x0fff) ^ t ^ CRC_TABLE[v]
    return c


def strip(b):
    hs, size = b[0], struct.unpack_from('<I', b, 4)[0]
    if b[8:12] != b'.FIT' or crc(b[:hs + size + 2]) != 0:
        sys.exit('not a FIT file, or its CRC is wrong')
    i, end, out, defs = hs, hs + size, bytearray(), {}
    while i < end:
        h = b[i]
        if h & 0x80:                                        # compressed timestamp header: data message
            loc = (h >> 5) & 3
        elif h & 0x40:                                      # definition message
            loc = h & 15
            e = '<' if b[i + 2] == 0 else '>'
            g, nf = struct.unpack_from(e + 'H', b, i + 3)[0], b[i + 5]
            fields = b[i + 6:i + 6 + 3 * nf]
            j, dev = i + 6 + 3 * nf, 0
            if h & 0x20:                                    # developer fields follow
                nd = b[j]
                dev = sum(b[j + 2 + 3 * k] for k in range(nd))
                j += 1 + 3 * nd
            keep = g not in (206, 207)                      # field_description, developer_data_id
            defs[loc] = (sum(fields[1::3]), dev, keep)
            if keep:
                out += bytes([h & ~0x20]) + b[i + 1:i + 6] + fields
            i = j
            continue
        else:
            loc = h & 15
        n, dev, keep = defs[loc]
        if keep:
            out += b[i:i + 1 + n]
        i += 1 + n + dev
    hdr = bytearray(b[:hs])
    if out == b[hs:end]:
        return bytes(b)                                     # nothing to strip
    hdr[1] = 0x10
    struct.pack_into('<I', hdr, 4, len(out))
    if hs >= 14:
        struct.pack_into('<H', hdr, 12, crc(hdr[:12]))
    f = bytes(hdr + out)
    return f + struct.pack('<H', crc(f))


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) == 3 else os.path.splitext(src)[0] + '_bryton.fit'
    b = open(src, 'rb').read()
    f = strip(b)
    open(dst, 'wb').write(f)
    print(f'{dst}: {len(f)} B (was {len(b)} B)')


if __name__ == '__main__':
    main()
