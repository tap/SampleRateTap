/// \file srt.hpp
/// \brief Umbrella header for the SampleRateTap near-unity asynchronous sample
///        rate converter library.
///
/// SampleRateTap is a header-only C++20 library that resamples audio between
/// two clock domains running at nominally the same rate (e.g. 48 kHz on both
/// sides) but sourced from independent oscillators, each within a few hundred
/// ppm of nominal. A producer thread push()es input samples at the input
/// clock; a consumer thread pull()s output samples at the output clock. A
/// lock-free FIFO sits between the domains and its occupancy drives a type-2
/// PI servo that estimates the instantaneous rate deviation.
#ifndef SRT_SRT_HPP
#define SRT_SRT_HPP

#define SRT_VERSION_MAJOR 0
#define SRT_VERSION_MINOR 1
#define SRT_VERSION_PATCH 0

#include "srt/asrc.hpp"

#endif // SRT_SRT_HPP
