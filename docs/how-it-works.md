# How the Rider S510 firmware works (R039 / bootloader R002)

Notes from reverse-engineering, written as plain descriptions. Addresses are where things live in the firmware
image as loaded on the unit (main image at `0xd0000000`, bootloader at `0x08000000`); open the image in Ghidra
with the same base to follow along (see [../ghidra/README.md](../ghidra/README.md)).

## Hardware and software stack

- MCU: STM32H747 (Cortex-M7). The main firmware runs from external SDRAM at `0xd0000000`.
- Storage: ~31 GB eMMC. One region holds the firmware image; the rest is the FAT volume you see over USB.
- GNSS u-blox, Bluetooth via a Nordic chip, ANT+.
- RTOS: µC/OS-II (kernel copied to the M7's ITCM at boot). Filesystem: Micrium µC/FS. UI: Embedded Wizard 11.
  Compiler: IAR EWARM 8.30.
- Nothing is encrypted or signed. Integrity checks are a plain additive checksum, gzip's CRC32, and a read-back
  after writing.

## Files from Bryton's update server

`https://corp.brytonsport.com/device/riders510/` serves `release.ini` (versions/sizes) and the parts:

| File | What it is |
|------|------------|
| `img.bin` | Main firmware, gzip-compressed (the only file these patches touch) |
| `iap.bin` | Bootloader, 128 KB, flashed into the MCU's internal flash at `0x08000000`. **Never modify it.** |
| `nordic.bin` | Bluetooth chip firmware package |

### Image formats

- `IMG.raw` (decompressed, 4 026 368 B): an ARM vector table at offset 0, the version string `R039` at offset
  `0x94`, and **the last 4 bytes = sum of all other bytes, mod 2³², little-endian**.
- `img.bin` (2 635 776 B): a gzip member (with the file name `IMG.bin`) of `IMG.raw`, zero padding, and again a
  4-byte additive checksum of everything before it; total size a multiple of 512.

Python's zlib doesn't reproduce Bryton's compressed bytes exactly (different compressor), which doesn't matter:
the unit decompresses and checks the result, it doesn't compare compressed bytes.

## How an update is installed

The update routine (`0xd018c264`) runs on **every startup**, from the hardware init at `0xd018a282`, just after
the filesystem is mounted. In order:

1. If `\update.ini` doesn't exist on the drive: nothing to do. Its content is not used — only its presence.
2. Battery is sampled; **below 38 %** the update is skipped (files stay, so it retries on the next start).
3. `TimeZone.bin` and `IAP.bin` (bootloader!) are applied if present and their checksums are right.
4. `\IMG.bin`: must be ≥ 1 MB and pass its additive checksum, is decompressed to `\IMGUnzip.bin` (gzip CRC
   checked), which must pass its own additive checksum.
5. The image is written to the firmware area of the eMMC, read back and summed again. **A mismatch is only
   logged**, the update still completes.
6. `update.ini`, `IMG.bin`, `IMGUnzip.bin` are deleted and the unit restarts.

If USB is connected when the unit starts, it stays in USB-drive mode and restarts when unplugged — so in
practice: copy the files, eject, unplug. This is exactly what the official Bryton Active "update with cable"
does. Just before the update routine, a cleanup deletes `*.exe` files in the drive's root; nothing else.

## How the unit boots (bootloader)

The bootloader (internal flash, never written by these tools) does, at every reset:

1. Clocks, SDRAM init and test, eMMC init.
2. Copies **6 MB** of the firmware area from eMMC to SDRAM `0xd0000000` — **no checksum, no signature**.
3. If the **Page** button (PE3, lower right) is held **and** USB power is present: stays in its own USB mode
   (screen: "Engineer Mode") until the cable is removed, then resets.
4. Otherwise jumps to the image's reset vector.

### Bootloader USB mode

A USB mass-storage device (`0483:5720`, bulk endpoints OUT `0x01` / IN `0x81`). Normal SCSI commands, but the
"group number" byte of `READ(10)` / `WRITE(10)` (CDB byte 6) selects a vendor operation:

| CDB byte 6 | With READ(10) / WRITE(10) |
|-----------:|---------------------------|
| `0x0d` | read 2 KB of the firmware area |
| `0x0c` | write 2 KB of the firmware area |

For those, the LBA field is the 2 KB chunk index (byte offset = LBA × 2048) and the transfer length must be
4 blocks (2048 B). A failed eMMC write is only printed on the bootloader's debug UART; the USB status still says
OK — hence `iaprecover.py` reads every written chunk back. Other sub-commands exist (UUID, chip erase, a data
partition window); the tools don't use them.

On macOS the system's disk driver owns any USB mass-storage device and Apple offers no way to send it custom
SCSI commands, so `iaprecover.py` uses libusb (as root it can detach the driver) and speaks the USB
mass-storage Bulk-Only protocol itself.

## How UI text is stored (Embedded Wizard strings)

UI strings are UTF-16 and stored in **compressed blocks**, one or more per language:

- block = `u32` decompressed size + LZW code stream: 9 → 12-bit codes, least-significant-bit first, `0x100` =
  clear, `0x101` = end, first free code `0x102`; every stream starts with a clear code. Decoder in the firmware
  at `0xd015e9d8`; `tools/ewstr.py` decodes and re-encodes byte-identically.
- each string inside a block is preceded by two UTF-16 characters (length, marker `0x4557`); a reference is
  `(block address, character index)` pointing just after that header.
- a multi-language text is a table of `(language id, block, index)` entries; language `0` is English.

Changing a text = decode the block, replace the characters (same length, so other indexes stay valid),
re-encode, and fit into the space before the next block (blocks are followed by a few bytes of zero padding).

Data-field titles shown on data pages and the workout screen come from one table (`0xd0150ef8`); in English they
live in block `0xd0000eec`. Field IDs used in `System/Bike*Profile.ini` (`GridN=`) match: 27 = "30s PW",
28 = "3s PW", 29 = "Power", 145 = workout total remaining.

## Workout next-step popup

- **Trigger** (`0xd01aaec8`, evaluated on every workout update, about once a second): for **time** steps, while
  `0 < remaining < 10` s — and it also beeps on every update in that window if the countdown beep is on (so about
  9 beeps); for **distance** steps, when ≤ 49 (m) remain. The first time, it prepares the next step's details
  and raises a "show preview" flag.
- **Display**: the data page checks the flag once a second (1000 ms timer). The popup is an Embedded Wizard
  dialog laid out as a full-screen semi-transparent black backdrop (colour `0xd0086d6c`) + a white panel at
  `0,192–240,320` holding "Next Step N/M", the step kind, the duration and the target range. Each rectangle is a
  16-byte constant (`0xd0086d70` … `0xd0086e00`) used only by this dialog, except the green bottom bar.
- **Hiding**: when the next step starts the flag is cleared (the popup goes within ~1 s), or when the "N/M"
  area is tapped.

`P1` changes the `< 10` comparison to `< 4` (beeps at 3, 2, 1; the preview flag is raised at 3 s too).
`P3` makes the backdrop transparent, moves the panel and the duration/target lines into a 72-px strip at the top
(over the interval chart), and moves the header, step kind and green bar off-screen. Data only, no code.

## Workout centre value (power)

The big number in the middle of the workout screen is the step target's live value. For power targets it is
the **instantaneous** power sample. The firmware also keeps rolling **3 s / 10 s / 30 s** averages of the
per-second power samples (window table `{3, 10, 30}` at `0xd0281178`), shown as data fields 28 / 78 / 27.
Switching the centre value to one of them is a planned patch (needs a little new code). FIT workout files can
already request `power_3s` / `power_10s` / `power_30s` targets per step, but this firmware rejects those target
types when importing.

## Battery percentage

Voltage only: a 100-entry table maps millivolts to percent (3450 mV = 0 %, ≈3790 mV = 50 %, ≥ 4140 mV = 100 %).
The last value is kept in a backup register and only allowed to go **up while on USB** and **down while on
battery**, so after a long time on the cable the unit can show 100 % for a while. Not a bug in the patches.
