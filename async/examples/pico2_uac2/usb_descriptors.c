// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// Device, configuration and string descriptors (TinyUSB's descriptor
// callbacks). The audio function follows UAC2 section 4: the class-specific
// AudioControl interface holds the clock and the four terminals; each
// AudioStreaming interface has a zero-bandwidth alternate 0 and a streaming
// alternate 1.
#include "usb_descriptors.h"

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL, STRID_AUDIO };

// 0xCafe is TinyUSB's test vendor ID, used by its examples: fine on a bench,
// never for a device that ships.
static tusb_desc_device_t const desc_device = {
    .bLength         = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB          = 0x0200,
    // An IAD groups the audio interfaces: the device class says so (USB IAD ECN).
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCafe,
    .idProduct          = 0x4A11, // A11: the roadmap item this device serves
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + UAC2_FUNC_DESC_LEN)
#define UAC2_AC_TOTAL_LEN                                                                                              \
    (TUD_AUDIO_DESC_CLK_SRC_LEN + 2 * TUD_AUDIO_DESC_INPUT_TERM_LEN + 2 * TUD_AUDIO_DESC_OUTPUT_TERM_LEN)
#define UAC2_ISO_ASYNC_DATA                                                                                            \
    ((uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA))

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    TUD_AUDIO_DESC_IAD(ITF_NUM_AUDIO_CONTROL, ITF_NUM_TOTAL, 0x00),
    TUD_AUDIO_DESC_STD_AC(ITF_NUM_AUDIO_CONTROL, 0x00, STRID_AUDIO),
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_IO_BOX, UAC2_AC_TOTAL_LEN, 0x00),
    // The device's own clock: internal, fixed, its frequency and validity read-only.
    TUD_AUDIO_DESC_CLK_SRC(UAC2_ENTITY_CLOCK, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                           (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS)
                               | (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS),
                           0x00, 0x00),
    // Playback: USB streaming -> speaker.
    TUD_AUDIO_DESC_INPUT_TERM(UAC2_ENTITY_PLAY_IT, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, UAC2_ENTITY_CLOCK,
                              UAC2_CHANNELS, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00, 0x0000, 0x00),
    TUD_AUDIO_DESC_OUTPUT_TERM(UAC2_ENTITY_PLAY_OT, AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, 0x00, UAC2_ENTITY_PLAY_IT,
                               UAC2_ENTITY_CLOCK, 0x0000, 0x00),
    // Capture: microphone -> USB streaming.
    TUD_AUDIO_DESC_INPUT_TERM(UAC2_ENTITY_REC_IT, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0x00, UAC2_ENTITY_CLOCK,
                              UAC2_CHANNELS, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00, 0x0000, 0x00),
    TUD_AUDIO_DESC_OUTPUT_TERM(UAC2_ENTITY_REC_OT, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, UAC2_ENTITY_REC_IT,
                               UAC2_ENTITY_CLOCK, 0x0000, 0x00),

    // Playback streaming interface.
    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING_OUT, 0x00, 0x00, 0x00),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING_OUT, 0x01, 0x02, 0x00),
    TUD_AUDIO_DESC_CS_AS_INT(UAC2_ENTITY_PLAY_IT, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM,
                             UAC2_CHANNELS, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(UAC2_BYTES_PER_SAMPLE, UAC2_BITS_PER_SAMPLE),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(UAC2_EP_OUT, UAC2_ISO_ASYNC_DATA, UAC2_EP_SIZE, 0x01),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000),
    TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(UAC2_EP_FB, UAC2_EP_FB_SIZE, 0x01),

    // Capture streaming interface.
    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING_IN, 0x00, 0x00, 0x00),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING_IN, 0x01, 0x01, 0x00),
    TUD_AUDIO_DESC_CS_AS_INT(UAC2_ENTITY_REC_OT, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM,
                             UAC2_CHANNELS, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(UAC2_BYTES_PER_SAMPLE, UAC2_BITS_PER_SAMPLE),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(UAC2_EP_IN, UAC2_ISO_ASYNC_DATA, UAC2_EP_SIZE, 0x01),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000),
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "the configuration's length must match its contents");

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char const* const string_desc_arr[] = {
    NULL, // the language ID, written below
    "SampleRateTap",
    "SampleRateTap Pico 2 UAC2",
    NULL, // the board's unique ID, written below
    "Pico 2 UAC2 (asynchronous)",
};

static uint16_t desc_str[32 + 1];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count = 0;
    if (index == STRID_LANGID) {
        desc_str[1] = 0x0409; // English (United States)
        chr_count   = 1;
    }
    else {
        char        serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        char const* str = NULL;
        if (index == STRID_SERIAL) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            str = serial;
        }
        else if (index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            str = string_desc_arr[index];
        }
        if (str == NULL) {
            return NULL;
        }
        chr_count              = strlen(str);
        size_t const max_count = sizeof(desc_str) / sizeof(desc_str[0]) - 1;
        if (chr_count > max_count) {
            chr_count = max_count;
        }
        for (size_t i = 0; i < chr_count; ++i) {
            desc_str[1 + i] = (uint16_t)str[i];
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
