# bryton-S510-mods

Unofficial, community-made firmware patches and tools for the **Bryton Rider S510** GPS bike computer.

It started as a curiosity: the S510 is a nice unit, but a couple of things in its workout screen annoyed me
(a full-screen "next step" popup that hides the data on short intervals, a very jumpy power number). So I
reverse-engineered how the firmware is built, updated and booted, made sure a unit can always be recovered,
and then started patching it. This repo contains the tooling and the patches — **not** Bryton's firmware: you
download that yourself from Bryton's update server, and the scripts apply the patches on your machine.

> [!WARNING]
> **Not affiliated with or endorsed by Bryton.** Flashing modified firmware can make your unit unusable and
> may void your warranty. Everything here was tested on **one** unit, firmware **R039** / bootloader **R002**,
> from macOS. Read the whole procedure, do the recovery test first, and use it at your own risk.

## What you get

| Patch | What it does | Status |
|-------|--------------|--------|
| `T1` | Renames the English data-field title "3s PW" to "3s QW". Pointless on purpose: a harmless, visible change to prove the build → flash path. | ✅ flashed and running |
| `P1` | Workout countdown: **3 beeps** (3, 2, 1 s) before a timed step ends instead of ~9. | 🧪 built, not yet tested on the unit |
| `P3` | Workout "next step" popup becomes a **small strip over the chart** (next duration + next target), without dimming the whole screen. | 🧪 built, not yet tested on the unit |

| Tool | What it does |
|------|--------------|
| `tools/fetch_firmware.py` | Downloads stock firmware from Bryton's server and checks it is the exact R039 the patches were made for |
| `build.py` | Applies patches to stock firmware and builds a flashable `IMG.bin` + `update.ini`, re-checking everything the unit checks |
| `deploy.py` | Builds, copies to the unit's USB drive, verifies the copy, ejects, logs |
| `tools/iaprecover.py` | **Recovery**: reads back, or rewrites with stock, the firmware area of the unit through its bootloader — works even if the firmware no longer boots |
| `tools/ewstr.py` | Decoder/encoder for the firmware's compressed text blocks (Embedded Wizard LZW) |
| `ghidra/` | Ghidra scripts used for the analysis (see [ghidra/README.md](ghidra/README.md)) |

How the unit updates, boots and stores its UI text, and what the patches change: [docs/how-it-works.md](docs/how-it-works.md).
Every check and every flash done so far: [docs/validation.md](docs/validation.md).

## Requirements

- A Rider S510 on firmware **R039** (check `System/device.txt` on its USB drive: `[OS] Version=R039`)
- macOS (the only platform tested), Python ≥ 3.8 with `tqdm` (`pip install tqdm`)
- For recovery: `libusb` (`brew install libusb`) and `sudo`
- A USB cable, battery **≥ 38 %** (the unit refuses to update below that)

```sh
git clone https://github.com/taladjidi/bryton-S510-mods && cd bryton-S510-mods
python3 tools/fetch_firmware.py          # downloads to firmware/R039/, must print "OK (stock R039)" twice
```

If it prints `DOES NOT MATCH R039`, Bryton has published newer firmware. **Stop**: the patches only apply to
R039 (`build.py` would refuse anyway), and a unit already updated past R039 is not supported.

## The safe procedure

The firmware itself has no protection against a bad image: once an image is written, the bootloader runs it
without any check. What makes experimenting safe is that the **bootloader is never touched** by these patches
and has its own USB mode that can read and rewrite the firmware area. So the order is: **prove you can recover
first, then flash.**

### Step 1 — enter bootloader mode ("Engineer Mode")

