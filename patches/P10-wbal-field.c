// P10 (not yet tested on a unit): a working "W' Bal" data field: the anaerobic work capacity left (kJ), from power
// and critical power (Skiba's W' balance, as in the W'bal charts of Golden Cheetah or Intervals.icu).
//
// Bryton's firmware already lists a "W' Bal" field (ID 180, not offered in the field menu) but its value function
// only ever shows "--", and its own W' balance code (0xd0280840) only runs when a W' and CP were sent by the app.
// P10 computes it every second while a ride is recording and shows it in field 180. Choose the field by editing
// the page in System/Bike1Profile.ini (GridN=180) on the unit's drive.
//
// Model, once per second (RidePower_Tick runs at 1 Hz while recording): CP = the FTP set on the unit;
// W' = the W' from the app profile (0x2406cbc4, J) when it looks sane (5-60 kJ), else 20 kJ.
//   above CP: W'bal -= P - CP              (J, 1 s steps)
//   below CP: W'bal += (W' - W'bal) * (CP - P) / W'   (Froncioni/Skiba differential form)
// W'bal starts full at each new ride (ride time went backwards) and never exceeds W'. It may go below 0: that
// means CP or W' are set too low. Paused: not updated (the tick only runs while recording).
//
// Hooks:
// - RidePower_Tick (0xd02809f4) calls Ride_UpdateWorkKj at 0xd0280aaa, after its record-state check (also with
//   P6): that call goes through wbal_tick, which calls it first, then updates W'bal.
// - field table ID 180 -> wbal_bind (callback: kJ, one decimal, unit kJ as "Total Work").
// - MeterMixGridVir_GetFieldWidgetKind (0xd017a658) "cmp r1,#0xb4" -> "cmp r1,#0xff": field 180 is drawn as a plain
//   number instead of Bryton's unfinished W' widget.
//
// Build: python3 tools/asm2patch.py patches/P10-wbal-field.c
// @cave 0xd03d9000          after the stock image (build.py grows the image, see GROW_END)
// @hook 0xd0280aaa wbal_tick  RidePower_Tick: bl Ride_UpdateWorkKj -> bl wbal_tick
// @word 0xd03b3988 wbal_bind  field table, ID 180 ("W' Bal") -> wbal_bind
// @bytes 0xd017a658 ff29  MeterMixGridVir_GetFieldWidgetKind: cmp r1,#0xb4 -> cmp r1,#0xff (field 180 drawn as a number)
// @sym Ride_UpdateWorkKj 0xd0282394
// @sym Data_GetValue 0xd0298670
// @sym g_Cfg_ftp 0x2406cb3e
// @sym g_Cfg_wPrime 0x2406cbc4
#include <stdint.h>

extern float Ride_UpdateWorkKj(void);
extern uint32_t Data_GetValue(uint32_t id, float *out);    // 0x18 ride time (s), 0x4f power (W); 0 = no value
extern volatile uint16_t g_Cfg_ftp;
extern volatile float g_Cfg_wPrime;

#define WB_MAGIC 0x57424131u                                // "WBA1"
struct wbal { uint32_t magic; float w, w0, last_t; };
#define WB ((volatile struct wbal *)0x3800f800)            // SRAM4, after P8's ring (0x3800f000..0x3800f408)

float wbal_tick(void)
{
    float r = Ride_UpdateWorkKj(), t = 0, p = 0, w0 = g_Cfg_wPrime;
    if (!(w0 >= 5000 && w0 <= 60000))
        w0 = 20000;
    Data_GetValue(0x18, &t);
    if (WB->magic != WB_MAGIC || t < WB->last_t) {        // power-on or a new ride
        WB->magic = WB_MAGIC;
        WB->w = w0;
    }
    WB->last_t = t;
    WB->w0 = w0;
    float cp = g_Cfg_ftp, w = WB->w;
    if (cp > 0) {
        if (!Data_GetValue(0x4f, &p) || p < 0)
            p = 0;                                          // no power reading: recovering as at rest
        w += p > cp ? cp - p : (w0 - w) * (cp - p) / w0;
        WB->w = w < w0 ? w : w0;
    }
    return r;
}

// Value callback: ctx + 0x18 -> { float value; uint8_t format; } (format 1 = one decimal, 0xff = "--")
static void wbal_value(uint8_t *ctx)
{
    uint8_t *out = *(uint8_t **)(ctx + 0x18);
    int ok = WB->magic == WB_MAGIC && g_Cfg_ftp;
    *(float *)out = ok ? WB->w / 1000 : -1;
    out[4] = ok ? 1 : 0xff;
}

// Binder, as Bryton's: slot = base + idx * 0x1c + 0x98: callback, 0, unit 0x11 (kJ, as field 80 "Total Work")
void wbal_bind(uint32_t idx, uint32_t unused, uint8_t *base)
{
    (void)unused;
    uint32_t *slot = (uint32_t *)(base + idx * 0x1c + 0x98);
    slot[0] = (uint32_t)wbal_value;
    slot[2] = 0;
    slot[3] = 0x11;
}
