# Validation

What was checked, and how, before and after each change reached a real unit. One unit: Rider S510, firmware
R039, bootloader R002, flashed from macOS.

## Testing strategy for patch code

Patches with new code (`P5` onward) go through every layer below before a ride. Each layer catches what the
others miss; none proves the code correct on its own.

| Layer | Tool | Catches | Misses |
|-------|------|---------|--------|
| 1. Build checks | `tools/asm2patch.py`, `build.py` | code that does not fit its cave, unknown firmware symbols, firmware function pointers without the Thumb bit (set automatically since 2026-10-22), bytes changed outside the patch, wrong firmware | anything about behaviour |
| 2. Instruction review | `ghidra/scripts/PatchCheck.java` (Ghidra's own decoder, read-only), decompile of the patched functions | hand-encoded instructions that decode to something else, a hook landing on the wrong instruction, code that reads differently from the source | runtime values |
| 3. Host tests | `tests/test_tools.py` (clang for this computer) | wrong logic in pure functions: DFA a1 against a Python reference, the crash guard state machine | anything touching the firmware |
| 4. Emulation fuzzing | `tools/patchsim.py` (Unicorn): the shipped bytes in the patched image, each entry point called hundreds of times with random arguments and memory, firmware calls stubbed | calls without the Thumb bit, faults, unmapped accesses, writes into the image, ITCM, peripherals or undeclared firmware RAM, endless loops, stack over 512 B; caught the 2026-10-22 bug on the old bytes | what the stubbed firmware functions would do, timing, other tasks, inputs outside the declared contracts (`@args`) |
| 5. Gate | `deploy.py` refuses an image that fails layer 4; CI (GitHub Actions) runs layers 1, 3 and 4 and the web builder check on every push | a build reaching the unit or the site without the checks | |
| 6. On the unit, short | install, boot, the feature itself, debug log (`D1`) for faults | everything the emulator stubs | rare paths |
| 7. On the unit, ride path | start a ride by hand, wait over a minute, 4-button restart (the ride must resume), stop and save; check the `.fit` (CRC, messages) | the recording path end to end, the firmware's resume | long-ride effects |
| 8. Ride | a real ride with the build, then the `.fit` and `\Debug` logs | long-run and real-sensor behaviour | |

Last line of defence, not a substitute for the layers: on the recording path (`P8`, `P9`), `patches/guard.h`
switches an entry point to the stock path if the unit restarted while it was running.

The from-scratch Rust firmware (github.com/taladjidi/bryton-rustfw) follows the same layered approach (its
AGENTS.md, "Testing strategy") and adds one for boot code: its emulator (Renode) passed an image that stopped at
its first MPU write on the unit (2026-10-22), so boot steps that touch the core are checked on the unit with a
build that flashes the backlight once per boot stage.

Why the layers grew: on 2026-10-22 `P9` (layers 1, 2, 3, 6 passed) wrote a firmware function pointer without its
Thumb bit; the first ride start faulted, and a 3 h ride was lost. Layer 2 showed the right address but not the
missing bit, and layer 6 never started a ride by hand. Layers 4, 5 and 7 and the Thumb bit check in layer 1 came
from that.

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
| Web builder agrees with build.py | `node site/test.mjs` (also in GitHub Actions on every push): web build of the `recommended` set vs `./build.py recommendedout`, compared on the decompressed image; the compressed `IMG.bin` differs (browser deflate vs zlib -9) | identical image |
| Patch format change | builds from hash-based patches vs builds from the earlier byte-based patches | byte-identical |

## On the unit

| # | What | How | Result |
|---|------|-----|--------|
| 0 | Read the whole firmware area | Engineer Mode, `iaprecover.py` | 4 026 368 B == stock R039 |
| 1 | Recovery write test | `iaprecover.py --write-vanilla` (stock over stock, each 2 KB chunk read back), then full readback | all chunks verified, readback == stock, boots normally |
| 2 | First patched firmware: `T1` | `deploy.py T1`, normal update at startup | update animation, boots, field title reads "3s QW" |
| 3 | `P1` + `P3` | `deploy.py` | boots; 3 beeps; strip over the chart without dimming. Issue: sometimes the strip stays up the whole next step (re-armed after the step change) |
| 4 | `P1` + `P3` + `D1` | `deploy.py` | boots; debug logs saved at each boot but only contain that boot (RAM log doesn't survive the 4-button reboot) |
| 5 | `P1` + `P3` + `P4` (P4 is now part of P3) | P4 instructions checked first with Ghidra's disassembler (`ghidra/scripts/PatchCheck.java`), then `deploy.py` | boots; 3 beeps; strip appears at 3 s and hides after every step change (incl. 15 s intervals), never sticks |
| 6 | `P1` + `P3` + `P4` + `P5` (the `workout` set at the time) | P5 new code assembled with clang, every instruction checked with `PatchCheck.java`, then `deploy.py` | boots; tapping the centre power cycles instant / 3s / 10s / 30s with the header label, smoothing as expected, mode kept across restarts |
| 7 | `workout` set (now `recommended`) built by the **web builder** | built on the live site, files written to the drive root, normal update at startup | installs and runs exactly like the `deploy.py` builds (its `IMG.bin` is compressed by the browser, not zlib -9) |
| 8-11 | `P1` + `P3` + `P4` + `P5` + `P6` + `P7` | P7 (C) built with clang, checked with `PatchCheck.java`; #8-#10 debugged the Bluetooth service on the unit | boots; media buttons control music on an iPhone after forgetting and re-pairing the S510; "Multimedia Control" in Quick Status. The `recommended` set (`P1` `P3` `P5` `P7`) is this image without `P6`, whose bytes no other patch touches |
| 12-38 | `P8` to `P17`, debug builds | per FLASHLOG (private); `P9`'s Alpha1 in the ride file was not tried by hand before a ride | see each patch's notes |
| 39 | ride build with the `P9` fix and the crash guard | manual start, over a minute, 4-button restart, resume, stop and save | `.fit` 3650 B, CRC OK, Alpha1 on every record, hrv message, no fault |

Side observation after #2: battery read 100 % after the session vs 90 % before. Explained by the voltage-only
battery gauge after a long time on USB (see [how-it-works.md](how-it-works.md#battery-percentage)); the battery
code and table are untouched by the patches (verified byte-for-byte).
