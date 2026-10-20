// P7: the S510 becomes a Bluetooth media remote for the paired phone.
//
// Adds a standard HID-over-GATT service (Bluetooth SIG "HID", 0x1812) with one consumer-control report:
// Play/Pause, Next, Previous, Volume up, Volume down. The phone's OS handles the keys (whatever app is playing),
// so nothing is needed on the phone.
// Buttons: the overview's Light Control widget, renamed "Multimedia Control" in Quick Status. Switched on, with no smart light
// connected, its first light box is shown as a "Media" box: |◀◀  ▶‖  ▶▶| on the top row, ♪− ♪+ centred below.
// With a light connected the widget is Bryton's, unchanged.
//
// How it fits in the firmware: the STM32 drives the Nordic radio through Nordic's serialization (each
// sd_ble_* call is packed and sent over UART). Ble_ServicesInit (0xd0251372) registers Bryton's services at
// start-up; the hook below makes its last call (Ble_LdcSvcInit) go through ldc_then_hid(), which registers
// Bryton's service exactly as before and then this one. Every error here is ignored: the worst case is no
// HID service, never Bryton's error handler (which resets the unit).
// The firmware has no encoder for sd_ble_gatts_descriptor_add (Bryton never adds a descriptor), so it is
// built here from the firmware's own serialization helpers, the same way its characteristic_add is.
//
// After installing: on the iPhone, Settings > Bluetooth > the S510 > "Forget This Device", then pair again
// from the Bryton app. Bryton disabled "Service Changed", so the phone keeps its old copy of the service list.
//
// Build: python3 tools/asm2patch.py patches/P7-media-remote.c
// @cave 0xd03d6ad0
// @hook 0xd02513fa ldc_then_hid  Ble_ServicesInit: bl Ble_LdcSvcInit -> bl ldc_then_hid
// @hook 0xd019f83e media_refresh  TapMenu refresh (0xd019f530): bl TapMenu_UpdateItemLayout -> bl media_refresh
// @hook 0xd0269ffc media_or_light  LightControlBox_OnPatternSelected: bl Light_ApplyModeIndex -> bl media_or_light
// @hook 0xd0269f3c header_guard  LightControlBox_OnOpenSensorLight: ldrb r0,[r1,#1]; cbnz r0 -> bl header_guard
// @jump 0xd01a4a32 quick_status_label  Quick Status pages, "Light Control" label: b.w EwGetVariantOfString -> b.w quick_status_label
// Firmware functions (Thumb: odd address where the function is passed as a pointer):
// @sym Ble_LdcSvcInit 0xd029c3e0
// @sym sd_ble_gatts_service_add 0xd0289f32
// @sym sd_ble_gatts_characteristic_add 0xd0289ff2
// @sym sd_ble_gatts_hvx 0xd0289eca
// @sym Ble_SerTxBufAlloc 0xd0289e30
// @sym ser_sd_transport_cmd_write 0xd02ac2be
// @sym uint8_t_enc 0xd02c1108
// @sym uint16_t_enc 0xd02c10c2
// @sym uint16_t_dec 0xd02c10e5
// @sym cond_field_enc 0xd02c1366
// @sym cond_field_dec 0xd02c139e
// @sym ser_ble_cmd_rsp_result_code_dec 0xd02c0ff8
// @sym ble_gatts_attr_t_enc 0xd03a32ad
// @sym g_ble_conn_handle 0x2400ec08
// @sym __aeabi_memclr4 0xd017022c          (clang zeroes structs with it; firmware __aeabi_memclr, same arguments)
// @sym TapMenu_UpdateItemLayout 0xd019fa30
// @sym Light_ApplyModeIndex 0xd02694f6
// @sym CoreGroup_OnGetVisible 0xd013a5b0
// @sym CoreGroup_OnSetVisible 0xd013a5b8
// @sym CoreGroup_OnSetEnabled 0xd013a54e
// @sym CoreView_OnSetEmbedded 0xd013a590
// @sym ViewsImage_OnSetVisible 0xd0142fda
// @sym ViewsText_OnSetString 0xd0144dd0
// @sym EwNewStringAnsi 0xd0105964
// @sym EwNewStringUtf8 0xd0105974
// @sym PatternSwitch_OnSetCaption 0xd026989a
// @sym HAL_Delay 0xd0189678
// @sym CoreRectView__OnSetBounds 0xd013990a
#include <stdint.h>

