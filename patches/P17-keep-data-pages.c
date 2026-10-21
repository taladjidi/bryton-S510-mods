// P17: ride pages are kept in memory instead of being rebuilt at every page change, and Previous Page from a
// remote (Di2 / ESS) no longer rebuilds the whole ride screen. Kept: data pages (Metergrid13), the map page
// (NaviNaviPlay1, page ids 7 and 8, one per map display mode) and the free-climb page (NaviNaviFreeClimb, id 20).
// Stock, every Next / Previous Page releases the data page and builds a new one (Metergrid13: 10 fields, an
// embedded map, timers, touch handlers), and the garbage collector later sweeps the old one: about a fifth of the
// UI task's time (1 ms sampling of the UI task). P17 keeps one data page object per page id; a page change shows the kept object
// again. Its 1 s timer (it refreshes the 10 fields) is stopped while the page is hidden, restarted and run once
// when it is shown, so the values are current at once.
// Lifetime: a hidden page is locked against the garbage collector, holds no reference to the ride screen (its
// callbacks to it are cleared; CreateSubPageById sets them again when the page is shown) and reacts to nothing:
// its 1 s timer and its own system event handlers are switched off (handlers restored as they were when shown). A shown page is
// unlocked and owned by the ride screen as in stock. So when the ride screen goes away (Settings, Quick Status,
// rebuilds), it and its shown page are collected as in stock, and no orphan ride screen stays alive (flash #36:
// an orphan ride screen kept alive by the cache got the "settings changed" event and faulted).
// Any rebuild of the ride screen (settings, layout edits, profile change: all go through MeterMeterPage__Init,
// 0xd017d57e) empties the cache first, so a changed layout is always built fresh. The cache lives in SRAM4 and is
// cleared at every start (the UI task's first call, before any EW object exists).
// Previous Page from a remote: stock (ApplicationApplication, event A78 value 6) dismisses every dialog and builds
// a new ride screen. When the ride screen is the only dialog, P17 calls its own previous-page method instead, the
// same one a swipe uses (otherwise the stock path runs).
// Details: docs/how-it-works.md, "Snappier UI".
//
// Build: python3 tools/asm2patch.py patches/P17-keep-data-pages.c
// @cave 0xd03e1000          after the stock image (build.py grows the image, see GROW_END)
// @hook 0xd028983e page_boot  gui_task: bl EwInit -> page_boot (clear the cache, then EwInit)
// @hook 0xd017db42 new_data  CreateSubPageById, data page: bl EwNewObject_veneer(Metergrid13, 0) -> cached
// @hook 0xd017d724 new_map  CreateSubPageById, ids 7/8: bl EwNewObject_veneer(NaviNaviPlay1, 0) -> cached
// @hook 0xd017da14 new_climb  CreateSubPageById, id 0x14: bl EwNewObject_veneer(NaviNaviFreeClimb, 0) -> cached
// @hook 0xd017de96 page_release  MeterMeterPage next/prev: bl Ui_PageHost_ReleaseAllSubPages -> page_release
// @hook 0xd017d582 page_reset  ride screen build (from MeterMeterPage__Init): bl ReleaseAllSubPages -> page_reset
// @jump 0xd014df5a prev_glue  ApplicationApplication A78, value 6: ldr.w r0,[r4,#0x408] -> prev_glue
// @sym EwInit 0xd02aac0c
// @sym EwNewObject_veneer 0xd010595c
// @sym EwLockObject_veneer 0xd01f1f4c
// @sym EwUnlockObject_veneer 0xd01f1f54
// @sym EwClassOf_veneer 0xd0146cf4
// @sym Ui_PageHost_ReleaseAllSubPages 0xd017dcb4
// @sym CoreTimer_OnSetEnabled 0xd0140a4c
// @sym Metergrid13_OnTimer 0xd01768b4
// @sym NaviPlay1_OnTimer 0xd01dd6a8
// @sym FreeClimb_OnTimer 0xd01ea4ac
// @sym CoreSystemEventHandler_OnSetEnabled 0xd0141478
// @sym CoreView__GetRoot 0xd0138372
// @sym CoreGroup_GetDialogAtIndex 0xd013a66a
// @sym MeterMeterPage_OnPrevPage 0xd017de86
// @sym prev_stock 0xd014df5e
// @sym prev_done 0xd014dfc8
#include <stdint.h>

