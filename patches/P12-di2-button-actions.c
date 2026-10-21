// P12 (tested on the unit, with P7): the Shimano Di2 hood buttons (D-Fly) get the same 10 actions as the SRAM / ESS
// remotes: Lap, Start/Stop, Lock/Unlock, Next Page, Previous Page, Quick Status, Map, First Data Page, Home Page,
// Latest Notification. Stock Di2 offers only the first four (copies of the physical buttons).
//
// How it works (stock): Settings > bike > sensors > Di2 > button setup has four rows (left / right, short / long);
// each row picks 0..4 from a 5-item list, saved as an int in the sensor ini (leftpress, rightpress, ...; no range
// check). Sensor_Di2SetShifterKeyMap turns the choice into a key code and Sensor_Di2Poll posts it to the key mailbox
// when the Di2 page 4 byte of that side changes. The ESS remote code already has an action handler for all 10
// actions, Sensor_EShiftHandleKeyAction, that does not depend on the ESS channel. So:
// - the option list: 5 -> 11 items, captions from the ESS option list;
// - the row value text: captions from the ESS option list (stock switch stops at 4);
// - the key map keeps the raw action number, and the poll hands it to Sensor_EShiftHandleKeyAction.
// No waiting for the sensor poll: sensor_task polls every sensor driver every 100 ms (a press arrived 70-111 ms
// before its action in the debug log). The ANT handler now posts a wake-up mailbox when a page 4 byte changes, and
// sensor_task's 100 ms sleep becomes a wait on that mailbox with the same timeout: a press wakes it at once, it runs
// the Di2 press check only, then sleeps the rest of its 100 ms (the other drivers keep their exact cadence). The
// actions themselves still run in sensor_task, as before; the ANT task only stores and posts.
// With P7 (media remote) also installed, five more actions drive the phone's media: 11 Play/Pause, 12 Next Track,
// 13 Previous Track, 14 Volume +, 15 Volume - (P7's media_remote_press, found at its fixed address; without P7
// the list stays at 10 actions).
//
// Build: python3 tools/asm2patch.py patches/P12-di2-button-actions.c
// @cave 0xd03db000          after the stock image (build.py grows the image, see GROW_END)
// @jump 0xd0288d2a di2_keymap  Sensor_Di2SetShifterKeyMap entry -> di2_keymap
// @jump 0xd0288d8c di2_poll  Sensor_Di2Poll entry -> di2_poll
// @jump 0xd02a27da di2_ant_page4  Ant_Di2_OnAntEvent, page 4: "ldrb r0,[r4,#4]; ldr r1,=0x24010599" -> di2_ant_page4
// @hook 0xd0289672 wake_init  sensor_task start: bl Vector_InitLock -> wake_init (same call, then create the mailbox)
// @hook 0xd02896d8 sensor_wait  sensor_task loop: bl OSTimeDlyHMSM(0,0,0,ms) -> sensor_wait
// @jump 0xd01156fc option_caption  SettingsRemoteSetupOption_GetOptionCaption entry -> ESS + media captions
// @hook 0xd011537a option_count  SettingsRemoteSetupOption__Init: "bl set 5 items" -> 11 items (16 with P7)
// @jump 0xd0115030 row_caption  SettingsRemoteSetup row value: "cmp r2,#4; bhi" -> option_caption of r2
// @sym Sensor_Di2IsEnabledAndOpen 0xd0288cae
// @sym Util_IntervalElapsed 0xd027c534
// @sym Sensor_Di2ReadData 0xd0288cc8
// @sym Sensor_EShiftHandleKeyAction 0xd02891dc
// @sym SettingsESSRemoteSetupOption_GetOptionCaption 0xd0137760
// @sym List_SetNoOfItems_r5 0xd012de1a
// @sym row_caption_done 0xd0115064
// @sym EwNewStringAnsi 0xd0105964
// @sym media_remote_press 0xd03d6c34       P7's function (its cave, fixed by P7's .patch bytes)
// @sym Ui_BeepIfEnabled 0xd025528e
// @sym Ui_PostEvent 0xd01f1f5c
// @sym ant_di2_done 0xd02a2822
// @sym Vector_InitLock 0xd026e6f2
// @sym OSMboxCreate 0xd01fb2dc
// @sym OSMboxPend 0xd01fb31e
// @sym OSMboxPost 0xd01fb420
// @sym OSTimeGet 0xd020cd04
// @sym OSTimeDlyHMSM 0xd020cc98
#include <stdint.h>

