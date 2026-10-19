# Recovery and the bootloader USB mode

You only need this page if a patched unit **no longer starts**, or if you want to prove the recovery path
before your first flash (recommended for tinkerers, optional for everyone else). Normal installs and going
back to stock use the ordinary update procedure from the [README](../README.md).

Needs: a Mac or Linux PC, Python ≥ 3.8, `libusb` (macOS: `brew install libusb`; Debian/Ubuntu:
`sudo apt install libusb-1.0-0`), `sudo`, and the stock firmware (`python3 tools/fetch_firmware.py`).
`tqdm` (`pip install tqdm`) adds progress bars but is optional. Windows: not supported for recovery.

## Why this is safe

The firmware itself has no protection against a bad image: once an image is written, the bootloader runs it
without any check. What makes experimenting safe is that the **bootloader is never touched** by these patches
and has its own USB mode that can read and rewrite the firmware area. Proving that path on your own unit
before the first flash (steps 1–3) takes ten minutes and removes the remaining doubt.

### Step 1 — enter bootloader mode ("Engineer Mode")

1. Plug the unit into the computer (it shows the BRYTON drive / charging screen, that's fine).
2. Press **all four buttons at once** to reboot it.
3. As soon as the screen goes off, release the others but **keep holding Page** (lower-right button, ≡).
4. The screen shows **"Engineer Mode"**. macOS now sees a ~272 MB *unformatted* disk instead of the 31 GB BRYTON
   drive (if macOS asks to initialise it: **Ignore**). That is the bootloader's own USB mode.

> A unit that is switched off and then plugged in does *not* go through the bootloader — it only wakes up to
> show the charging screen. The four-button reboot is what makes the bootloader run with Page held.

Leaving: just unplug. The bootloader restarts the unit normally.

### Step 2 — read back your firmware (read-only, cannot harm the unit)

```sh
sudo python3 tools/iaprecover.py --out build/readback.bin
```

It reads the whole firmware area (≈4 MB, under a minute) through the bootloader and compares it with stock
R039. You want: **`MATCHES vanilla IMG.raw`**. This proves (a) your unit runs stock R039 and (b) your Mac can
talk to the bootloader. `sudo` is needed so libusb can take the device away from macOS's disk driver.

### Step 3 — recovery write test (rewrite stock over stock)

Still in Engineer Mode:

```sh
sudo python3 tools/iaprecover.py --write-vanilla      # type "write" when asked
sudo python3 tools/iaprecover.py --out build/after.bin  # independent full readback
```

This writes the stock image over itself, 2 KB at a time, reading every chunk back (the bootloader only reports
write errors on an internal debug port, so the read-back is the real check). Because every byte written equals
what is already there, even an interrupted run leaves the unit as it was. Unplug, check it boots normally.

**Once step 3 has passed, you have a proven way back from a unit that doesn't boot.** That is the same command
you would run if a patch ever went wrong.

### Going back to stock

- **Unit boots:** *Back to stock firmware* in the web builder, or `./deploy.py vanilla` (exactly Bryton's
  original file), then unplug — same as installing a patch.
- **Unit does not boot:** Step 1 (Engineer Mode) + `sudo python3 tools/iaprecover.py --write-vanilla`.
- Never put an `IAP.bin` (bootloader) on the drive: the update code would flash it into the chip, and a bad
  bootloader is the one thing these tools cannot recover from. `deploy.py` refuses if it finds one.


## If Bryton no longer serves R039

Once Bryton publishes a newer version, `fetch_firmware.py` can no longer download R039. If your unit still
runs stock R039, use its own copy instead: enter Engineer Mode, read it back (Step 2 above), then

```sh
python3 tools/fetch_firmware.py --from-readback build/readback.bin
```

It checks the readback is exactly stock R039 and saves it as the base the patches are applied to. (A unit
that already runs patched firmware reads back as "not stock"; go back to stock first.)
