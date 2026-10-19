#!/usr/bin/env python3
"""Download the stock Rider S510 firmware from Bryton's own update server and check it is R039.

  tools/fetch_firmware.py [--dest DIR]        default DIR = firmware/R039

Gets img.bin (what the Bryton Active app downloads), writes IMG.raw (= img.bin decompressed, what the patches
apply to) and checks both against firmware/SHA256SUMS. If Bryton has published a newer release, the hashes
will not match: the patches here are for R039 only and must not be applied to anything else.
"""
import hashlib, os, sys, urllib.request, zlib

from tqdm import tqdm

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
    print(get('release.ini').decode(errors='replace').split('[BootLoader]')[0].strip())
    img = get('img.bin')
    raw = zlib.decompressobj(16 + 15).decompress(img)       # img.bin = gzip + zero padding + 4-byte sum
    want = dict(reversed(line.split()) for line in open(os.path.join(ROOT, 'firmware', 'SHA256SUMS')))
    os.makedirs(dest, exist_ok=True)
    ok = True
    for name, data in (('img.bin', img), ('IMG.raw', raw)):
        open(os.path.join(dest, name), 'wb').write(data)
        good = hashlib.sha256(data).hexdigest() == want[name]
        ok &= good
        print(f'{name:8} {len(data):>8} B  {"OK (stock R039)" if good else "DOES NOT MATCH R039"}')
    if not ok:
        sys.exit('Not the R039 these patches were made for — do not use them on this firmware.')
    print(f'saved to {dest}')


if __name__ == '__main__':
    main(sys.argv[1:])