extern int Sensor_Di2IsEnabledAndOpen(void);
extern int Util_IntervalElapsed(void *timer);
extern void Sensor_Di2ReadData(void);
extern void Sensor_EShiftHandleKeyAction(int action);
extern void *SettingsESSRemoteSetupOption_GetOptionCaption(void *t, unsigned i);
extern void *EwNewStringAnsi(const char *s);
extern uint32_t media_remote_press(unsigned bit);
extern void Ui_BeepIfEnabled(int pattern);
extern int Ui_PostEvent(int event, int value);
extern void Vector_InitLock(void);
extern void *OSMboxCreate(void *msg);
extern void *OSMboxPend(void *mbox, uint16_t timeout, uint8_t *err);
extern uint8_t OSMboxPost(void *mbox, void *msg);
extern uint32_t OSTimeGet(void);
extern uint8_t OSTimeDlyHMSM(int h, int m, int s, unsigned ms);
#define OSTimeDly_ms(ms) OSTimeDlyHMSM(0, 0, 0, ms)              // 1 tick = 1 ms (OSTimeDlyHMSM: ticks = ms)

#ifndef P7_INSTALLED   // P7's first 8 bytes at media_remote_press (push {r1-r7,lr}; movs r1,#1; ...): else zeros
#define P7_INSTALLED (((const volatile uint32_t *)0xd03d6c34)[0] == 0x2101b5fe && \
                      ((const volatile uint32_t *)0xd03d6c34)[1] == 0x0217f10d)
#endif
#define MEDIA_FIRST 11                                // 11..15: media_remote_press bits 0..4
#define NEXT_PAGE 4
#define WAKE_KEY 0x44693257u                          // "Di2W"

#ifndef DI2                                    // (tests/test_tools.py builds this for the host with its own)
#define DI2 ((volatile uint8_t *)0x2400fd48)   // driver state: +0 state (5 = open), +0xc..0xf actions, +0x10/0x11 last bytes
#define DI2_TIMER ((void *)0x2400fd60)
#define RIGHT_PAGE (*(volatile uint8_t *)0x24010599)   // Di2 page 4: high nibble 1 short / 2 long, low nibble counter
#define LEFT_PAGE (*(volatile uint8_t *)0x2401059a)
#define KEY_TONE (*(volatile uint8_t *)0x2406cadc)     // the ESS handler's beep condition
// The wake-up mailbox, in SRAM4 (unused by the firmware; random after power-on, kept over a warm reset): valid
// only with its check word, both written by wake_init as sensor_task's first call, before the sensor manager even
// starts (it opens the Di2 channel), so no page 4 can arrive while they are stale.
#define WAKE (*(void *volatile *)0x38000110)
#define WAKE_CHECK (*(volatile uint32_t *)0x38000114)
#endif
#define R_SHORT 0xc                                   // then R long, L short, L long
#define L_SHORT 0xe

#ifdef __thumb__
// Register glue: the item count (the list is in r5, which C code preserves) and the row value (index in r2,
// caption expected in r1, then back into the stock code).
__asm__(".text\n .p2align 1\n .thumb_func\n option_count: push {r4, lr}\n bl option_total\n mov r1, r0\n"
        "  pop {r4, lr}\n b.w List_SetNoOfItems_r5\n"
        " .p2align 1\n .thumb_func\n row_caption: mov r1, r2\n bl option_caption\n mov r1, r0\n b.w row_caption_done\n"
        " .p2align 1\n .thumb_func\n di2_ant_page4: mov r0, r4\n bl di2_ant\n b.w ant_di2_done\n");
#endif

int option_total(void)
{
    return P7_INSTALLED ? MEDIA_FIRST + 5 : MEDIA_FIRST;
}

void *option_caption(void *t, unsigned i)
{
    static const char *const media[5] = {"Play/Pause", "Next Track", "Previous Track", "Volume +", "Volume -"};
    if (i < MEDIA_FIRST)
        return SettingsESSRemoteSetupOption_GetOptionCaption(t, i);
    return i < MEDIA_FIRST + 5 ? EwNewStringAnsi(media[i - MEDIA_FIRST]) : 0;
}

