// USB GameCube-adapter (WUP-028) transport for PhobGCC RP2040.
//
// Emulates the official Nintendo GameCube controller adapter so the controller
// can be plugged directly into a Switch over USB-C and seen as a GC controller
// on port 1. This is a drop-in replacement for the joybus enterMode(): it
// reuses the same GCReport produced by buttonsToGCReport().
//
// Identity, report layout and the 8.333 ms "consistency-mode" cadence are
// ported from the NaxGCC firmware (src/hid/gcc.rs, src/usb_comms.rs).

#include "comms/usbAdapter.hpp"

#include "pico/unique_id.h"
#include "hardware/pwm.h"

#include "tusb.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Shared state between enterUsbMode() and the C-linkage TinyUSB callbacks.
// ---------------------------------------------------------------------------
static std::function<GCReport()> s_reportFunc;
static int s_rumblePin = -1;
static int s_brakePin = -1;
static int *s_rumblePower = nullptr;

static bool s_gcFirst = false;       // first IN report after mount carries no data

#define CONSISTENCY_INTERVAL_US 8333u
#define GC_INPUT_REPORT_LEN 37

// ===========================================================================
// USB descriptors (C linkage for TinyUSB)
// ===========================================================================
extern "C" {

#define USB_VID 0x057e
#define USB_PID 0x0337
#define USB_BCD 0x0200

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0010,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// HID report descriptor copied verbatim from NaxGCC (src/hid/gcc.rs).
static uint8_t const desc_hid_report[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x11,        //     Report ID (17) - rumble out
    0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x05, 0x91, 0x00,
    0xC0,              //   End Collection
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x21,        //     Report ID (33) - input
    0x05, 0x00, 0x15, 0x00, 0x25, 0xFF, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x33, 0x09, 0x34, 0x09, 0x35,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,
    0xC0,              //   End Collection
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x13,        //     Report ID (19) - init
    0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x91, 0x00,
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return desc_hid_report;
}

enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define EPNUM_HID_OUT 0x02
#define EPNUM_HID_IN 0x81

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                             EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

static char const *string_desc_arr[] = {
    (const char[]) {0x09, 0x04},
    "PhobGCC",
    "PhobGCC USB-C (Consistency Mode)",
    NULL,
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    size_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == STRID_SERIAL) {
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(serial, sizeof(serial));
        chr_count = strlen(serial);
        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = serial[i];
        }
    } else {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }
        char const *str = string_desc_arr[index];
        chr_count = strlen(str);
        size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
        if (chr_count > max_count) chr_count = max_count;
        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// ===========================================================================
// TinyUSB device / HID callbacks
// ===========================================================================
void tud_mount_cb(void) {
    s_gcFirst = false;
}

void tud_umount_cb(void) {
    s_gcFirst = false;
}

void tud_suspend_cb(bool remote_wakeup_en) { (void) remote_wakeup_en; }
void tud_resume_cb(void) {}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) reqlen;
    return 0;
}

// Host -> device: rumble command (report id 0x11, bit data[1]&0x01) and the
// init command (0x13, no action). Mirrors NaxGCC's GccRequestHandler.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void) instance; (void) report_type;
    if (report_id == 0x11 && bufsize >= 2 && s_rumblePower != nullptr) {
        const bool rumble = (buffer[1] & 0x01) != 0;
        if (rumble) {
            pwm_set_gpio_level(s_brakePin, 0);
            pwm_set_gpio_level(s_rumblePin, *s_rumblePower);
        } else {
            pwm_set_gpio_level(s_rumblePin, 0);
            pwm_set_gpio_level(s_brakePin, 0);
        }
    }
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void) instance; (void) report; (void) len;
}

} // extern "C"

// ===========================================================================
// Report building + main loop
// ===========================================================================
static void buildInputReport(uint8_t *buf, const GCReport &r) {
    memset(buf, 0, GC_INPUT_REPORT_LEN);
    buf[0] = 0x21;
    buf[1] |= 0x14;  // port 1: wired controller present

    if (!s_gcFirst) {
        buf[1] |= 0x04;
        buf[10] |= 0x04;
        buf[19] |= 0x04;
        buf[28] |= 0x04;
        s_gcFirst = true;
        return;
    }

    // Adapter button byte layout (lsb0) differs from GCReport's, so remap.
    uint8_t buttons1 = (uint8_t) ((r.a ? 0x01 : 0) | (r.b ? 0x02 : 0) |
                                  (r.x ? 0x04 : 0) | (r.y ? 0x08 : 0) |
                                  (r.dLeft ? 0x10 : 0) | (r.dRight ? 0x20 : 0) |
                                  (r.dDown ? 0x40 : 0) | (r.dUp ? 0x80 : 0));
    uint8_t buttons2 = (uint8_t) ((r.start ? 0x01 : 0) | (r.z ? 0x02 : 0) |
                                  (r.r ? 0x04 : 0) | (r.l ? 0x08 : 0));

    buf[2] = buttons1;
    buf[3] = buttons2;
    buf[4] = r.xStick;
    buf[5] = r.yStick;
    buf[6] = r.cxStick;
    buf[7] = r.cyStick;
    buf[8] = r.analogL;
    buf[9] = r.analogR;
}

// Best-effort emission of the current controller state. Skipped silently if the
// endpoint is busy / the host isn't polling; the master clock has already moved
// on, so the next successful emission simply carries the latest state.
static void emitReportIfReady(void) {
    if (!tud_hid_ready()) {
        return;
    }
    GCReport report = s_reportFunc();
    uint8_t buf[GC_INPUT_REPORT_LEN];
    buildInputReport(buf, report);
    tud_hid_report(0, buf, GC_INPUT_REPORT_LEN);
}

void enterUsbMode(const int rumblePin, const int brakePin, int &rumblePower,
                  std::function<GCReport()> func) {
    s_reportFunc = func;
    s_rumblePin = rumblePin;
    s_brakePin = brakePin;
    s_rumblePower = &rumblePower;

    tud_init(BOARD_TUD_RHPORT);

    // Free-running master clock. The emission deadline advances purely on wall
    // time (never gated on host poll/ack), giving a true "consistency mode"
    // 8.333 ms cadence regardless of the host. (NaxGCC model.)
    uint64_t nextEmitUs = time_us_64();

    while (true) {
        tud_task();

        const uint64_t now = time_us_64();
        if (now >= nextEmitUs) {
            // Keep the 8333us phase even if a slow iteration made us late.
            do {
                nextEmitUs += CONSISTENCY_INTERVAL_US;
            } while (nextEmitUs <= now);

            emitReportIfReady();
        }
    }
}
