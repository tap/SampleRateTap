// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The device's USB Audio Class 2.0 topology (USB-IF, "Universal Serial Bus
// Device Class Definition for Audio Devices", Release 2.0): one internal
// fixed clock, a playback path (USB streaming -> speaker) and a capture path
// (microphone -> USB streaming), both at 48 kHz, stereo, 16-bit, each on an
// asynchronous isochronous endpoint; the playback path's explicit feedback
// endpoint tells the host the device's rate (USB 2.0, 5.12.4.2). Included by
// tusb_config.h, so it holds macros only.
#ifndef PICO2_UAC2_USB_DESCRIPTORS_H
#define PICO2_UAC2_USB_DESCRIPTORS_H

enum { ITF_NUM_AUDIO_CONTROL = 0, ITF_NUM_AUDIO_STREAMING_OUT, ITF_NUM_AUDIO_STREAMING_IN, ITF_NUM_TOTAL };

#define UAC2_ENTITY_CLOCK 0x10
#define UAC2_ENTITY_PLAY_IT 0x01 // USB streaming in, from the host
#define UAC2_ENTITY_PLAY_OT 0x02 // the speaker it plays to
#define UAC2_ENTITY_REC_IT 0x03  // the microphone it records from
#define UAC2_ENTITY_REC_OT 0x04  // USB streaming out, to the host

#define UAC2_EP_OUT 0x01
#define UAC2_EP_FB 0x81
#define UAC2_EP_IN 0x82

#define UAC2_SAMPLE_RATE 48000
#define UAC2_CHANNELS 2
#define UAC2_BYTES_PER_SAMPLE 2
#define UAC2_BITS_PER_SAMPLE 16

// One frame's packet at Full Speed: 48 frames a millisecond, plus one for
// the frames an asynchronous clock runs ahead (196 bytes, 49 frames).
#define UAC2_EP_SIZE TUD_AUDIO_EP_SIZE(UAC2_SAMPLE_RATE, UAC2_BYTES_PER_SAMPLE, UAC2_CHANNELS)

// 16.16 feedback in 4 bytes: what Linux and Windows read at Full Speed
// (the 10.14 format macOS wants is not served; README).
#define UAC2_EP_FB_SIZE 4

#define UAC2_FUNC_DESC_LEN                                                                                             \
    (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN + TUD_AUDIO_DESC_CS_AC_LEN + TUD_AUDIO_DESC_CLK_SRC_LEN        \
     + 2 * TUD_AUDIO_DESC_INPUT_TERM_LEN                                                                               \
     + 2 * TUD_AUDIO_DESC_OUTPUT_TERM_LEN /* playback AS interface: alt 0, alt 1 with its data and feedback endpoints  \
                                           */                                                                          \
     + 2 * TUD_AUDIO_DESC_STD_AS_INT_LEN + TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN             \
     + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN                                              \
     + TUD_AUDIO_DESC_STD_AS_ISO_FB_EP_LEN /* capture AS interface: alt 0, alt 1 with its data endpoint */             \
     + 2 * TUD_AUDIO_DESC_STD_AS_INT_LEN + TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN             \
     + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

#endif
