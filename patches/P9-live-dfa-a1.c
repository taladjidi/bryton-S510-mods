// P9 (not yet tested on a unit): a live "DFA a1" data field, the aerobic threshold marker from heart rate
// variability (Rogers et al. 2021): about 0.75 at the aerobic threshold (VT1), above 1 when easy, towards 0.5 hard.
// Needs P8 (it collects the beats) and a chest strap that sends RR intervals (ANT+ or Bluetooth).
//
// The field: Bryton's field list has unused "Non" entries; ID 12 becomes "DFA a1". Choose it by editing the page in
// System/Bike1Profile.ini (GridN=12) on the unit's drive. Shows "--" until 64 clean beats are in, or when no beat
// came for 5 s.
//
// How the value is computed, each time the field is drawn (about once a second): the beats of the last 2 minutes
// (P8 keeps the last 512 in SRAM4), artifacts removed with the usual 5 % rule (a beat more than 5 % away from the
// previous kept one is dropped), then detrended fluctuation analysis over box sizes 4 to 16 beats: alpha1 is the
// slope of log F(n) against log n.
//
// Bryton's field mechanism: Field_FormatValueString (0xd0160500) calls the field's "binder" from the table at
// 0xd03b36b8 (one per field ID), which stores a callback and a unit code in a slot; the callback then writes the
// value (float) and a format code (0xff = "--", 2 = two decimals) where the slot's +0x18 pointer says.
// The title: field 12's title object (0xd0054210) points to a one-language string "Non" (block 0xd0000eec,
// index 0x78); the block pointer is changed to a new Embedded Wizard string block below with "DFA a1" at the same
// index.
//
// Build: python3 tools/asm2patch.py patches/P9-live-dfa-a1.c
// @cave 0xd03d8000          after the stock image (build.py grows the image, see GROW_END)
// @word 0xd03b36e8 dfa_bind  field table, ID 12 ("Non") -> dfa_bind
// @word 0xd0054208 dfa_title_block  field 12 title: string block of its only translation -> "DFA a1"
// @sym OSTimeGet 0xd020cd04
#include <stdint.h>

extern uint32_t OSTimeGet(void);                           // uC/OS-II ticks, 1 ms

#define RING_MAGIC 0x52523031u                              // as P8
struct rr_ring { uint32_t magic, head; uint16_t rr[512]; };
#define RING ((volatile struct rr_ring *)0x3800f000)

struct dfa_state { uint32_t head, changed_ms; float x[512]; };   // SRAM4 too, after P8's ring
#define ST ((struct dfa_state *)0x3800e000)

// ln(x) for x > 0: exponent from the float bits, then 2*atanh((m-1)/(m+1)) for m in [1,2). Error < 1e-6.
static float ln(float x)
{
    union { float f; uint32_t u; } v = { x };
    int e = (int)(v.u >> 23) - 127;
    v.u = (v.u & 0x7fffff) | 0x3f800000;
    float t = (v.f - 1) / (v.f + 1), t2 = t * t;
    return e * 0.69314718f + 2 * t * (1 + t2 * (1.f / 3 + t2 * (1.f / 5 + t2 * (1.f / 7 + t2 / 9))));
}

