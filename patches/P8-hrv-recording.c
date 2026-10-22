// P8 (tested on a 97 min ride; the SRAM4 ring for P9 and the recovery after missed ANT+ messages are newer, untested): record beat-to-beat heart rate (RR intervals) in the ride file, so HRV and DFA a1
// can be analysed afterwards (Intervals.icu, Runalyze, Kubios...). Written as the standard FIT "hrv" message
// (global 78: up to 5 RR intervals per message, in ms), like Garmin units do. Nothing changes on screen.
//
// Where the beats come from:
// - ANT+ straps: Bryton's ANT+ heart rate decoder (Nordic's) keeps the time of the last beat (1/1024 s) and a
//   beat counter, and only adds them to running totals. Around its call, one new beat = one RR interval. After
//   missed messages (the channel runs at 4 Hz, so radio losses), the time of the beat before the last one, which
//   straps send on data page 4, still gives the last interval, and with two new beats the first one too.
// - Bluetooth straps: Nordic's heart rate client already decodes the RR intervals of each notification (up to 20,
//   1/1024 s) into the event it hands to Bryton's handler, which only reads the heart rate.
// RR intervals outside 250-2500 ms (24-240 bpm) are dropped as glitches.
//
// Where they go: right after Bryton writes each 1-second record to the activity file, the RR intervals queued
// since then are written as hrv messages to the same file (local message 10, which Bryton never uses; its
// definition is written before each message, so the file stays valid however Bryton opens or resumes it).
// The queue lives in RTC backup registers 21-31 (writable, unused by Bryton; P5 uses 8, P7 20): the image area
// may not be writable. If nothing drains it (not recording), it starts over instead of keeping stale beats.
// The beats also go to a 512-beat ring in SRAM4 for the live DFA a1 field (P9); without P9 nothing reads it.
//
// Build: python3 tools/asm2patch.py patches/P8-hrv-recording.c
// @cave 0xd03d7000          right after the stock image (build.py grows the image, see GROW_END)
// @hook 0xd02898d8 ant_hr_process  HRHighANT_Task: bl Ant_Hr_ProcessData -> bl ant_hr_process
// @hook 0xd021b632 write_record_then_hrv  HRLog_Sensor_Point: bl Fit_WriteRaw (1-second record) -> bl write_record_then_hrv
// @word 0xd029e634 ble_hrs_evt  Ble_App_HrsCInit: Bluetooth heart rate event handler (pool word) -> ble_hrs_evt
// @sym Ant_Hr_ProcessData 0xd029fc02
// @sym Ble_App_HrsCEvtHandler 0xd029e520
// @sym Fit_WriteRaw 0xd02942ca
// @sym Fit_WriteDefinitionMsg 0xd0294284
// @sym Fit_WriteDataMsg 0xd02942aa
// @sym g_AntHr_lastBeatCount 0x3003e60f
// @sym g_AntHr_lastBeatTime 0x3003e5ae
// @sym g_AntHr_prevBeatTime 0x3003e472
#include <stdint.h>
#include "guard.h"                                         // crash guard: a fault here costs one restart

extern uint32_t Ant_Hr_ProcessData(void *evt, void *a, void *b, void *c);
extern void Ble_App_HrsCEvtHandler(void *hrs_c, const uint8_t *evt);
extern void Fit_WriteRaw(const void *buf, uint32_t len, void *file);
extern void Fit_WriteDefinitionMsg(uint32_t local, const void *def, uint32_t len, void *file);
extern void Fit_WriteDataMsg(uint32_t local, const void *data, uint32_t len, void *file);
extern volatile uint8_t g_AntHr_lastBeatCount;     // ANT+ page byte 6: heart beat count
extern volatile uint16_t g_AntHr_lastBeatTime;     // bytes 4-5: time of the last beat, 1/1024 s
extern volatile uint16_t g_AntHr_prevBeatTime;     // page 4 bytes 2-3: time of the beat before it

#define BKP(n) (((volatile uint32_t *)0x58004050)[n])      // RTC backup registers
enum { COUNT = 21, FIRST = 22, SLOTS = 10 };               // queue: count in 21, RR (1/1024 s) in 22-31