typedef struct { uint16_t uuid; uint8_t type; } ble_uuid_t;                       // type 1 = Bluetooth SIG 16-bit
typedef struct { uint8_t read_perm, write_perm, flags; } ble_gatts_attr_md_t;    // perm: sm | lv << 4; flags: vloc << 1
typedef struct {
    uint8_t props, ext_props;
    const uint8_t *p_user_desc; uint16_t user_desc_max, user_desc_size;
    const void *p_pf, *p_user_desc_md;
    const ble_gatts_attr_md_t *p_cccd_md, *p_sccd_md;
} ble_gatts_char_md_t;                                                              // 0x1c bytes, as Ble_LdcSvcAddChar
typedef struct {
    const ble_uuid_t *p_uuid; const ble_gatts_attr_md_t *p_attr_md;
    uint16_t init_len, init_offs, max_len; const uint8_t *p_value;
} ble_gatts_attr_t;                                                                 // 0x14 bytes
typedef struct { uint16_t value, user_desc, cccd, sccd; } ble_gatts_char_handles_t;
typedef struct { uint16_t handle; uint8_t type; uint16_t offset; uint16_t *p_len; const uint8_t *p_data; } ble_gatts_hvx_params_t;

typedef uint32_t (*field_codec_t)(void);
extern uint32_t Ble_LdcSvcInit(void *ldc, void *init);
extern uint32_t sd_ble_gatts_service_add(uint8_t type, const ble_uuid_t *uuid, uint16_t *p_handle);
extern uint32_t sd_ble_gatts_characteristic_add(uint16_t svc, const ble_gatts_char_md_t *md,
                                                const ble_gatts_attr_t *value, ble_gatts_char_handles_t *handles);
extern uint32_t sd_ble_gatts_hvx(uint16_t conn, const ble_gatts_hvx_params_t *p);
extern void     Ble_SerTxBufAlloc(uint8_t **pp_buf, uint32_t *p_len);
extern uint32_t ser_sd_transport_cmd_write(uint8_t *buf, uint16_t len, uint32_t (*rsp)(const uint8_t *, uint16_t));
extern uint32_t uint8_t_enc(const void *field, uint8_t *buf, uint32_t len, uint32_t *idx);
extern uint32_t uint16_t_enc(const void *field, uint8_t *buf, uint32_t len, uint32_t *idx);
extern uint32_t uint16_t_dec(void);
extern uint32_t cond_field_enc(const void *field, uint8_t *buf, uint32_t len, uint32_t *idx, field_codec_t enc);
extern uint32_t cond_field_dec(const uint8_t *buf, uint32_t len, uint32_t *idx, void *pp_field, field_codec_t dec);
extern uint32_t ser_ble_cmd_rsp_result_code_dec(const uint8_t *buf, uint32_t *idx, uint32_t len, uint8_t op, uint32_t *res);
extern uint32_t ble_gatts_attr_t_enc(void);
extern volatile uint16_t g_ble_conn_handle;        // phone connection, recorded by Bryton's LDC service; 0xffff = none
extern void TapMenu_UpdateItemLayout(uint8_t *menu);
extern void Light_ApplyModeIndex(void *light, int index);
extern int  CoreGroup_OnGetVisible(void *group);
extern void CoreGroup_OnSetVisible(void *group, int on);
extern void CoreGroup_OnSetEnabled(void *group, int on);
extern void CoreView_OnSetEmbedded(void *view, int on);
extern void ViewsImage_OnSetVisible(void *image, int on);
extern void ViewsText_OnSetString(void *text, void *str);
extern void *EwNewStringAnsi(const char *s);
extern void *EwNewStringUtf8(const char *s, int bytes);
extern void PatternSwitch_OnSetCaption(void *sw, void *str);
extern void HAL_Delay(uint32_t ms);
extern void CoreRectView__OnSetBounds(void *view, int x1, int y1, int x2, int y2);   // rect by value

