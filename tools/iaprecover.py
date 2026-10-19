#!/usr/bin/env python3
"""Rider S510 recovery through the BOOTLOADER USB mode: read back the OS area, or rewrite it with vanilla.

Run with the conda Python (it has tqdm): sudo "$(which python3)" iaprecover.py …
  sudo python3 iaprecover.py [--out FILE] [--bytes N]   read the OS area, compare with vanilla IMG.raw
  sudo python3 iaprecover.py --write-vanilla            write vanilla IMG.raw to the OS area, verify every chunk

Put the unit in bootloader USB mode first: USB plugged in, press all four buttons to reboot, keep holding Page
(lower right) until the screen shows "Engineer Mode" (docs/how-it-works.md).
Verified 2026-10-19: full 4 026 368 B readback == vanilla IMG.raw (sha256 c7f5e714…).

Protocol (recovered from the bootloader, see docs/how-it-works.md): plain USB mass storage (Bulk-Only Transport, EP OUT 0x01 /
IN 0x81). Opcode 0x28 READ(10) with CDB byte 6 = 0x0d ("read data from iNAND") returns 2 KB of the OS area:
LBA field k -> OS-area byte offset k*2048; transfer length must be 4 blocks (2048 B).
Opcode 0x2A WRITE(10) with byte 6 = 0x0c writes 2 KB the same way (bootloader FUN_0800932c case 0xc).
A failed sector write is only logged on the bootloader's debug UART (the CSW still says OK), so --write-vanilla
reads every chunk back and stops at the first mismatch. Only the SHA256SUMS-verified vanilla image can be
written; nothing else is ever sent.
Uses libusb through ctypes (no pyusb). sudo is needed so libusb can detach macOS's mass-storage driver.
"""
import ctypes, ctypes.util, hashlib, os, struct, sys

from tqdm import tqdm

VID = 0x0483                     # STMicroelectronics (as seen in normal USB mode); PID printed when found
EP_OUT, EP_IN = 0x01, 0x81
CHUNK = 2048
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
VANILLA = os.path.join(ROOT, 'firmware', 'R039', 'IMG.raw')

lib = ctypes.CDLL(ctypes.util.find_library('usb-1.0') or '/opt/homebrew/lib/libusb-1.0.dylib')
lib.libusb_get_device_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))]
lib.libusb_get_device_descriptor.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.libusb_open.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
lib.libusb_free_device_list.argtypes = [ctypes.c_void_p, ctypes.c_int]
for f in ('libusb_detach_kernel_driver', 'libusb_claim_interface', 'libusb_release_interface',
          'libusb_attach_kernel_driver', 'libusb_kernel_driver_active'):
    getattr(lib, f).argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.libusb_bulk_transfer.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_char_p, ctypes.c_int,
                                     ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
lib.libusb_close.argtypes = [ctypes.c_void_p]
lib.libusb_error_name.restype = ctypes.c_char_p


def check(rc, what):
    if rc < 0:
        sys.exit(f'{what}: {lib.libusb_error_name(rc).decode()}')
    return rc


def open_unit():
    check(lib.libusb_init(None), 'libusb_init')
    devs = ctypes.POINTER(ctypes.c_void_p)()
    n = check(lib.libusb_get_device_list(None, ctypes.byref(devs)), 'device list')
    found = []
    for i in range(n):
        desc = ctypes.create_string_buffer(18)
        lib.libusb_get_device_descriptor(devs[i], desc)
        vid, pid = struct.unpack_from('<HH', desc.raw, 8)
        if vid == VID:
            found.append((devs[i], pid))
    if len(found) != 1:
        sys.exit(f'expected exactly one USB device with VID {VID:04x}, found {[hex(p) for _, p in found]}')
    h = ctypes.c_void_p()
    check(lib.libusb_open(found[0][0], ctypes.byref(h)), 'open (run with sudo?)')
    lib.libusb_free_device_list(devs, 1)
    print(f'opened {VID:04x}:{found[0][1]:04x}')
    if lib.libusb_kernel_driver_active(h, 0) == 1:
        check(lib.libusb_detach_kernel_driver(h, 0), 'detach macOS driver (sudo? drive ejected?)')
    check(lib.libusb_claim_interface(h, 0), 'claim interface 0')
    return h


