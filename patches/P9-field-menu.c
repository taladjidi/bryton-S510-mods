// P9, field menu part (was P11; tested on the unit, both menus): "DFA a1" (field 12) can be picked on the unit, as the last item of the Heart
// Rate category, both in the ride-screen field menu (long press on a field) and in Settings > data fields. The field
// itself is P9-live-dfa-a1.c; both sources make the one P9 patch.
//
// How the menus work (both menus have the same tables, in code): a category is chosen, the item count comes from
// a compare chain (Heart Rate: 18 items), each item's name from a switch on (category, item) returning a title
// object, and picking item i writes the field ID of (category, i), again a switch, into the page's grid slot
// (global setting + 0x15954 + page * 0x30 + grid * 4) and saves the page to System/BikeNProfile.ini.
// - the Heart Rate count, 18 -> 19 (one immediate in each count function);
// - the two name functions: the new last item -> field 12's title object, else as before;
// - the two "pick" functions: the new item -> write field 12 into the slot, then let Bryton's code save
//   the page: with an unknown category (0x17) it writes nothing and saves the slot as it is.
// The four functions are entered through a b.w at their first instruction; the wrapper re-runs that instruction.
//
// Build: { python3 tools/asm2patch.py patches/P9-live-dfa-a1.c; python3 tools/asm2patch.py patches/P9-field-menu.c; }
// @cave 0xd03da000          after the stock image (build.py grows the image, see GROW_END)
// @jump 0xd0178e78 map_hook  MeterSetDataField_MapItemToFieldId entry (push.w) -> map_hook
// @jump 0xd01206ec assign_hook  Page_AssignGridFieldType entry (push.w) -> assign_hook
// @jump 0xd0178520 meter_name_hook  MeterSetDataField_GetItemName entry -> meter_name_hook
// @jump 0xd011fd98 settings_name_hook  SettingssetDataField_GetFieldName entry -> settings_name_hook
// @bytes 0xd01797fc 1321  MeterSetDataField_SetCategory: Heart Rate 18 -> 19 items (moveq r1,#0x13)
// @bytes 0xd011fb1e 1321  SettingssetDataField_InitItemCount: Heart Rate 18 -> 19 items (moveq r1,#0x13)
// @sym map_body 0xd0178e7c
// @sym assign_body 0xd01206f0
// @sym meter_name_body 0xd0178526
// @sym settings_name_body 0xd011fd9e
// @sym EwGetVariantOfString_veneer 0xd0105944
#include <stdint.h>

extern uint32_t EwGetVariantOfString_veneer(const void *title);

// The new item: Heart Rate (category 6) item 18 = field 12, title object 0xd0054210 ("DFA a1" with P9)
#define NEW_CAT 6
#define NEW_ITEM 18
#define NEW_FIELD 12
#define NEW_TITLE ((const void *)0xd0054210)
#define NO_CATEGORY 0x17                           // no case in the pick functions: saves the slot unchanged
#define SETTING(obj) (*(uint8_t **)((uint8_t *)(obj) + 0x60))
#define CATEGORY(obj) (*(int *)(SETTING(obj) + 0x14538))
#define SLOT(obj, page, grid) (*(int *)(SETTING(obj) + 0x15954 + (page) * 0x30 + (grid) * 4))

// The stock functions, entered past the b.w: their first instruction(s), then a jump back.
void map_orig(void *t, int item, int cat, int page, int grid);
void assign_orig(void *t, int item, int cat, int page, int grid);
uint32_t meter_name_orig(void *t, int item);
uint32_t settings_name_orig(void *t, int item);
__asm__(".text\n .p2align 1\n .thumb_func\n map_orig: push.w {r2-r7, r9, lr}\n b.w map_body\n"
        " .p2align 1\n .thumb_func\n assign_orig: push.w {r2-r7, r9, lr}\n b.w assign_body\n"
        " .p2align 1\n .thumb_func\n meter_name_orig: ldr r2, [r0, #0x60]\n movw r3, #0x4538\n movt r3, #1\n"
        "  b.w meter_name_body\n"
        " .p2align 1\n .thumb_func\n settings_name_orig: ldr r2, [r0, #0x60]\n movw r3, #0x4538\n movt r3, #1\n"
        "  b.w settings_name_body\n");

static int pick_extra(void *t, int item, int cat, int page, int grid)
{
    if (cat != NEW_CAT || item != NEW_ITEM)
        return cat;
    SLOT(t, page, grid) = NEW_FIELD;
    return NO_CATEGORY;
}

void map_hook(void *t, int item, int cat, int page, int grid)
{
    map_orig(t, item, pick_extra(t, item, cat, page, grid), page, grid);
}

void assign_hook(void *t, int item, int cat, int page, int grid)
{
    assign_orig(t, item, pick_extra(t, item, cat, page, grid), page, grid);
}

uint32_t meter_name_hook(void *t, int item)
{
    return CATEGORY(t) == NEW_CAT && item == NEW_ITEM ? EwGetVariantOfString_veneer(NEW_TITLE)
                                                      : meter_name_orig(t, item);
}

uint32_t settings_name_hook(void *t, int item)
{
    return CATEGORY(t) == NEW_CAT && item == NEW_ITEM ? EwGetVariantOfString_veneer(NEW_TITLE)
                                                      : settings_name_orig(t, item);
}