// The image area is code and constants only (the IAR init table puts all data at 0xd0600000 / 0x24000000 /
// 0x30000000), so the one value kept between calls lives in RTC backup register 20, unused by Bryton (OS and
// bootloader use 1-4, 12, 13, 19; P5 uses 8).
#define HID_REPORT_HANDLE (*(volatile uint32_t *)0x580040a0)   // RTC 0x58004000 + 0x50 + 4*20; 0 = no service

enum { SD_BLE_GATTS_DESCRIPTOR_ADD = 0xA3 };       // Nordic numbering: service_add 0xA0, characteristic_add 0xA2, hvx 0xA6
enum { ENC = 1 | 2 << 4, OPEN = 1 | 1 << 4 };      // security mode 1 level 2 (encrypted, the phone is bonded) / level 1
enum { PROP_READ = 0x02, PROP_WRITE_NO_RSP = 0x04, PROP_NOTIFY = 0x10 };

// Consumer-control report, report ID 1, one byte: bit 0 Play/Pause, 1 Next, 2 Previous, 3 Volume+, 4 Volume-.
static const uint8_t report_map[] = {
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01,                   // Consumer page, Consumer Control, App, ID 1
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05,                   // 5 one-bit fields
    0x09, 0xCD, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xE9, 0x09, 0xEA,       // Play/Pause, Next, Previous, Vol+, Vol-
    0x81, 0x02, 0x95, 0x03, 0x81, 0x01, 0xC0,                         // Input; 3 padding bits; End Collection
};
static const uint8_t hid_info[] = { 0x11, 0x01, 0x00, 0x02 };       // HID 1.11, no country, normally connectable
static const uint8_t report_ref[] = { 0x01, 0x01 };                  // report ID 1, input report

// --- sd_ble_gatts_descriptor_add over the serialization link (missing from the firmware) -------------------
// Like every serialized sd_* call, ser_sd_transport_cmd_write waits for the response (os_rsp_wait) and returns
// the result its decoder gave. The new descriptor's handle is in the response but nothing needs it: not decoded.
static uint32_t descriptor_add_rsp(const uint8_t *buf, uint16_t len)
{
    uint32_t idx = 0, res = 3;                                        // NRF_ERROR_INTERNAL unless decoded
    ser_ble_cmd_rsp_result_code_dec(buf, &idx, len, SD_BLE_GATTS_DESCRIPTOR_ADD, &res);
    return res;
}

static uint32_t descriptor_add(uint16_t char_handle, const ble_gatts_attr_t *attr)
{
    uint8_t *buf, op = SD_BLE_GATTS_DESCRIPTOR_ADD;
    uint32_t len = 0, idx = 0;
    Ble_SerTxBufAlloc(&buf, &len);                                    // buf[0] = packet type, len = room after it
    if (uint8_t_enc(&op, buf + 1, len, &idx) || uint16_t_enc(&char_handle, buf + 1, len, &idx) ||
        cond_field_enc(attr, buf + 1, len, &idx, ble_gatts_attr_t_enc) ||
        cond_field_enc(&op, buf + 1, len, &idx, 0))                   // p_handle present (the SoftDevice needs one)
        return 4;                                                     // NRF_ERROR_NO_MEM: encoding did not fit
    return ser_sd_transport_cmd_write(buf, (uint16_t)(idx + 1), descriptor_add_rsp);
}

// --- the HID service ---------------------------------------------------------------------------------------
static uint32_t add_char(uint16_t svc, uint16_t uuid16, uint8_t props, const uint8_t *value, uint16_t len,
                         ble_gatts_char_handles_t *h)
{
    // Notification switch (CCCD): written by the phone over an encrypted link; reading must be open (the SoftDevice
    // rejects anything else with NRF_ERROR_INVALID_PARAM: flash #9 showed S5007).
    static const ble_gatts_attr_md_t cccd_md = { OPEN, ENC, 1 << 1 };
    const ble_gatts_attr_md_t value_md = { ENC, (props & PROP_WRITE_NO_RSP) ? ENC : OPEN, 1 << 1 };   // value in the stack
    const ble_uuid_t uuid = { uuid16, 1 };
    const ble_gatts_char_md_t md = { props, 0, 0, 0, 0, 0, 0, (props & PROP_NOTIFY) ? &cccd_md : 0, 0 };
    const ble_gatts_attr_t attr = { &uuid, &value_md, len, 0, len, value };
    return sd_ble_gatts_characteristic_add(svc, &md, &attr, h);
}

