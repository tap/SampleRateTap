// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// TinyUSB configuration (the copy the Pico SDK 2.1.1 carries, 0.18.0): a
// Full-Speed device with one UAC2 function and nothing else. The SDK
// defines CFG_TUSB_MCU and CFG_TUSB_OS for the RP2350.
#ifndef PICO2_UAC2_TUSB_CONFIG_H
#define PICO2_UAC2_TUSB_CONFIG_H

#include "usb_descriptors.h"

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_AUDIO 1
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN UAC2_FUNC_DESC_LEN
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 2
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64

// Capture. Flow control off: the driver then sends what the IN FIFO holds,
// up to a packet, and the firmware puts exactly the frames its own clock
// produced since the last packet there (main.cpp). With it on, TinyUSB
// would size the packets from the host's frame clock, which is the
// synchronous device this one exists not to be.
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 0
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX UAC2_EP_SIZE
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (4 * UAC2_EP_SIZE)

// Playback, with the explicit feedback endpoint. Eight packets of FIFO
// (392 frames, 8.2 ms): the FIFO-count feedback holds it half full.
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX UAC2_EP_SIZE
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (8 * UAC2_EP_SIZE)
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1

#endif