def bulk(h, ep, data_or_len, timeout=5000):
    n = ctypes.c_int()
    if ep & 0x80:
        buf = ctypes.create_string_buffer(data_or_len)
        check(lib.libusb_bulk_transfer(h, ep, buf, data_or_len, ctypes.byref(n), timeout), 'bulk in')
        return buf.raw[:n.value]
    check(lib.libusb_bulk_transfer(h, ep, data_or_len, len(data_or_len), ctypes.byref(n), timeout), 'bulk out')


def command(h, k, tag, write=None):
    """One 2 KB vendor transfer: READ(10)/0x0d, or WRITE(10)/0x0c when write is the 2 KB to send."""
    op, sub, flags = (0x2a, 0x0c, 0x00) if write else (0x28, 0x0d, 0x80)
    cdb = struct.pack('>BBIBH', op, 0, k, sub, CHUNK // 512).ljust(16, b'\0')
    bulk(h, EP_OUT, struct.pack('<4sIIBBB', b'USBC', tag, CHUNK, flags, 0, 10) + cdb)
    data = bulk(h, EP_OUT, write) if write else bulk(h, EP_IN, CHUNK)
    sig, rtag, residue, status = struct.unpack('<4sIIB', bulk(h, EP_IN, 13))
    if sig != b'USBS' or rtag != tag or status != 0 or (not write and len(data) != CHUNK):
        sys.exit(f'chunk {k}: bad reply (sig {sig}, tag {rtag}/{tag}, status {status})')
    return data


def read_chunk(h, k, tag):
    return command(h, k, tag)


def write_vanilla(h, van):
    """Write every 2 KB chunk of vanilla, read it back, stop at the first mismatch."""
    for line in open(os.path.join(ROOT, 'firmware', 'SHA256SUMS')):
        hsh, name = line.split()
        if name == 'IMG.raw' and hashlib.sha256(van).hexdigest() != hsh:
            sys.exit('vanilla IMG.raw fails SHA256SUMS — refusing to write')
    if len(van) % CHUNK:
        sys.exit('image size is not a multiple of 2 KB')
    n = len(van) // CHUNK
    if input(f'Write vanilla R039 ({len(van)} B, {n} chunks) to the OS area? type "write": ') != 'write':
        sys.exit('aborted, nothing written')
    for k in tqdm(range(n), desc='write+verify', unit='chunk'):
        want = van[k * CHUNK:(k + 1) * CHUNK]
        command(h, k, 2 * k + 1, write=want)
        if command(h, k, 2 * k + 2) != want:
            sys.exit(f'chunk {k} (offset {k * CHUNK:#x}) did not read back as written — STOP, re-run to retry')


def main():
    args = sys.argv[1:]
    out = args[args.index('--out') + 1] if '--out' in args else 'os_area_readback.bin'
    van = open(VANILLA, 'rb').read()
    size = int(args[args.index('--bytes') + 1], 0) if '--bytes' in args else len(van)
    h = open_unit()
    if '--write-vanilla' in args:
        try:
            write_vanilla(h, van)
        finally:
            lib.libusb_release_interface(h, 0)
            lib.libusb_attach_kernel_driver(h, 0)
            lib.libusb_close(h)
        print('done; now run without options for a full independent readback')
        return
    got = bytearray()
    try:
        for k in tqdm(range((size + CHUNK - 1) // CHUNK), desc='read', unit='chunk'):
            got += read_chunk(h, k, k + 1)
    finally:
        lib.libusb_release_interface(h, 0)
        lib.libusb_attach_kernel_driver(h, 0)
        lib.libusb_close(h)
    got = bytes(got[:size])
    open(out, 'wb').write(got)
    print(f'read {len(got)} B -> {out}  sha256 {hashlib.sha256(got).hexdigest()}')
    ref = van[:size]
    if got == ref:
        print('MATCHES vanilla IMG.raw' + ('' if size == len(van) else f' (first {size} B)'))
    else:
        i = next(i for i in range(len(ref)) if got[i] != ref[i])
        print(f'DIFFERS from vanilla at offset {i:#x}: got {got[i:i+16].hex()} want {ref[i:i+16].hex()}')


if __name__ == '__main__':
    main()