static void hid_service_add(void)
{
    static const uint8_t zero;
    const ble_uuid_t svc_uuid = { 0x1812, 1 }, ref_uuid = { 0x2908, 1 };
    const ble_gatts_attr_md_t ref_md = { ENC, OPEN, 1 << 1 };
    const ble_gatts_attr_t ref = { &ref_uuid, &ref_md, sizeof report_ref, 0, sizeof report_ref, report_ref };
    ble_gatts_char_handles_t ignored, report = { 0, 0, 0, 0 };
    uint16_t svc = 0;
    HID_REPORT_HANDLE = 0;
    if (sd_ble_gatts_service_add(1, &svc_uuid, &svc) ||
        add_char(svc, 0x2A4A, PROP_READ, hid_info, sizeof hid_info, &ignored) ||                  // HID Information
        add_char(svc, 0x2A4B, PROP_READ, report_map, sizeof report_map, &ignored) ||              // Report Map
        add_char(svc, 0x2A4C, PROP_WRITE_NO_RSP, &zero, 1, &ignored) ||                          // HID Control Point
        add_char(svc, 0x2A4D, PROP_READ | PROP_NOTIFY, &zero, 1, &report) ||                     // Report (input)
        descriptor_add(report.value, &ref))                                                        // Report Reference
        return;                                                                                    // give up quietly
    HID_REPORT_HANDLE = report.value;
}

// Hooked in place of Ble_ServicesInit's call to Ble_LdcSvcInit: same arguments, same result.
uint32_t ldc_then_hid(void *ldc, void *init)
{
    uint32_t r = Ble_LdcSvcInit(ldc, init);
    hid_service_add();
    return r;
}

// One notification; like Bryton's Ble_LdcSvcSendNotify, wait and retry while the TX queue is full (0x13).
static uint32_t notify(ble_gatts_hvx_params_t *p)
{
    uint32_t r;
    for (int tries = 0; (r = sd_ble_gatts_hvx(g_ble_conn_handle, p)) == 0x13 && tries < 50; tries++)
        HAL_Delay(2);
    return r;
}

// Send one key: press then release. bit: 0 Play/Pause, 1 Next, 2 Previous, 3 Volume+, 4 Volume-.
// Returns 0 if both notifications were accepted (no phone connected or service missing: nonzero, nothing sent).
uint32_t media_remote_press(unsigned bit)
{
    uint8_t down = (uint8_t)(1u << bit), up = 0;
    uint16_t n = 1;
    ble_gatts_hvx_params_t p = { (uint16_t)HID_REPORT_HANDLE, 1, 0, &n, &down };   // type 1 = notification
    if (p.handle == 0 || g_ble_conn_handle == 0xffff)
        return 8;                                                      // NRF_ERROR_INVALID_STATE
    uint32_t r = notify(&p);
    p.p_data = &up;
    n = 1;
    return r ? r : notify(&p);
}

// --- the buttons: the overview's Light Control widget as a media box ---------------------------------------
// TapMenu (overview) offsets: +0x60 global settings, whose +0x18bdd byte is "Light Control" on/off in Quick
// Status; +0x1e38 the first light box (SettingLightUnitSensorLightControlBox). Box offsets: +0x60 its 6 pattern
// buttons (3 x 2 grid of 73 x 46 px), +0x194 light icon, +0x208 / +0x29c header texts, +0x330 arrow,
// +0x149c light data, +0x1620 light index (-1 = none, Bryton's own "no light" value).
#define MENU_SETTINGS(m)  (*(uint8_t **)((m) + 0x60))
#define BOX_INDEX(b)      (*(volatile int32_t *)((b) + 0x1620))
#define BOX_BUTTON(b, i)  (((void **)((b) + 0x60))[i])