extern int EwInit(void);
extern void *EwNewObject_veneer(const void *cls, int arg);
extern void EwLockObject_veneer(void *obj);
extern void EwUnlockObject_veneer(void *obj);
extern const void *EwClassOf_veneer(void *obj);
extern void Ui_PageHost_ReleaseAllSubPages(void *page);
extern void CoreTimer_OnSetEnabled(void *timer, int on);
extern void CoreSystemEventHandler_OnSetEnabled(void *handler, int on);
extern void Metergrid13_OnTimer(void *self, void *sender);
extern void NaviPlay1_OnTimer(void *self, void *sender);
extern void FreeClimb_OnTimer(void *self, void *sender);
extern void *CoreView__GetRoot(void *view);
extern void *CoreGroup_GetDialogAtIndex(void *group, int index);
extern void MeterMeterPage_OnPrevPage(void *page, void *sender);

#define CLASS_METER_PAGE ((const void *)0xd032ba70)   // "Meter::MeterPage" (EW_CLASSES.csv)
#define U8(o, off) (*(volatile uint8_t *)((uint8_t *)(o) + (off)))
#define I32(o, off) (*(volatile int32_t *)((uint8_t *)(o) + (off)))
#define PTR(o, off) (*(void **)((uint8_t *)(o) + (off)))
#define PAGE_SETTINGS 0x60                            // MeterMeterPage -> globalSetting
#define PAGE_ID 0x2694                                // globalSetting: current page id
#define HANDLER_ENABLED 0x20                          // CoreSystemEventHandler: Enabled (u8)

// One kind of kept page: where the ride screen (MeterMeterPage) keeps it, its 1 s timer, the run
// of 8-byte slots CreateSubPageById wires to the ride screen, its own system event handlers.
struct kind { uint16_t field, timer, slots, nslots, handler[2]; };
enum { DATA, MAP, CLIMB, KINDS };
static const struct kind K[KINDS] = {
    [DATA] = { 0x64, 0x1860, 0xa0, 4, { 0, 0 } },              // Metergrid13
    [MAP] = { 0x68, 0x9c, 0x74, 5, { 0x313c, 0 } },             // NaviNaviPlay1 (handler: event +0x748)
    [CLIMB] = { 0x78, 0x8c, 0x6c, 4, { 0x2a7c, 0x2aa0 } },      // NaviNaviFreeClimb (events +0x658, +0x988)
};

#define KEYS 40                                       // page ids 0..31, then 32 + map display mode (2, 3, 6)
struct entry { void *obj; uint8_t kind, hidden, handlers_on, pad; };
struct cache { void *owner; struct entry e[KEYS]; };
#define CACHE ((volatile struct cache *)0x38000200)   // SRAM4 0x38000200..0x38000348, unused by the firmware and
                                                      // the other patches

int page_boot(void)
{
    CACHE->owner = 0;
    for (int i = 0; i < KEYS; i++)
        CACHE->e[i].obj = 0, CACHE->e[i].hidden = 0;
    return EwInit();
}

// Hidden (locked) pages are unlocked and left to the collector; the entry of a shown page is only forgotten (that
// page belongs to its ride screen, and may already be collected with it: never dereferenced here).
static void flush(void)
{
    for (int i = 0; i < KEYS; i++) {
        void *o = CACHE->e[i].obj;
        CACHE->e[i].obj = 0;
        if (o && CACHE->e[i].hidden)
            EwUnlockObject_veneer(o);
        CACHE->e[i].hidden = 0;
    }
    CACHE->owner = 0;
}

static void hide(volatile struct entry *e)
{
    const struct kind *k = &K[e->kind];
    uint8_t *o = e->obj;
    CoreTimer_OnSetEnabled(o + k->timer, 0);
    uint8_t on = 0;
    for (int h = 0; h < 2; h++)
        if (k->handler[h]) {
            on |= (U8(o + k->handler[h], HANDLER_ENABLED) ? 1 : 0) << h;
            CoreSystemEventHandler_OnSetEnabled(o + k->handler[h], 0);
        }
    e->handlers_on = on;
    uint32_t *slots = (uint32_t *)(o + k->slots);
    for (int w = 0; w < 2 * k->nslots; w++)
        slots[w] = 0;
    EwLockObject_veneer(o);
    e->hidden = 1;
}