static float sqrtf_(float x)
{
#ifdef __arm__
    float r;
    __asm("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    return __builtin_sqrtf(x);                              // host build, tests/test_tools.py
#endif
}

// DFA alpha1 of n intervals in x (overwritten by the integrated series), box sizes 4..16.
static float dfa_a1(float *x, int n)
{
    float mean = 0, y = 0;
    for (int i = 0; i < n; i++)
        mean += x[i];
    mean /= n;
    for (int i = 0; i < n; i++)
        x[i] = y += x[i] - mean;
    float sl = 0, sf = 0, sll = 0, slf = 0;
    for (int s = 4; s <= 16; s++) {
        // least-squares line through each box (k = 0..s-1), sum of squared residuals
        float sk = s * (s - 1) / 2.f, skk = (s - 1) * s * (2 * s - 1) / 6.f, d = s * skk - sk * sk, f2 = 0;
        int boxes = n / s;
        for (int b = 0; b < boxes; b++) {
            const float *p = x + b * s;
            float sy = 0, sky = 0;
            for (int k = 0; k < s; k++) {
                sy += p[k];
                sky += k * p[k];
            }
            float slope = (s * sky - sk * sy) / d, icpt = (sy - slope * sk) / s;
            for (int k = 0; k < s; k++) {              // residuals in a second pass: floats, keep precision
                float r = p[k] - icpt - slope * k;
                f2 += r * r;
            }
        }
        float l = ln((float)s), f = ln(sqrtf_(f2 / (boxes * s)) + 1e-9f);
        sl += l; sf += f; sll += l * l; slf += l * f;
    }
    return (13 * slf - sl * sf) / (13 * sll - sl * sl);
}

// Value callback: ctx + 0x18 -> { float value; uint8_t format; }
static void dfa_value(uint8_t *ctx)
{
    uint8_t *out = *(uint8_t **)(ctx + 0x18);
    float v = -1;
    uint8_t fmt = 0xff;
    uint32_t now = OSTimeGet(), head = RING->head;
    if (RING->magic == RING_MAGIC) {
        if (head != ST->head) {
            ST->head = head;
            ST->changed_ms = now;
        }
        if (now - ST->changed_ms < 5000) {
            // newest first: keep beats within 5 % of the previous kept one, up to 2 minutes; after 3 drops in a
            // row the reference itself was the odd one: restart from the current beat
            int n = 0, avail = head < 512 ? (int)head : 512, drops = 0;
            float ref = 0, total = 0;
            for (int i = 1; i <= avail && total < 120000; i++) {
                float rr = RING->rr[(head - i) & 511] * (1000.f / 1024);
                total += rr;
                if (ref == 0 || drops == 3 || (rr > ref * 0.95f && rr < ref * 1.05f)) {
                    ST->x[n++] = ref = rr;
                    drops = 0;
                } else
                    drops++;
            }
            if (n >= 64) {
                v = dfa_a1(ST->x, n);
                fmt = 2;
            }
        }
    }
    *(float *)out = v;
    out[4] = fmt;
}

// Binder: same shape as Bryton's (e.g. 0xd039cda4): slot = base + idx * 0x1c + 0x98: callback, 0, unit (0: none)
void dfa_bind(uint32_t idx, uint32_t unused, uint8_t *base)
{
    (void)unused;
    uint32_t *slot = (uint32_t *)(base + idx * 0x1c + 0x98);
    slot[0] = (uint32_t)dfa_value;
    slot[2] = 0;
    slot[3] = 0;
}

// Embedded Wizard string block (tools/ewstr.py encode): a filler entry, then "DFA a1" at index 0x78, then 0.
// Entries are [length + 3, 0x4557] text NUL, and the block must be a chain of them from its start to a 0: the
// string cache (EwDisposeStrings 0x317a) walks it with p += *p to see whether any entry is still in use, and frees
// the block when it reaches the 0. A block starting with NULs is freed while its string is on screen (flash #13/#14:
// the page froze after a minute or on editing the field). Hence a filler entry of spaces up to the index.
__attribute__((aligned(4))) const uint8_t dfa_title_block[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0xed, 0x00, 0xb8, 0x52, 0x04, 0x04, 0x00, 0x83, 0x08, 0x0f, 0x2a, 0x4c, 0xc8,
    0x70, 0xa1, 0xc3, 0x86, 0x10, 0x1f, 0x4a, 0x8c, 0x48, 0x71, 0xa2, 0xc5, 0x8a, 0x18, 0x2f, 0x6a, 0xcc, 0xc8,
    0x71, 0xa3, 0xc7, 0x8e, 0x20, 0x3f, 0x3a, 0x04, 0x00, 0x20, 0xc1, 0xc0, 0x22, 0x44, 0x00, 0x18, 0x01, 0x10,
    0x44, 0x61, 0x18, 0x00, 0x31, 0x48, 0xca, 0x0c, 0x08,
};
