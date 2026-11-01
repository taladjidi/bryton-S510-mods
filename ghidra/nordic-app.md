# The Nordic chip's application (R039 `nordic.bin`)

What the nRF52 radio chip runs, read from Bryton's update file for interoperability: how it frames, queues and
answers the STM32, and every path that leaves it silent. Facts, addresses and numbers only; nothing of the
binary is reproduced. Analysed 2026-11-01 (Ghidra 12.2 headless, raw ARM:LE:32:Cortex at the base below),
matched function by function against Nordic's nRF5 SDK 14.2.0 (`nRF5_SDK_14.2.0_17b948a.zip` from
developer.nordicsemi.com, `examples/connectivity/experimental_ant_ble`, `components/serialization`,
`components/libraries/scheduler`, `components/drivers_nrf/uart`, `components/softdevice/common`).
"Confirmed" means seen in the binary; "inferred" means taken from the SDK or reasoning, not checked in the binary.

## The file, the load address and the SoftDevice

- **Bryton header (16 bytes, confirmed):** u16 1, u16 134 (the init packet's length), u32 0x0077DB5E (the byte sum
  of everything after the header: checked), 8 zero bytes.
- **Init packet (134 bytes at offset 16, Nordic Secure DFU protobuf `dfu-cc.proto`, confirmed):** signed command;
  op code INIT; fw_version 1; hw_version 1; sd_req = [0x0001] (not a published FWID); type APPLICATION;
  sd_size 0; bl_size 0; app_size 86 644; hash SHA-256 (equal to the image's SHA-256, byte-reversed as Nordic
  stores it); is_debug = true (field 9); signature type ECDSA P-256 SHA-256, 64-byte signature. With is_debug the
  bootloader may skip the version checks (inferred: depends on how Bryton's bootloader was built).
- **Application image:** offset 150, 86 644 bytes, plaintext Thumb code (Keil ARMCC build: `B .` default
  handlers, scatter-load `__main`).
- **Load address 0x2D000 (confirmed):** the 16 IRQ default handlers (0x42244..0x42272, one `B .` each) only line up
  at this base; reset handler 0x4214C calls SystemInit 0x3D0F6 then `__main` 0x421D4; image ends 0x42274.
  0x2D000 is exactly the FLASH origin of SDK 14.2's S332 connectivity example.
- **Initial SP 0x2000F220; app RAM from 0x20006800** (the app_ram_base it passes to `sd_ble_cfg_set`, and the
  lowest RAM variable; inferred as the RAM start).
- **SoftDevice: S332 v5.0.x (combined ANT + BLE), API v5.** Evidence: the base above; `sd_softdevice_enable`
  takes a third argument (the ANT licence key string: S332's signature; Bryton's own commercial key, not
  reproduced here); the BLE opcodes and SVC numbers are API v5's; and the patched `sd_ble_version_get` reports
  SD_VERSION / 100 000 = 50 = v5.0 (below).
- **Low-frequency clock (confirmed bytes at 0x42240: 02 00 00 07):** source 2 = SYNTH (synthesised from the
  HF clock), accuracy 7 = 20 ppm (enum values from the S132 v5 headers; inferred identical in S332). The HF
  crystal then runs all the time (power cost inferred). POWER TASKS_CONSTLAT is set at start (0x40000078 = 1,
  as Nordic's example), constant latency mode.

## 1. The UART transport

| Item | Value | Where | SDK match | |
|------|-------|-------|-----------|---|
| Peripheral | UARTE0 with EasyDMA (`use_easy_dma` = 1) | config at 0x42074, IRQ handler 0x36E0C (UARTE) / 0x36CAC (legacy), dispatcher 0x36F24 | `nrf_drv_uart.c` | confirmed |
| Pins (nRF side) | TXD P0.18, RXD P0.17, CTS P0.14, RTS P0.13 | 0x42074 | | confirmed |
| Flow control | HWFC on | 0x42074 | `SER_PHY_UART_FLOW_CTRL` | confirmed |
| Parity | **excluded** (SDK default is included: Bryton changed it) | 0x42074 | `ser_config.h` `SER_PHY_UART_PARITY` | confirmed |
| Baud | 1 000 000 (0x10000000) | 0x42074 | `SER_PHY_UART_BAUDRATE_VAL` | confirmed |
| IRQ priority | 7 (lowest) | 0x42074 | `ser_phy_config_conn.h` | confirmed |
| Framing | 2-byte LE length, then payload, no checksum | ser_phy event handler 0x3F138 | `ser_phy_uart.c` | confirmed |
| DMA chunk | 255 bytes (longer payloads in 255-byte pieces) | 0x3F138 | `UART_TRANSFER_MAX` | confirmed |
| RX buffer | one, 384 bytes at 0x2000E6D4 | HAL 0x3EEA4 | `SER_HAL_TRANSPORT_RX_MAX_PKT_SIZE` 384 | confirmed |
| TX buffer | one, 384 bytes at 0x2000E554 | alloc 0x3EF12, send 0x3EF58 | `SER_HAL_TRANSPORT_TX_MAX_PKT_SIZE` 384 | confirmed |
| Max payload | 384 bytes each way (386 on the wire) | 0x3EF58 (`len < 0x181`) | | confirmed |

- **TX while our RTS holds it:** asynchronous. `ser_phy_tx_pkt_send` starts DMA of the 2-byte header, then the
  payload, from the UARTE IRQ; the frame ends at ENDTX. There is **no time-out** anywhere (no timer, no
  watchdog): the UARTE waits on CTS for as long as our RTS is high, then carries on (confirmed: no timer in the
  path, IRQ handler 0x36E0C handles only ERROR / ENDRX / RXTO / ENDTX).
- **While a frame is held:** the TX buffer stays allocated. Whoever needs to send next (the next event, or a
  command's response) spins in thread mode on `ser_hal_transport_tx_pkt_alloc` returning NO_MEM (event encoders
  0x41204 / 0x412BC, command path 0x41958): the CPU is awake and busy, but the UART IRQ still runs, so a
  command can still be received meanwhile (confirmed).
- **RX errors (confirmed, 0x3F138):** on a UARTE ERROR event, if ERRORSRC has OVERRUN or PARITY (mask 3), the PHY
  raises a hardware error, which the HAL turns into SER_HAL_TRANSP_EVT_PHY_ERROR, which the connectivity layer
  answers with `APP_ERROR_CHECK(NRF_ERROR_FORBIDDEN)` (0x41678, event 4): **the chip hangs** (section 3). On any
  error (FRAMING or BREAK too) it re-arms the 2-byte header read, dropping its place in the frame. A framing or
  break error inside a payload therefore desynchronises it: the HAL is still in RECEIVING, the next header's
  buffer request arrives in that state, and `APP_ERROR_CHECK_BOOL(false)` hangs it (inferred from SDK
  `ser_hal_transport.c` `phy_events_handler`; the binary's HAL matches the SDK where checked).
- **Overlong packet (length > 384):** received byte by byte into a 1-byte drop buffer, then
  SER_HAL_TRANSP_EVT_RX_PKT_DROPPED, answered with `APP_ERROR_CHECK(SER_WARNING_CODE)`, which **hangs** too (the
  warning bug, section 3; 0x41678 event 3, confirmed).

## 2. Event forwarding

- **Path (confirmed, SDK `ser_conn_handlers.c`):** SoftDevice event IRQ SWI2 (vector 0x3FDF4) polls the SoftDevice:
  BLE poll 0x41E1C (`sd_ble_evt_get` into a 262-byte stack buffer, evt_len 0x106), ANT poll 0x41E80
  (`sd_ant_event_get`, 48-byte `ant_evt_t`). Each event goes through `app_sched_event_put` (0x4035A) from the
  observers 0x416F6 (BLE, size = evt_len) and 0x4172E (ANT, size 0x30). The main loop's `app_sched_execute`
  (0x404CC) runs the encoders 0x41204 (type 0x02) / 0x412BC (type 0x08).
- **Scheduler (confirmed, main 0x415C6):** `app_sched_init(event_size 264, queue_size 96)`, buffer at 0x20006888
  (97 x 8-byte headers, then 97 x 264 bytes, about 26 KB). SDK 14.2 uses 16 (`SER_CONN_SCHED_QUEUE_SIZE`):
  Bryton raised it to 96. `APP_SCHEDULER_WITH_PAUSE` is in (pause 0x40464, resume 0x40490).
- **One event in flight:** after sending an event the encoder pauses the scheduler; TX done resumes it. A command
  header arriving pauses it too (RX_PKT_RECEIVING), and its response's TX done resumes it. So events go out one
  frame at a time, never two queued in the UART, and no new event starts between a command's header and its
  response (an event already being sent finishes first). The pause count is balanced for every command that gets
  a response (confirmed in 0x41678; SDK counter semantics inferred).
- **Queue full (confirmed):** after each put, if the free space is 0 the observer calls `nrf_sdh_suspend` (0x3FD24,
  disables the SWI2 IRQ); the main loop resumes it when more than 48 slots are free (0x30, main 0x415C6). But
  the poll loops (0x41E1C, 0x41E80, SDK `nrf_sdh_ble.c` / `nrf_sdh_ant.c`) do not look at the suspend flag: they
  keep pulling every event the SoftDevice holds in that pass. **If one pass brings more events than the free
  slots, `app_sched_event_put` returns NO_MEM and `APP_ERROR_CHECK` hangs the chip.** Within one pass the order
  is the stack observers' (BLE, then ANT; SoC events are not forwarded).
- **While suspended:** the SoftDevice keeps its own queues. ANT's event queue is the size the host asked for in
  the ANT enable (stock and rustfw: 32); on resume the ANT poll can bring up to that many in one pass, after all
  pending BLE events.
- **Event drop (other than the hang):** none. Every event pulled is either queued or fatal.
- **Largest BLE event (confirmed):** 262 bytes as pulled from the SoftDevice (`sd_ble_evt_get` length 0x106). A
  larger event makes `sd_ble_evt_get` fail (DATA_SIZE), which is not NOT_FOUND, so `APP_ERROR_HANDLER` hangs the
  chip (0x41E1C tail). 262 matches SDK `BLE_EVT_LEN_MAX(ATT_MTU)` for an ATT MTU of about 125 to 128 (inferred
  arithmetic). Serialized, an event must fit the 384-byte TX buffer. A GATTC HVX of 100 or 150 bytes is about
  18 + length bytes as an event: **fits** (inferred layout). With an ATT MTU of 153 (stock) the largest events
  (a full primary service discovery response, BLE_EVT_LEN_MAX(153) about 318) could exceed 262 (inferred); at
  MTU 103 nothing realistic does.
- **BLE events encoded (confirmed, encoder switch 0x3F444):** 0x01, 0x02, GAP 0x10..0x24, GATTC 0x30..0x3C, GATTS
  0x50..0x57, L2CAP 0x70..0x77: every API v5 event. An id outside these returns NOT_SUPPORTED, which the encoder
  answers with `APP_ERROR_CHECK(SER_WARNING_CODE)`: hang (cannot happen with this SoftDevice).
- **ANT events (inferred from SDK `ant_event.c`; encoder 0x41378):** all S332 event codes are encoded; any other
  code would hang the same way.

## 3. Error handling: every fault hangs, nothing resets

- **`app_error_fault_handler` 0x3F414 (confirmed):** enters the SoftDevice critical region (0x3CAA4: application
  IRQs masked, UART included), then returns only if the fault id is 0xBADDCAFE (`SER_WARNING_CODE`); otherwise it
  spins forever (`temp = 1; while (temp);`, the SDK's debug branch of `ser_conn_error_handling.c`; the release
  branch, `NVIC_SystemReset`, was not built).
- **The warning bug (confirmed):** `APP_ERROR_CHECK` calls `app_error_handler_bare` (0x3F018), which always passes
  fault id 0x4001 (NRF_FAULT_ID_SDK_ERROR) with the error code in the info struct. The id never equals
  0xBADDCAFE, so **every `APP_ERROR_CHECK(SER_WARNING_CODE)` hangs as well**. The same is true in the SDK source.
- **SoftDevice asserts (confirmed):** `sd_softdevice_enable` gets 0x3F415 as its fault handler (literal at
  0x3FDE8): a SoftDevice assert hangs the same way.
- **CPU exceptions (confirmed):** NMI 0x2F8B6, HardFault 0x2F95E, MemManage 0x318B2, BusFault 0x3411A, UsageFault
  0x350E6, SVC 0x3899A, DebugMon 0x39556, PendSV 0x3A9AA, SysTick 0x3C2E2 are each a bare `B .`.
- **Watchdog: none (confirmed):** no reference to WDT (0x40010000) anywhere in the image. A hung chip stays hung
  until its reset pin (our PA6) or power.
- **What a hung chip looks like (inferred from the nRF52832 UARTE description):** no frame of any type; the UART
  IRQ is masked, so after the header DMA in flight completes the UARTE stops reading and drops its RTS once its
  4-byte FIFO fills: our CTS goes high after about 2 to 6 bytes and stays high. In the app_error hang the
  SoftDevice still runs (its interrupts are not masked): the phone's BLE link and ANT channels stay up on the
  air, with nothing reaching us. After a HardFault the SoftDevice stops too, so the phone link drops after its
  supervision time-out. That difference tells the two apart on a ride.
- **Alive but silent (other than the hangs):** only while our RTS is held (TX waits, no time-out, recovers when
  released), and DTM (section 4). `sd_app_evt_wait` (SVC 0x3E) is woken by every IRQ, UART included; no path
  waits on a flag that nothing sets.

## 4. The command path

- **Dispatch (confirmed):** `ser_conn_rx_process` 0x416D8 runs from the main loop when the RX flag is set;
  `ser_conn_received_pkt_process` 0x4182C switches on the type byte: 0x00 and 0x06 to the command decoder 0x41958,
  0x03 to DTM 0x4177C, 0x05 to reset 0x41A5C. **Any other type hangs** (`APP_ERROR_CHECK(SER_WARNING_CODE)`).
- **One command at a time (confirmed in the HAL, SDK states inferred):** one RX buffer. After a full packet the
  PHY re-arms only the next 2-byte header; when that header arrives while the previous packet is still
  unprocessed, the payload read waits (state RECEIVED_PENDING_BUF_REQ) and CTS is held until the first command's
  packet is freed after its response is queued. Responses come in order, one per command.
- **Answer:** the decoder allocates the TX buffer (spinning while an event is being sent), writes type 0x01, runs
  the opcode's handler from the table at 0x3FE6C (121 entries of {opcode u32, handler}; lookup 0x41A64, call
  0x41AA0), and sends `[01][op][result u32][outputs]`. ANT commands (type 0x06) are answered as type 0x01 too.
- **Unknown opcode (confirmed, 0x41958):** handler lookup fails, NOT_SUPPORTED, then
  `APP_ERROR_CHECK(SER_WARNING_CODE)` **before** the NOT_SUPPORTED response is sent: the chip hangs, no response.
- **Malformed arguments (confirmed for 0x9E and 0x69):** decoders check the opcode and that the parsed length
  equals the packet length exactly (`SER_ASSERT` enabled in this build); any mismatch (or a type-only packet,
  0x41958 returns 9 for length 0) makes the handler fail, the command path returns NRF_ERROR_INTERNAL without a
  response, and the main loop's `APP_ERROR_CHECK` hangs the chip. A SoftDevice error (NO_MEM, INVALID_PARAM, ...)
  is not a fault: it comes back as the response's result.
- **Opcodes present (confirmed, table 0x3FE6C):** SoC 0x32 (power off), 0x43, 0x49; common 0x60..0x69 (0x69
  `sd_ble_cfg_set`, handler 0x3EAFA); L2CAP 0xB0..0xB4; GAP 0x6C..0x90 (0x86 listed twice, same handler);
  GATTC 0x94..0x9E (0x9E is `sd_ble_gattc_exchange_mtu_request`, handler 0x3E688, SVC 0x9E: our inferred opcode
  is right; request = `9E [conn u16] [client_rx_mtu u16]`, exactly 5 bytes); GATTS 0xA0..0xAD; ANT 0xC0, 0xC2..0xC8,
  0xCC..0xDB, 0xDD..0xE1, 0xE7, 0xE9..0xEC, 0xF2..0xF5, 0xF8..0xFA. No custom (Bryton) opcode.
- **`sd_ble_cfg_set` 0x69 (confirmed, decoder 0x38782):** ids 0x01 (VS UUID), 0x20..0x24 (connection: GAP, GATTC,
  GATTS, GATT = ATT MTU, L2CAP), 0x40 (role count), 0x41 (device name), 0xA0 (service changed), 0xA1 (attribute
  table). A connection id 0x20..0x23 carries a tag byte after the presence byte; so ATT MTU is exactly
  `69 [23 00 00 00] [01] [tag] [mtu u16]`, 9 bytes. An unknown id has no decoder (SDK bug: the default case does
  not return); what follows then was not traced. The RAM limit it passes is 0x20006800: a configuration that does
  not fit comes back as NO_MEM in the response (inferred from the S332 API), not a fault.
- **Packets with no response (confirmed):** type 0x05 (the chip resets), op 0x32 power off (0x41BF0 closes the
  transport, then SVC 0x32), type 0x03 DTM (response, then `ser_conn_is_ready_to_enter_dtm` 0x41806 disables the
  SoftDevice, closes the transport and runs DTM: silent to this protocol until reset).
- **Security contexts (inferred from SDK `conn_ble_gap_sec_keys.c`):** each `sd_ble_gap_sec_params_reply` (0x7F)
  takes one of 8 slots (`SER_MAX_CONNECTIONS`), freed by that link's AUTH_STATUS or DISCONNECTED event. A ninth
  reply with all slots taken fails to decode: hang.

## 5. What Bryton changed from Nordic's ANT + BLE connectivity example

- UART parity off (above); pins.
- Scheduler queue 96 instead of 16; BLE event buffer 262 bytes (SDK default MTU 23 gives far less).
- `sd_ble_version_get` (handler 0x3E856, confirmed): after the SVC it overwrites the three fields with its own
  versions: version_number = a byte of initialised data (0x20006886, 4 on R039: the "Image 4" stock logs),
  company_id = the u16 at flash 0x731E8 (outside the app, likely the bootloader's version: "Bootloader 1",
  inferred), subversion = the SoftDevice info's SD_VERSION (0x3014, when the info size at 0x3000 is at least 0x15)
  divided by 100 000 ("SD 50" = v5.0).
- GPIO: P0.24 configured as an output and set high once at start, P0.03 cleared once (OUTCLR; no direction set
  in the app). Nothing toggles a GPIO afterwards. If PH8 (our "request line") is wired to P0.24 it carries no
  information; if to another nRF pin, this app never drives it (wiring not known: inferred).
- The ANT licence key passed to the SoftDevice is Bryton's (licensed: not reproduced).
- No logging (NRF_LOG compiled out), no custom commands, no sleep mode of its own, no watchdog; the debug
  `while (temp)` error handler was shipped.
- ANT events: type 0x08, `ant_evt_t` encoded by the SDK's `ant_evt_t_enc` (44-byte payload, layout as seen on the
  unit: rustfw BOARD.md 11).

## Model parameters for the simulator

- **Frames:** 2-byte LE length + payload; payload 1..384 bytes each way. Chip-to-host: `01` responses, `02` BLE
  events, `08` ANT events only.
- **Host-to-chip, fatal (the chip hangs forever, see "hung" below):** payload length 0 or over 384; type not in
  {00, 03, 05, 06}; type-only payload; an opcode not in the table; any command whose arguments do not parse to
  exactly the payload length; a UART parity or overrun error; a framing or break error inside a payload (hang on
  the next packet); a 9th `sec_params_reply` without AUTH_STATUS or DISCONNECTED in between.
- **No response:** type 05 (reset), op 0x32 (off), type 03 (DTM, then silent until reset).
- **One command at a time:** after a packet, the chip reads the next 2-byte header and then holds CTS until the
  previous command's response has been queued. Responses in order, one each, `[01][op][u32 result][outputs]`.
- **TX:** one frame at a time, waits on our RTS with no time-out; resumes exactly where it stopped.
- **Events:** a queue of 96 events (each up to 264 bytes). One event frame in flight; none starts between a
  received command header and that command's response (one already in flight finishes first). The chip pulls
  events from the SoftDevice in passes (all pending BLE, then all pending ANT); after a put that leaves 0 free
  slots it stops pulling new passes until more than 48 are free. **If a pass brings more events than the free
  slots: hang.** The SoftDevice's ANT queue (32, from the ANT enable) buffers ANT events while suspended.
- **Largest BLE event:** 262 bytes before encoding; larger: hang.
- **Hung:** never sends another byte; accepts about 2 to 6 more bytes then holds CTS high; the BLE link and ANT
  channels stay up on the air (app hang) or drop (HardFault); only PA6 reset or power recovers it. No watchdog.
- **Throughput:** the drain rate is ours: one frame per completed TX, so the event rate the chip can sustain is
  our read rate divided by the frame size (an ANT event is 46 bytes on the wire).

## Which of the ride's conditions could silence the chip, most likely first

1. **Event queue overflow under a throttled drain (mechanism confirmed, trigger inferred).** Three ANT channels
   at about 4 Hz (12 events a second, 46 bytes each) plus the iPhone's ANCS / AMS traffic (GATTC notifications,
   write responses, HVN TX complete for anything we send) arrive faster than a host that reads every 100 ms and
   holds RTS in between can take them; the 96 slots fill, and the first SoftDevice pass with more events than
   free slots hangs the chip. The burst that tips it can come at about 30 s: our low-priority search time-out is
   12 (30 s), so every channel still searching after a reset times out together and the ANT poll brings
   EVENT_RX_SEARCH_TIMEOUT plus EVENT_CHANNEL_CLOSED for each in one pass; a resume after suspension brings up to
   32 ANT events plus all pending BLE ones in one pass too.