// Button order, packed (cave space). UTF-8; the unit's font (BrytonRiderFont, DeviceLangs.bin) has ◀ ▶ ‖ ♪ −
// but no ⏮ ⏯ ⏭ or speaker, so the usual symbols are composed from those.
static const char captions[] = "|◀◀\0▶‖\0▶▶|\0♪−\0♪+";
static const uint8_t keys[5] = { 2, 0, 1, 4, 3 };                    // media_remote_press bits, button order

// Runs where the refresh called TapMenu_UpdateItemLayout: Bryton has just shown or hidden the box (shown only
// with a light connected). Widget on and box hidden: show it as the media box instead.
void media_refresh(uint8_t *menu)
{
    uint8_t *box = menu + 0x1e38;
    if (MENU_SETTINGS(menu)[0x18bdd] && !CoreGroup_OnGetVisible(box)) {
        BOX_INDEX(box) = -1;                                          // media mode; a light sets its index back
        CoreView_OnSetEmbedded(box, 1);                               // same three calls as Bryton's refresh
        CoreGroup_OnSetVisible(box, 1);
        CoreGroup_OnSetEnabled(box, 1);
        ViewsImage_OnSetVisible(box + 0x194, 0);
        ViewsImage_OnSetVisible(box + 0x330, 0);
        ViewsText_OnSetString(box + 0x208, EwNewStringAnsi(""));
        ViewsText_OnSetString(box + 0x29c, EwNewStringAnsi("Media"));
        CoreRectView__OnSetBounds(BOX_BUTTON(box, 3), 45, 103, 118, 149);   // bottom row centred under the top
        CoreRectView__OnSetBounds(BOX_BUTTON(box, 4), 123, 103, 196, 149);  // row (Bryton: x 6 and 84)
        // ponytail: captions rebuilt on every refresh (7 small EW strings, garbage collected); a flag if it shows up
        const char *c = captions;
        for (int i = 0; i < 6; i++) {
            void *sw = BOX_BUTTON(box, i);                    // never selected in media mode: no OnSetSelected
            CoreGroup_OnSetEnabled(sw, i < 5);
            CoreGroup_OnSetVisible(sw, i < 5);
            if (i < 5) {
                const char *e = c;
                while (*e)
                    e++;
                PatternSwitch_OnSetCaption(sw, EwNewStringUtf8(c, (int)(e - c)));
                c = e + 1;
            }
        }
    } else {
        ViewsImage_OnSetVisible(box + 0x194, 1);                      // light (or hidden): Bryton's header back;
        ViewsImage_OnSetVisible(box + 0x330, 1);                      // the light refresh has redone the rest
        CoreRectView__OnSetBounds(BOX_BUTTON(box, 3), 6, 103, 79, 149);
        CoreRectView__OnSetBounds(BOX_BUTTON(box, 4), 84, 103, 157, 149);
    }
    TapMenu_UpdateItemLayout(menu);
}

// A pattern button was tapped (Bryton found its index). Media mode: send the key instead of a light mode.
void media_or_light(uint8_t *light, int index)
{
    if (BOX_INDEX(light - 0x149c) >= 0)
        Light_ApplyModeIndex(light, index);
    else if (index < 5)
        media_remote_press(keys[index]);
}

// The Quick Status pages' label for the widget: "Multimedia Control" instead of "Light Control" (English only;
// Bryton's string is shared with the light settings page, so it is not edited).
void *quick_status_label(void)
{
    return EwNewStringAnsi("Multimedia Control");
}

// The box header was tapped: Bryton opens the light's settings page for light BOX_INDEX, which is -1 in media
// mode. Replaces the stock "ldrb r0,[r1,#1]; cbnz r0,exit" (r1 = box + 0x142 touch flags, r4 = box); to exit it
// points r1 at a zero byte so the stock "ldrb r1,[r1]; cbz r1,exit" that follows does it.
__attribute__((naked)) void header_guard(void)
{
    __asm volatile(
        "ldrb r0, [r1, #1]\n"
        "cbnz r0, 1f\n"
        "add.w r0, r4, #0x1600\n"
        "ldr r0, [r0, #0x20]\n"                                       // BOX_INDEX
        "cmp r0, #0\n"
        "bge 2f\n"
        "1: adr r1, 3f\n"
        "2: bx lr\n"
        ".p2align 2\n"
        "3: .word 0\n");
}
