/// @file tap_sr_bridge_capi.cpp
/// @brief C ABI implementation: a tagged pair of the two float converters.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.

#include "tap_sr_bridge_capi.h"

#include <memory>

#include "tap/sr/bridge/ratio.h"

namespace {

    // Direction is a compile-time template parameter in C++; the C ABI makes
    // it a runtime tag over the two instantiations.
    template <tap::sr::bridge::direction D>
    using conv = tap::sr::bridge::basic_converter<float, D>;

} // namespace

struct tap_sr_bridge_converter {
    int                                             dir; // 0 up, 1 down
    conv<tap::sr::bridge::direction::up_to_48k>*    up   = nullptr;
    conv<tap::sr::bridge::direction::down_to_44k1>* down = nullptr;

    ~tap_sr_bridge_converter() {
        delete up;
        delete down;
    }
};

extern "C" {

tap_sr_bridge_converter* tap_sr_bridge_create(int direction, int profile, unsigned channels) {
    if ((direction != 0 && direction != 1) || profile < 0 || profile > 3 || channels == 0) {
        return nullptr;
    }
    const tap::sr::bridge::profile p = profile == 0   ? tap::sr::bridge::profile::economy()
                                       : profile == 1 ? tap::sr::bridge::profile::transparent()
                                       : profile == 2 ? tap::sr::bridge::profile::balanced()
                                                      : tap::sr::bridge::profile::super_economy();
    try {
        // unique_ptr owns the wrapper until the converter constructor has
        // succeeded, so a throw below cannot leak it.
        auto c = std::make_unique<tap_sr_bridge_converter>();
        c->dir = direction;
        if (direction == 0) {
            c->up = new conv<tap::sr::bridge::direction::up_to_48k>(channels, p);
        }
        else {
            c->down = new conv<tap::sr::bridge::direction::down_to_44k1>(channels, p);
        }
        return c.release();
    }
    catch (...) {
        return nullptr;
    }
}

void tap_sr_bridge_destroy(tap_sr_bridge_converter* c) {
    delete c;
}

uint64_t tap_sr_bridge_outputs_for(const tap_sr_bridge_converter* c, uint64_t in_frames) {
    return c->dir == 0 ? c->up->outputs_for(in_frames) : c->down->outputs_for(in_frames);
}

uint64_t tap_sr_bridge_frames_needed(const tap_sr_bridge_converter* c, uint64_t out_frames) {
    return c->dir == 0 ? c->up->frames_needed(out_frames) : c->down->frames_needed(out_frames);
}

size_t tap_sr_bridge_process(tap_sr_bridge_converter* c, const float* in, size_t in_frames, float* out) {
    return c->dir == 0 ? c->up->process(in, in_frames, out) : c->down->process(in, in_frames, out);
}

size_t tap_sr_bridge_flush(tap_sr_bridge_converter* c, float* out) {
    return c->dir == 0 ? c->up->flush(out) : c->down->flush(out);
}

uint64_t tap_sr_bridge_flush_output_frames(const tap_sr_bridge_converter* c) {
    return c->dir == 0 ? c->up->flush_output_frames() : c->down->flush_output_frames();
}

void tap_sr_bridge_reset(tap_sr_bridge_converter* c) {
    c->dir == 0 ? c->up->reset() : c->down->reset();
}

double tap_sr_bridge_latency_input_frames(const tap_sr_bridge_converter* c) {
    return c->dir == 0 ? c->up->latency_input_frames() : c->down->latency_input_frames();
}

size_t tap_sr_bridge_taps(const tap_sr_bridge_converter* c) {
    return c->dir == 0 ? c->up->taps() : c->down->taps();
}

unsigned tap_sr_bridge_version(void) {
    return (TAP_SR_VERSION_MAJOR << 16) | (TAP_SR_VERSION_MINOR << 8) | TAP_SR_VERSION_PATCH;
}

} // extern "C"