static void show(volatile struct entry *e)
{
    const struct kind *k = &K[e->kind];
    uint8_t *o = e->obj;
    e->hidden = 0;
    EwUnlockObject_veneer(o);
    for (int h = 0; h < 2; h++)
        if (k->handler[h] && (e->handlers_on >> h & 1))
            CoreSystemEventHandler_OnSetEnabled(o + k->handler[h], 1);
    CoreTimer_OnSetEnabled(o + k->timer, 1);
    // the timer's own handler once, so the values are current at once (direct calls: no function pointers here)
    if (e->kind == DATA)
        Metergrid13_OnTimer(o, 0);
    else if (e->kind == MAP)
        NaviPlay1_OnTimer(o, 0);
    else
        FreeClimb_OnTimer(o, 0);
}

static int key_of(void *page, int kind)
{
    void *gs = PTR(page, PAGE_SETTINGS);
    int id = (int16_t)I32(gs, PAGE_ID);
    if (kind != MAP)
        return id;
    // the map display mode CreateSubPageById just set (SetNaviDisplayMode): one kept map per mode
    if (id == 7)
        return 32 + (I32(gs, 0x1868c) == 9 ? 2 : 3);
    if (id == 8)
        return 32 + (I32(gs, 0x28ac) == 1 ? 6 : 2);
    return -1;
}

// The page of this kind for the current page id: the kept one (shown again, timer on, refreshed now) or a new one.
void *page_new(const void *cls, int arg, void *page, int kind)
{
    int key = key_of(page, kind);
    if (key < 0 || key >= KEYS)
        return EwNewObject_veneer(cls, arg);
    if (CACHE->owner != page) {
        flush();
        CACHE->owner = page;
    }
    volatile struct entry *e = &CACHE->e[key];
    if (e->obj && e->hidden && e->kind == kind) {
        show(e);
        return e->obj;
    }
    void *o = EwNewObject_veneer(cls, arg);
    e->obj = o;
    e->kind = (uint8_t)kind;
    e->hidden = 0;
    return o;
}

// Next / previous: the outgoing page is hidden (timer and handlers off, callbacks to the ride screen cleared,
// locked), then the stock release (dismiss, remove from the group).
void page_release(void *page)
{
    if (page == CACHE->owner)
        for (int k = 0; k < KINDS; k++) {
            void *o = PTR(page, K[k].field);
            for (int i = 0; o && i < KEYS; i++)
                if (CACHE->e[i].obj == o && CACHE->e[i].kind == k && !CACHE->e[i].hidden) {
                    hide(&CACHE->e[i]);
                    break;
                }
        }
    Ui_PageHost_ReleaseAllSubPages(page);
}

// A ride screen is being built: forget every kept page (layout, profile or settings may have changed).
void page_reset(void *page)
{
    flush();
    CACHE->owner = page;
    Ui_PageHost_ReleaseAllSubPages(page);
}

// Previous Page from a remote: if the ride screen is the only dialog, its own previous-page method.
int prev_page(void *app)
{
    void *root = CoreView__GetRoot(app);
    if (!root)
        return 0;
    void *top = CoreGroup_GetDialogAtIndex(root, 0);
    if (!top || CoreGroup_GetDialogAtIndex(root, 1) || EwClassOf_veneer(top) != CLASS_METER_PAGE)
        return 0;
    MeterMeterPage_OnPrevPage(top, 0);
    return 1;
}

// r4 = the MeterMeterPage (CreateSubPageById) / the ApplicationApplication (A78 handler).
__asm__(".text\n .p2align 1\n .thumb_func\n new_data: mov r2, r4\n movs r3, #0\n b.w page_new\n"
        " .p2align 1\n .thumb_func\n new_map: mov r2, r4\n movs r3, #1\n b.w page_new\n"
        " .p2align 1\n .thumb_func\n new_climb: mov r2, r4\n movs r3, #2\n b.w page_new\n"
        " .p2align 1\n .thumb_func\n prev_glue: mov r0, r4\n bl prev_page\n cbz r0, 1f\n b.w prev_done\n"
        "1: ldr.w r0, [r4, #0x408]\n b.w prev_stock\n");
