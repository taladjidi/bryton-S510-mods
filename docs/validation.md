# Validation

What was checked, and how, before and after each change reached a real unit. One unit: Rider S510, firmware
R039, bootloader R002, flashed from macOS.

## Offline (no unit needed)

| Check | How | Result |
|-------|-----|--------|
| Downloaded firmware is the one analysed | SHA-256 of `img.bin` and of its decompressed `IMG.raw` vs `firmware/SHA256SUMS`; sizes vs Bryton's `release.ini` | match |
| Image checksum understood | sum of all bytes but the last 4 == last 4 bytes, for both `IMG.raw` and `img.bin` | match |
| Back-to-stock package is Bryton's file | `./build.py vanilla` copies Bryton's `img.bin` unchanged (repacking `IMG.raw` with zlib gives an equivalent but not identical file, so it is not used when `img.bin` is present) | identical by construction |
| Patched builds only change what they declare | `build.py` compares every byte with stock; T1 differs in 192 bytes of one text block + 2 checksum bytes | as declared |
| Built package passes the unit's checks | `build.py` re-decompresses `IMG.bin`, re-checks both checksums, size ≥ 1 MB, 512-byte padding, version string | pass |
| Bad patches are refused | wrong original-bytes hash, overlapping patches, patch applied twice, text of a different length, re-encoded text block too big | all refused |
| Text-block encoder is exact | decode → re-encode four language blocks, compare with the firmware's bytes | byte-identical (4/4) |
| Web builder agrees with build.py | `node site/test.mjs` (also in GitHub Actions on every push): web build of the `workout` set vs `./build.py workout`, compared on the decompressed image; the compressed `IMG.bin` differs (browser deflate vs zlib -9) | identical image |
| Patch format change | builds from hash-based patches vs builds from the earlier byte-based patches | byte-identical |

## On the unit

| # | What | How | Result |
|---|------|-----|--------|
| 0 | Read the whole firmware area | Engineer Mode, `iaprecover.py` | 4 026 368 B == stock R039 |
| 1 | Recovery write test | `iaprecover.py --write-vanilla` (stock over stock, each 2 KB chunk read back), then full readback | all chunks verified, readback == stock, boots normally |
| 2 | First patched firmware: `T1` | `deploy.py T1`, normal update at startup | update animation, boots, field title reads "3s QW" |
| 3 | `P1` + `P3` | `deploy.py` | boots; 3 beeps; strip over the chart without dimming. Issue: sometimes the strip stays up the whole next step (re-armed after the step change) |
| 4 | `P1` + `P3` + `D1` | `deploy.py` | boots; debug logs saved at each boot but only contain that boot (RAM log doesn't survive the 4-button reboot) |
| 5 | `P1` + `P3` + `P4` | P4 instructions checked first with Ghidra's disassembler (`ghidra/scripts/PatchCheck.java`), then `deploy.py` | boots; 3 beeps; strip appears at 3 s and hides after every step change (incl. 15 s intervals), never sticks |
| 6 | `P1` + `P3` + `P4` + `P5` (= `workout`) | P5 new code assembled with clang, every instruction checked with `PatchCheck.java`, then `deploy.py` | boots; tapping the centre power cycles instant / 3s / 10s / 30s with the header label, smoothing as expected, mode kept across restarts |
| 7 | `workout` built by the **web builder** | built on the live site, files written to the drive root, normal update at startup | installs and runs exactly like the `deploy.py` builds (its `IMG.bin` is compressed by the browser, not zlib -9) |

Side observation after #2: battery read 100 % after the session vs 90 % before. Explained by the voltage-only
battery gauge after a long time on USB (see [how-it-works.md](how-it-works.md#battery-percentage)); the battery
code and table are untouched by the patches (verified byte-for-byte).
