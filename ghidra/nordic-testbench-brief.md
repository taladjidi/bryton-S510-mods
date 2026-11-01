# Brief: a radio chip test bench from Bryton's nRF52 app

For a dedicated Claude Code session started in `/Users/tangui/Documents/bryton/rustfw`. Self-contained: read this
whole file first, then the files it names. Written 2026-11-01 evening.

## Why

On the 2026-11-01 road ride (rustfw build be0c6c1) every sensor dropped out for about 270 s at a time, every ~305 s.
The cause, found the same evening: Bryton's app on the Nordic radio chip queues events in 96 slots and **hangs for
ever** when the queue overflows (its error handler loops, no watchdog). Two power savings of 2026-10-31 had cut how
fast rustfw drains the chip (review finding 8, a 20 us receive wait instead of 200 us, and finding 9, a 100 ms ride
pass), so a ride's 15 to 25 events a second overflowed it in about 30 s; rustfw's own radio watchdog then reset the chip
after 30 s of silence, with a doubling backoff. Both changes and a suspect MTU change were taken out (main 99061aa), and
the simulator gained a first chip model (`ChipLimits`) whose ride tests reproduce the hang.

The owner's goal: **robust testing that does not depend on rides or the unit** ("me saying all good nothing broke" is
not a test). The radio chip is the black box that broke; Bryton's app binary is ours to read. Turn it into a test bench.

## Rules (read before anything)

- rustfw `AGENTS.md` (gate, no dashes, safe Rust, tests that fail before a fix, fuzz for every input path, docs) and
  `~/.claude/CLAUDE.md` (typed agents and their models; the main session orchestrates, agents do the legwork; one
  review per feature; about 3 agents at once; a task over ~120 agent turns is split).
- **Public vs private.** rustfw is PUBLIC: only hardware and interface facts go there (BOARD.md section 11, a command
  table, model parameters), each with its source. Nordic's serialization codecs are Nordic's open SDK (pc-ble-driver,
  nRF5 SDK 14 `components/serialization`): cite those for layouts. What only the binary shows (queue sizes, fatal paths,
  addresses) is stated as a fact with "Bryton's nRF app, reversed" as the source; details and addresses go to this
  private repo (bryton-S510-mods, `ghidra/nordic-app.md`). **Never** put `nordic.bin`, code or tables copied from it,
  or Bryton's ANT licence key (it is in the binary) in any repo, commit, test or doc. The ANT+ network key and Di2 key
  rules of AGENTS.md also hold.
- Never run Ghidra headless on the stock project `~/Documents/bryton/ghidra` (its GUI may be open); make new projects
  in the session's scratchpad.
- No unit work is needed. If any is wanted later, the owner must be present (key presses beep).
- After 23:00: throttle builds and gates (`nice -n 19 env CARGO_BUILD_JOBS=2 RUST_TEST_THREADS=2`), one full gate at a
  time.

## What exists

- The binary: `/Users/tangui/Documents/bryton/firmware/R039/nordic.bin` (86 794 B): a 16-byte Bryton header, a
  Nordic Secure DFU init packet (signed by Bryton: we cannot load our own app on the chip), then the app.
- The first reversal: `bryton-S510-mods/ghidra/nordic-app.md` (commit af299d1). Key facts:
  - App at 0x2D000 on S332 v5.0.x (ANT + BLE), app RAM from 0x20006800. Matched against nRF5 SDK 14.2.
  - UARTE, RTS/CTS, 1 Mbaud; frames 1 to 384 payload bytes each way; one RX and one TX buffer; TX waits on our RTS
    with no time-out and resumes.
  - Commands one at a time: after a packet the chip reads the next 2-byte header, then holds CTS until the previous
    command's response is queued. No response for type 05 (reset), op 0x32 (power off), type 03 (DTM).
  - Events: 96 slots of 264 B (SDK default 16), one frame in flight, no new event between a command header and its
    response; stops pulling at 0 free, resumes above 48; one pull bringing more events than free slots: hang.
  - Fatal (chip hangs, silent until reset): payload 0 or over 384 B; type not 00 / 03 / 05 / 06; an unknown opcode;
    a command whose length does not match its arguments exactly; UART parity / overrun / framing error inside a
    payload; a BLE event over 262 B; a SoftDevice assert. Error handler at 0x3F414 (masks, loops); CPU exception
    handlers are `B .`; no WDT.
  - Checked: GATTC 0x9E (Exchange MTU) request is exactly 5 B; `sd_ble_cfg_set` ATT MTU exactly 9 B.
  - Not determined: which nRF pin PH8 is; what an unknown cfg_set id does; the 8-slot pairing limit (SDK default).
