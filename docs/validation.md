# Validation

What was checked, and how, before and after each change reached a real unit. One unit: Rider S510, firmware
R039, bootloader R002, flashed from macOS.

## Offline (no unit needed)

| Check | How | Result |
|-------|-----|--------|
| Downloaded firmware is the one analysed | SHA-256 of `img.bin` and of its decompressed `IMG.raw` vs `firmware/SHA256SUMS`; sizes vs Bryton's `release.ini` | match |
| Image checksum understood | sum of all bytes but the last 4 == last 4 bytes, for both `IMG.raw` and `img.bin` | match |
| Package rebuild is exact | `./build.py vanilla` output vs Bryton's `img.bin` | byte-identical |
| Patched builds only change what they declare | `build.py` compares every byte with stock; T1 differs in 192 bytes of one text block + 2 checksum bytes | as declared |
| Built package passes the unit's checks | `build.py` re-decompresses `IMG.bin`, re-checks both checksums, size ≥ 1 MB, 512-byte padding, version string | pass |
| Bad patches are refused | wrong original-bytes hash, overlapping patches, patch applied twice, text of a different length, re-encoded text block too big | all refused |
| Text-block encoder is exact | decode → re-encode four language blocks, compare with the firmware's bytes | byte-identical (4/4) |
| Patch format change | builds from hash-based patches vs builds from the earlier byte-based patches | byte-identical |

## On the unit

| # | What | How | Result |
|---|------|-----|--------|
| 0 | Read the whole firmware area | Engineer Mode, `iaprecover.py` | 4 026 368 B == stock R039 |
| 1 | Recovery write test | `iaprecover.py --write-vanilla` (stock over stock, each 2 KB chunk read back), then full readback | all chunks verified, readback == stock, boots normally |
| 2 | First patched firmware: `T1` | `deploy.py T1`, normal update at startup | update animation, boots, field title reads "3s QW" |
| 3 | `P1` + `P3` | — | not flashed yet |

Side observation after #2: battery read 100 % after the session vs 90 % before. Explained by the voltage-only
battery gauge after a long time on USB (see [how-it-works.md](how-it-works.md#battery-percentage)); the battery
code and table are untouched by the patches (verified byte-for-byte).