2. **A command the chip cannot decode (confirmed mechanism).** An opcode outside the table, or one byte too many
   or too few in any command (a rarely sent one: the MTU exchange, a cfg_set, a GATT write to the phone, a
   channel reconfiguration after the 30 s search time-out), hangs it with no response; the next probes go
   unanswered.
3. **A UART error on the chip's RX (confirmed mechanism, trigger unknown):** parity or overrun hangs at once; a
   framing or break error inside a command hangs at the next one. Overrun needs our TX to ignore its CTS; a break
   or framing error needs a glitch on PD5 (pin reconfiguration, USART2 restarted mid-frame).
4. **A BLE event over 262 bytes (confirmed mechanism):** only with a large ATT MTU and a large GATT response;
   unlikely at MTU 103.
5. **Security context leak (inferred):** more than 8 `sec_params_reply` without the link's AUTH_STATUS or
   DISCONNECTED in between; unlikely on one phone.
6. **SoftDevice assert or CPU fault (confirmed to hang, trigger unknown).** The phone link dropping at the moment
   of silence would point here rather than to 1 to 5.

Holding RTS for 100 ms by itself is safe: the chip waits without a time-out and carries on. It becomes fatal only
through 1, when the hold lasts long enough (in total, against the event rate) to fill the queue.
