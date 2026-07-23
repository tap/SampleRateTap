/// @file ratio_capi.cpp
/// @brief C ABI implementation: a tagged pair of the two float converters.
// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the RatioTap contributors.

#include "ratio_capi.h"

#include <new>

#include "tap/ratio/ratio.h"

namespace {

    // Direction is a compile-time template parameter in C++; the C ABI makes
    // it a runtime tag over the two instantiations.
    template <tap::ratio::direction D>
    using conv = tap::ratio::basic_converter<float, D>;

} // namespace

struct ratio_converter {
    int                                        dir; // 0 up, 1 down
    conv<tap::ratio::direction::up_to_48k>*    up   = nullptr;
    conv<tap::ratio::direction::down_to_44k1>* down = nullptr;

    ~ratio_converter() {
        delete up;
        delete down;
    }
};

extern "C" {

ratio_converter* ratio_create(int direction, int profile, unsigned channels) {
    if ((direction != 0 && direction != 1) || (profile != 0 && profile != 1) || channels == 0) {
        return nullptr;
    }
    const tap::ratio::profile p = profile == 0 ? tap::ratio::profile::economy() : tap::ratio::profile::transparent();
    try {
        auto* c = new ratio_converter;
        c->dir  = direction;
        if (direction == 0) {
            c->up = new conv<tap::ratio::direction::up_to_48k>(channels, p);
        }
        else {
            c->down = new conv<tap::ratio::direction::down_to_44k1>(channels, p);
        }
        return c;
    }
    catch (...) {
        return nullptr;
    }
}

void ratio_destroy(ratio_converter* c) {
    delete c;
}

uint64_t ratio_outputs_for(const ratio_converter* c, uint64_t in_frames) {
    return c->dir == 0 ? c->up->outputs_for(in_frames) : c->down->outputs_for(in_frames);
}

uint64_t ratio_frames_needed(const ratio_converter* c, uint64_t out_frames) {
    return c->dir == 0 ? c->up->frames_needed(out_frames) : c->down->frames_needed(out_frames);
}

size_t ratio_process(ratio_converter* c, const float* in, size_t in_frames, float* out) {
    return c->dir == 0 ? c->up->process(in, in_frames, out) : c->down->process(in, in_frames, out);
}

size_t ratio_flush(ratio_converter* c, float* out) {
    return c->dir == 0 ? c->up->flush(out) : c->down->flush(out);
}

uint64_t ratio_flush_output_frames(const ratio_converter* c) {
    return c->dir == 0 ? c->up->flush_output_frames() : c->down->flush_output_frames();
}

void ratio_reset(ratio_converter* c) {
    c->dir == 0 ? c->up->reset() : c->down->reset();
}

double ratio_latency_input_frames(const ratio_converter* c) {
    return c->dir == 0 ? c->up->latency_input_frames() : c->down->latency_input_frames();
}

size_t ratio_taps(const ratio_converter* c) {
    return c->dir == 0 ? c->up->taps() : c->down->taps();
}

unsigned ratio_version(void) {
    return (TAP_RATIO_VERSION_MAJOR << 16) | (TAP_RATIO_VERSION_MINOR << 8) | TAP_RATIO_VERSION_PATCH;
}

} // extern "C"
