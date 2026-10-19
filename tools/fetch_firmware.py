#!/usr/bin/env python3
"""Download the stock Rider S510 firmware from Bryton's own update server and check it is R039.

  tools/fetch_firmware.py [--dest DIR]        default DIR = firmware/R039
  tools/fetch_firmware.py --from-readback FILE
      no download: use your own unit's firmware, read back in Engineer Mode with tools/iaprecover.py (for when
      Bryton no longer serves R039). Only IMG.raw is produced; that is all build.py needs.

Gets img.bin (what the Bryton Active app downloads), writes IMG.raw (= img.bin decompressed, what the patches
apply to) and checks both against firmware/SHA256SUMS. If Bryton has published a newer release, the hashes
will not match: the patches here are for R039 only and must not be applied to anything else.
"""
import hashlib, os, sys, urllib.request, zlib

from progress import tqdm

URL = 'https://corp.brytonsport.com/device/riders510/'
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def get(name):
    with urllib.request.urlopen(URL + name, timeout=60) as r:
        total = int(r.headers.get('Content-Length') or 0)
        out = bytearray()
        with tqdm(total=total, unit='B', unit_scale=True, desc=name) as bar:
            while chunk := r.read(65536):
                out += chunk
                bar.update(len(chunk))
    return bytes(out)


def main(args):
    dest = args[args.index('--dest') + 1] if '--dest' in args else os.path.join(ROOT, 'firmware', 'R039')
    want = dict(reversed(line.split()) for line in open(os.path.join(ROOT, 'firmware', 'SHA256SUMS')))
    if '--from-readback' in args:
        raw = open(args[args.index('--from-readback') + 1], 'rb').read()[:4026368]   # readback may be padded
        if hashlib.sha256(raw).hexdigest() != want['IMG.raw']:
            sys.exit('This readback is not stock R039; cannot be used as the base for the patches.')
        os.makedirs(dest, exist_ok=True)
        open(os.path.join(dest, 'IMG.raw'), 'wb').write(raw)
        return print(f'IMG.raw  OK (stock R039, from your unit) -> {dest}')
    print(get('release.ini').decode(errors='replace').split('[BootLoader]')[0].strip())
    img = get('img.bin')
    raw = zlib.decompressobj(16 + 15).decompress(img)       # img.bin = gzip + zero padding + 4-byte sum
    os.makedirs(dest, exist_ok=True)
    ok = True
    for name, data in (('img.bin', img), ('IMG.raw', raw)):
        open(os.path.join(dest, name), 'wb').write(data)
        good = hashlib.sha256(data).hexdigest() == want[name]
        ok &= good
        print(f'{name:8} {len(data):>8} B  {"OK (stock R039)" if good else "DOES NOT MATCH R039"}')
    if not ok:
        sys.exit('Not the R039 these patches were made for; do not use them on this firmware.')
    print(f'saved to {dest}')


if __name__ == '__main__':
    main(sys.argv[1:])