// Every action gets the ESS handler's short beep (pattern 2, when key tones are on): it beeps for 5..10 itself;
// Lap has its own lap tone (Ride_HandleLapKey). The beep blocks this task ~80 ms, so it comes after the action.
// Next Page posts the ride screen's next-page event itself, as Ride_OnKeyMessage does for the page key: the key
// queue on the way costs ~190 ms (debug log), Previous Page (event 0x65) arrives in 1-2 ms. Ride pages only, like
// Previous Page.
static void run(unsigned action)
{
    if (action == NEXT_PAGE)
        Ui_PostEvent(5, 3);
    else if (action < MEDIA_FIRST) {
        Sensor_EShiftHandleKeyAction((int)action);
        if (action >= 5 || action == 1)
            return;
    } else if (action < MEDIA_FIRST + 5 && P7_INSTALLED)
        media_remote_press(action - MEDIA_FIRST);
    else
        return;
    if (KEY_TONE)
        Ui_BeepIfEnabled(2);
}

void di2_keymap(const uint8_t *cfg)
{
    DI2[L_SHORT] = cfg[0x17];
    DI2[R_SHORT] = cfg[0x18];
    DI2[L_SHORT + 1] = cfg[0x19];
    DI2[R_SHORT + 1] = cfg[0x1a];
}

static void side(uint8_t page, int prev, int actions)
{
    unsigned kind = page >> 4;
    if (page == DI2[prev])
        return;
    DI2[prev] = page;
    if (kind == 1 || kind == 2) {
        uint8_t action = DI2[actions + kind - 1];
        if (action)
            run(action);
    }
}

static void presses(void)
{
    if (DI2[0] != 5)
        return;
    side(RIGHT_PAGE, 0x10, R_SHORT);
    side(LEFT_PAGE, 0x11, L_SHORT);
}

void di2_poll(void)
{
    if (Sensor_Di2IsEnabledAndOpen() && Util_IntervalElapsed(DI2_TIMER))
        Sensor_Di2ReadData();
    presses();
}

static void *wake(void)
{
    void *m = WAKE;
    return WAKE_CHECK == ((uint32_t)(uintptr_t)m ^ WAKE_KEY) ? m : 0;
}

void wake_init(void)
{
    Vector_InitLock();
    WAKE_CHECK = 0;
    void *m = OSMboxCreate(0);
    WAKE = m;
    if (m)
        WAKE_CHECK = (uint32_t)(uintptr_t)m ^ WAKE_KEY;
}

// ANT task (Ant_Di2_OnAntEvent, page 4 in msg): store the two bytes as stock does; a changed one wakes sensor_task.
// Nothing else here: this task's stack is small (a printf overflows it) and it serves every ANT sensor.
void di2_ant(const uint8_t *msg)
{
    int changed = msg[4] != RIGHT_PAGE || msg[5] != LEFT_PAGE;
    RIGHT_PAGE = msg[4];
    LEFT_PAGE = msg[5];
    void *m = wake();
    if (changed && m)
        OSMboxPost(m, (void *)1);   // a mailbox holds one message: presses during one wake-up give one wake-up
}

// sensor_task, instead of OSTimeDlyHMSM(0,0,0,ms) between two polls of all sensor drivers: the same sleep, cut
// short by a Di2 press, which is handled at once (the rest of the drivers wait for the next full poll).
void sensor_wait(int h, int m, int s, unsigned ms)
{
    (void)h; (void)m; (void)s;
    void *mb = wake();
    if (!mb) {
        if (ms)
            OSTimeDly_ms(ms);
        return;
    }
    uint32_t end = OSTimeGet() + ms;
    for (;;) {
        uint32_t now = OSTimeGet();
        if ((int32_t)(end - now) <= 0)
            return;
        uint8_t err;
        OSMboxPend(mb, (uint16_t)(end - now), &err);   // 0 would mean forever: excluded above
        if (err == 10)                                  // OS_ERR_TIMEOUT: the usual next poll
            return;
        if (err != 0) {                                 // any other error: plain sleep, never a busy loop
            OSTimeDly_ms(end - now);
            return;
        }
        presses();
    }
}
