// P7 (DRAFT, Bluetooth half only): the S510 becomes a Bluetooth media remote for the paired phone.
//
// Adds a standard HID-over-GATT service (Bluetooth SIG "HID", 0x1812) with one consumer-control report:
// Play/Pause, Next, Previous, Volume up, Volume down. The phone's OS handles the keys (whatever app is playing),
// so nothing is needed on the phone. The overview buttons that call media_remote_press() come in a later step.
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

static ble_gatts_char_handles_t report;                              // report.value: where key presses are notified
static uint16_t desc_handle;
static uint16_t *desc_rsp_out;

// --- sd_ble_gatts_descriptor_add over the serialization link (missing from the firmware) -------------------
static uint32_t descriptor_add_rsp(const uint8_t *buf, uint16_t len)
{
    uint32_t idx = 0, res = 3;                                        // NRF_ERROR_INTERNAL unless decoded
    if (ser_ble_cmd_rsp_result_code_dec(buf, &idx, len, SD_BLE_GATTS_DESCRIPTOR_ADD, &res) == 0 && res == 0)
        cond_field_dec(buf, len, &idx, &desc_rsp_out, uint16_t_dec);
    return res;
}

static uint32_t descriptor_add(uint16_t char_handle, const ble_gatts_attr_t *attr, uint16_t *p_handle)
{
    uint8_t *buf, op = SD_BLE_GATTS_DESCRIPTOR_ADD;
    uint32_t len = 0, idx = 0;
    Ble_SerTxBufAlloc(&buf, &len);                                    // buf[0] = packet type, len = room after it
    desc_rsp_out = p_handle;
    if (uint8_t_enc(&op, buf + 1, len, &idx) || uint16_t_enc(&char_handle, buf + 1, len, &idx) ||
        cond_field_enc(attr, buf + 1, len, &idx, ble_gatts_attr_t_enc) || cond_field_enc(p_handle, buf + 1, len, &idx, 0))
        return 4;                                                     // NRF_ERROR_NO_MEM: encoding did not fit
    return ser_sd_transport_cmd_write(buf, (uint16_t)(idx + 1), descriptor_add_rsp);
}

// --- the HID service ---------------------------------------------------------------------------------------
static uint32_t add_char(uint16_t svc, uint16_t uuid16, uint8_t props, const uint8_t *value, uint16_t len,
                         ble_gatts_char_handles_t *h)
{
    static const ble_gatts_attr_md_t cccd_md = { ENC, ENC, 1 << 1 };  // notifications: enabled by the phone, encrypted
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
    ble_gatts_char_handles_t ignored;
    uint16_t svc;
    report.value = 0;
    if (sd_ble_gatts_service_add(1, &svc_uuid, &svc) ||
        add_char(svc, 0x2A4A, PROP_READ, hid_info, sizeof hid_info, &ignored) ||                  // HID Information
        add_char(svc, 0x2A4B, PROP_READ, report_map, sizeof report_map, &ignored) ||              // Report Map
        add_char(svc, 0x2A4C, PROP_WRITE_NO_RSP, &zero, 1, &ignored) ||                          // HID Control Point
        add_char(svc, 0x2A4D, PROP_READ | PROP_NOTIFY, &zero, 1, &report) ||                     // Report (input)
        descriptor_add(report.value, &ref, &desc_handle))                                          // Report Reference
        report.value = 0;                                                                          // give up quietly
}

// Hooked in place of Ble_ServicesInit's call to Ble_LdcSvcInit: same arguments, same result.
uint32_t ldc_then_hid(void *ldc, void *init)
{
    uint32_t r = Ble_LdcSvcInit(ldc, init);
    hid_service_add();
    return r;
}

// Send one key: press then release. bit: 0 Play/Pause, 1 Next, 2 Previous, 3 Volume+, 4 Volume-.
// Returns 0 if both notifications were accepted (no phone connected or service missing: nonzero, nothing sent).
uint32_t media_remote_press(unsigned bit)
{
    uint8_t down = (uint8_t)(1u << bit), up = 0;
    uint16_t n = 1;
    ble_gatts_hvx_params_t p = { report.value, 1, 0, &n, &down };     // type 1 = notification
    if (report.value == 0 || g_ble_conn_handle == 0xffff)
        return 8;                                                      // NRF_ERROR_INVALID_STATE
    uint32_t r = sd_ble_gatts_hvx(g_ble_conn_handle, &p);
    p.p_data = &up;
    n = 1;
    return r | sd_ble_gatts_hvx(g_ble_conn_handle, &p);
}