1. Plug the unit into the Mac (it shows the BRYTON drive / charging screen, that's fine).
2. Press **all four buttons at once** to reboot it.
3. As soon as the screen goes off, release the others but **keep holding Page** (lower-right button, ≡).
4. The screen shows **"Engineer Mode"**. macOS now sees a ~272 MB *unformatted* disk instead of the 31 GB BRYTON
   drive (if macOS asks to initialise it: **Ignore**). That is the bootloader's own USB mode.

> A unit that is switched off and then plugged in does *not* go through the bootloader — it only wakes up to
> show the charging screen. The four-button reboot is what makes the bootloader run with Page held.

Leaving: just unplug. The bootloader restarts the unit normally.

### Step 2 — read back your firmware (read-only, cannot harm the unit)

```sh
sudo "$(which python3)" tools/iaprecover.py --out build/readback.bin
```

It reads the whole firmware area (≈4 MB, under a minute) through the bootloader and compares it with stock
R039. You want: **`MATCHES vanilla IMG.raw`**. This proves (a) your unit runs stock R039 and (b) your Mac can
talk to the bootloader. `sudo` is needed so libusb can take the device away from macOS's disk driver.

### Step 3 — recovery write test (rewrite stock over stock)

Still in Engineer Mode:

```sh
sudo "$(which python3)" tools/iaprecover.py --write-vanilla      # type "write" when asked
sudo "$(which python3)" tools/iaprecover.py --out build/after.bin  # independent full readback
```

This writes the stock image over itself, 2 KB at a time, reading every chunk back (the bootloader only reports
write errors on an internal debug port, so the read-back is the real check). Because every byte written equals
what is already there, even an interrupted run leaves the unit as it was. Unplug, check it boots normally.

**Once step 3 has passed, you have a proven way back from a unit that doesn't boot.** That is the same command
you would run if a patch ever went wrong.

### Step 4 — flash a patch

Unplug, let the unit start normally, plug it in again (BRYTON drive mounted), then:

```sh
./deploy.py T1                     # or: ./deploy.py -n beeps+strip P1 P3
```

`deploy.py` builds the package from stock + the patches you name, checks the drive really is the S510, copies
`IMG.bin` and `update.ini` to its root, verifies the copies, ejects it and appends an entry to your local
`FLASHLOG.md`. **Unplug**: the unit restarts, shows the firmware-update animation, installs the update and
restarts again. Check the change, and write what you saw in `FLASHLOG.md`.

Every build starts from stock: flashing `P1 P3` removes `T1` unless you list it again.

### Going back to stock

- **Unit boots:** `./deploy.py vanilla` (exactly Bryton's original file), then unplug — same as a patch.
- **Unit does not boot:** Step 1 (Engineer Mode) + `sudo "$(which python3)" tools/iaprecover.py --write-vanilla`.
- Never put an `IAP.bin` (bootloader) on the drive: the update code would flash it into the chip, and a bad
  bootloader is the one thing these tools cannot recover from. `deploy.py` refuses if it finds one.

## Checks behind this

Short version of [docs/validation.md](docs/validation.md):

- The tools reproduce **every check the unit makes** on an update (image checksum, gzip, padding, package
  checksum, size limits) and re-verify the built package by decompressing it again.
- `./build.py vanilla` reproduces Bryton's `img.bin` **byte for byte**; patched builds differ from stock **only**
  in the declared bytes (plus the 4-byte checksum), which `build.py` checks.
- Patches identify what they replace by **hash**, not by copying it: a wrong firmware, wrong address, two
  overlapping patches or a patch applied twice are refused.
- The text-block encoder reproduces Bryton's compressed blocks **byte for byte** before it is trusted to write one.
- On the unit: full readback == stock, full stock rewrite + readback == stock and boots, `T1` flashed and running.

## Writing patches

```
desc:   one line
source: where the change is explained
0xADDR  sha256:<first 16 hex of sha256(original bytes)>  <new bytes, hex>
string  0xBLOCK  0xINDEX  sha256:<16 hex of the original text>  room=<bytes>  "new text"
```

Addresses are load addresses (the image runs at `0xd0000000`). See [docs/how-it-works.md](docs/how-it-works.md)
for the image layout and the string blocks, and `build.py`'s header for the details.

## Legal

The firmware is Bryton's copyrighted work and is **not** included here — not as a file, and not as fragments
inside patches (they carry only new bytes and hashes). This repo contains my own code and documentation of how
the device works, written for interoperability and to let owners fix things on hardware they own. Bryton names
are used only to say what this is compatible with. If you represent Bryton and have a concern, please open an
issue or contact me — I'd much rather talk than have it taken down.

Code: [MIT](LICENSE).