- rustfw today (main 99061aa): `src/drivers/nrf.rs` (framing, `Rx`, `call`, `start`), `src/drivers/ble.rs` (BLE
  commands and events), `src/app/radio.rs` (`poll_nrf`, `nrf_wait_cycles` = 200 us), `src/main.rs` `nrf_recv` /
  `nrf_send`, `src/app/nrf_watch.rs` (watchdog), BOARD.md section 11 (what is known of the link, stock's commands).
  Simulator: `src/sim/mod.rs` (`ChipLimits { slots: 96, frame_gap_us: 80, wait_us }`, OFF by default, events queued,
  hang on overflow) and `src/sim/ride_radio_tests.rs` (10 min and 4 h rides: production high-water 12 of 96, no hang;
  the old 20 us wait with a 100 ms pass hangs 6 times in 10 min). Known simplifications (review of 95d92fa): bytes are
  produced at read time (no 16-byte FIFO byte by byte), no 0 / 48 pull rule, the frame end is counted in whole ms
  (pessimistic for a short wait), the 80 us gap between frames is inferred.
- The stock STM32 firmware, reversed: `~/Documents/bryton/ghidra` (notes, `HARDWARE.md`, `notes/agent_b2.md` for the
  serialization encoders): how stock drives the chip.

## The work, in order (each step usable on its own; merge each before the next)

### Step 1: the chip's command and event table (about half a day)

- Reverse the app's command dispatch: every (type, opcode) it decodes, the exact payload length or length rule each
  must have (fixed, or the variable parts and their bounds), and its response; every event it can send (BLE event ids,
  the 0x08 ANT event of 44 B, responses) with its layout and maximum size. Cross-check each against the nRF5 SDK 14
  codecs (cite file and function) and against what stock sends (BOARD.md 11).
- In rustfw: the table as data (a module in `src/drivers/` or `src/sim/`, facts and sources only), and the simulated
  chip enforcing it: an unknown (type, opcode), a wrong length or an out-of-range field HANGS the simulated chip, as
  the real one does.
- Tests: one that builds every command our code can send (every builder in nrf.rs / ble.rs / the ANT channel code,
  with representative and boundary arguments) and checks it against the table; structure-aware fuzz of our event
  parsers generated from the table's real layouts, with reach counts.
- Done when: the gate fails if any code path sends a command the chip would hang on; BOARD.md 11 has the table's
  source and coverage; the private notes have the addresses.

### Step 2: the transport and the queue, exactly (about a day)

- Reverse precisely: the UARTE RX side (when the chip raises its RTS, i.e. our CTS), the "one command at a time"
  rule, the event pull (which SoftDevice calls, how many per pull, the 0 / 48 thresholds), the SoftDevice's own ANT
  event queue (32, per BOARD.md) and what the app does on its overflow event, the configurations (`sd_ble_cfg_set`
  combinations) that make `sd_ble_enable` fail for lack of RAM given app RAM 0x20006800, the pairing contexts limit.
- In the simulator: model the 16-byte RX FIFO and RTS byte by byte, the frame end in microseconds, the pull rule, CTS
  held while a command waits for its response, the ANT queue. Then turn the strict chip ON by default for every sim
  test (ChipLimits default), fixing the model (not the assertions) where existing tests break, unless a test encoded
  wrong behaviour.
- Tests: the ride tests at realistic traffic (HR, power, Di2 at their real periods, the phone's ANCS / AMS bursts, a
  BLE trainer, scans), a property test over random pass lengths up to the worst real pass (map re-centre 162 ms, card
  write) asserting the queue's high-water stays under 48, and a test that each fatal condition of the table hangs the
  model (so the model itself is tested).
- Done when: every simulator test runs against the strict chip and the worst-case pass length has a documented margin.

### Step 3: Bryton's real code in the loop (2 to 3 days; the most robust)

- Run `nordic.bin`'s app in an emulator: Renode's nRF52840 platform (rustfw already uses Renode for the STM32), or a
  small Unicorn-engine harness. Stub the SoftDevice: the app calls it through SVC numbers (S332 v5 API); the harness
  answers them (events from a script: sensors at their periods, phone traffic) and models UARTE0 with EasyDMA, the
  RTC / timers and the GPIOs the app uses. Wire its UART to the rustfw simulator (a pipe or socket to a `Board`
  implementation), so the library's real radio code talks to Bryton's real app byte for byte.
- Use it to: (a) measure the app's cycles between two frames at 64 MHz and replace the inferred 80 us; (b) differential
  tests: the same traffic through the emulated app and the fast `ChipLimits` model must give the same frames, CTS
  behaviour and hangs (this keeps the fast model honest, so it can stay in every gate); (c) a long nightly soak (the
  emulator is slow: not in check.sh's default run; an ignored test or a tools/ script, documented in docs/devloop.md).
- The emulator harness and its scripts live in rustfw only if they contain no Bryton code or data; the binary is read
  from its path outside the repo at run time, and the test skips when it is absent (the owner's machine has it).
- Optional later: the real S332 SoftDevice (downloadable from thisisant.com under its licence, kept local like the ANT
  key) instead of SVC stubs.

### Alongside: the stock envelope

From the stock STM32 firmware already reversed: stock's read cadence of USART2, its waits, the commands and
configurations it sends. Record them in BOARD.md 11 as the known-good host behaviour, and add a gate check that
flags any command or configuration rustfw sends that stock never does (allowed only with a dated "seen on the unit"
entry in docs/status.md).

## Hand-back

- After each step: merged to main through the project's merge procedure, docs current (BOARD.md 11, docs/status.md,
  docs/layout.md, AGENTS.md if a rule changes), the private notes updated and committed in bryton-S510-mods.
- At the end of the session: a handoff in rustfw's memory (what is merged, what is open, the next step), and a short
  report to the owner: what each step now catches that the 2026-11-01 ride would have needed, and what still needs
  the unit or a ride.
