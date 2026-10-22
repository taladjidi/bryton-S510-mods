// Crash guard for patch code (the golden rule in docs/how-it-works.md: a patch must never cost a ride).
//
// Each guarded entry point has a state byte in SRAM4. guard_enter() marks it busy and guard_leave() idle again. If
// the unit restarts while a patch is busy (a fault in the patch, or in firmware code it called), the byte is still
// busy on the next call: that entry point is switched off and its caller takes the stock path instead, until the
// unit is switched off (SRAM4 does not survive that; it does survive the fault restart, and the fault handler
// cleans the data cache before it). So a broken patch costs one restart: Bryton's own recovery then resumes the
// ride from its last 60 s checkpoint (Fit_SaveTempUpdateData) without the patch, instead of crashing again.
//
// Ceiling: a restart for another reason (fault elsewhere, USB plug-in) while a guarded call is preempted also
// switches that entry point off. Harmless: the feature is off until power-off, stock behaviour meanwhile.
#include <stdint.h>

#ifndef GUARD_AT
#define GUARD_AT 0x3800f920u                                // SRAM4: after P9's record flags (0x3800f900, 8 B)
#endif
#define GUARD_MAGIC 0x31445247u                             // "GRD1": SRAM4 is random after power-on
enum { GUARD_IDLE = 0, GUARD_BUSY = 0xb5, GUARD_OFF = 0x0f };
// Entry points, one byte each. Things that must agree (a record definition and its records) share one.
enum { G_P8_ANT, G_P8_BLE, G_P8_HRV, G_P9_FILE, G_P9_FIELD, G_SITES = 16 };
struct guard { uint32_t magic; uint8_t state[G_SITES]; };
#define GUARD ((volatile struct guard *)GUARD_AT)

// 1: run the patch (then call guard_leave), 0: take the stock path.
static inline int guard_enter(int site)
{
    volatile struct guard *g = GUARD;
    if (g->magic != GUARD_MAGIC) {
        for (int i = 0; i < G_SITES; i++)
            g->state[i] = GUARD_IDLE;
        g->magic = GUARD_MAGIC;
    }
    if (g->state[site] != GUARD_IDLE) {                     // busy: the last call never returned
        g->state[site] = GUARD_OFF;
        return 0;
    }
    g->state[site] = GUARD_BUSY;
    return 1;
}

static inline void guard_leave(int site)
{
    GUARD->state[site] = GUARD_IDLE;
}