// The last 512 beats also go to a ring in SRAM4 (64 KB at 0x38000000, never used by Bryton's code), for the live
// DFA a1 field (P9). Contents are random after power-on, hence the magic.
#define RING_MAGIC 0x52523031u                              // "RR01"
struct rr_ring { uint32_t magic, head; uint16_t rr[512]; };
#define RING ((volatile struct rr_ring *)0x3800f000)

static uint32_t irq_off(void)
{
    uint32_t p;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(p) : : "memory");
    return p;
}

static void irq_restore(uint32_t p)
{
    __asm volatile("msr primask, %0" : : "r"(p) : "memory");
}

static void push_rr(uint32_t rr)
{
    if (rr < 256 || rr > 2560)                             // 250-2500 ms
        return;
    uint32_t p = irq_off(), n = BKP(COUNT);
    if (n >= SLOTS)                                        // full (not recording) or garbage after a reset
        n = 0;
    BKP(FIRST + n) = rr;
    BKP(COUNT) = n + 1;
    if (RING->magic != RING_MAGIC) {
        RING->magic = RING_MAGIC;
        RING->head = 0;
    }
    RING->rr[RING->head++ & 511] = rr;
    irq_restore(p);
}

// ANT+: replaces Bryton's call to Ant_Hr_ProcessData (same arguments, same result).
uint32_t ant_hr_process(void *evt, void *a, void *b, void *c)
{
    if (!guard_enter(G_P8_ANT))
        return Ant_Hr_ProcessData(evt, a, b, c);
    uint8_t c0 = g_AntHr_lastBeatCount;
    uint16_t t0 = g_AntHr_lastBeatTime;
    uint32_t r = Ant_Hr_ProcessData(evt, a, b, c);
    uint8_t beats = (uint8_t)(g_AntHr_lastBeatCount - c0);
    uint16_t t1 = g_AntHr_lastBeatTime, p = g_AntHr_prevBeatTime, dp = (uint16_t)(p - t0);
    if (beats == 1)
        push_rr((uint16_t)(t1 - t0));
    else if (beats >= 2 && dp && dp < (uint16_t)(t1 - t0)) {   // page 4 time falls between: fresh
        if (beats == 2)
            push_rr(dp);                                     // 3+ beats: that first span holds several, dropped
        push_rr((uint16_t)(t1 - p));
    }
    guard_leave(G_P8_ANT);
    return r;
}

// Bluetooth: Bryton's heart rate client event handler, with the RR intervals queued first.
// Event (Nordic ble_hrs_c_evt_t): type at 0 (2 = notification), RR count at 6, RR[20] at 8.
void ble_hrs_evt(void *hrs_c, const uint8_t *evt)
{
    if (evt[0] == 2 && guard_enter(G_P8_BLE)) {
        for (uint32_t i = 0; i < evt[6] && i < 20; i++)
            push_rr(((const uint16_t *)(evt + 8))[i]);
        guard_leave(G_P8_BLE);
    }
    Ble_App_HrsCEvtHandler(hrs_c, evt);
}

// hrv definition: reserved, little-endian, global message 78, 1 field: #0 "time", 10 bytes = 5 x uint16 (0x84)
static const uint8_t hrv_def[] = { 0, 0, 78, 0, 1, 0, 10, 0x84 };

// After each 1-second record: the queued RR intervals as hrv messages (ms, unused slots 0xffff = invalid).
void write_record_then_hrv(const void *buf, uint32_t len, void *file)
{
    uint32_t rr[SLOTS];
    Fit_WriteRaw(buf, len, file);
    if (!guard_enter(G_P8_HRV))
        return;
    uint32_t p = irq_off(), n = BKP(COUNT);
    if (n > SLOTS)
        n = 0;
    for (uint32_t i = 0; i < n; i++)
        rr[i] = BKP(FIRST + i);
    BKP(COUNT) = 0;
    irq_restore(p);
    // ponytail: definition before every message (+9 B each, ~30 KB/hour); write it once per file if size matters
    for (uint32_t i = 0; i < n; i += 5) {
        uint16_t t[5];
        for (uint32_t k = 0; k < 5; k++)
            t[k] = i + k < n ? (uint16_t)((rr[i + k] * 1000 + 512) >> 10) : 0xffff;
        Fit_WriteDefinitionMsg(10, hrv_def, sizeof hrv_def, file);
        Fit_WriteDataMsg(10, t, sizeof t, file);
    }
    guard_leave(G_P8_HRV);
}
