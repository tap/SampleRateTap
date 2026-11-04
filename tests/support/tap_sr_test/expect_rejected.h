// SPDX-License-Identifier: MIT
// Copyright 2026 Timothy Place and the SampleRateTap contributors
//
// The family's rejection assertion, for a battery built with or without
// exceptions (the roadmap's A12; Q5: a current consumer builds every engine
// with -fno-exceptions -fno-rtti on the M33, M55 and Hexagon).
//
// An engine rejects a configuration through tap::dsp::raise<E>
// (tap/dsp/detail/expects.h): a throw of E where the build has exceptions,
// std::terminate() where it does not, and the noexcept validate() beside each
// constructor names the reason either way (A3). So:
//
//   TAP_SR_EXPECT_REJECTED(statement, E)
//     with exceptions:    EXPECT_THROW(statement, E)
//     without, hosted:    EXPECT_DEATH(statement, ""): the process terminates
//     without, bare metal: nothing; gtest has no death tests there, and a
//                          terminate would end the one-shot run. The
//                          validate() checks beside each use are the
//                          rejection's evidence on those legs.
//
//   TAP_SR_EXPECT_REJECTED_WITH(statement, E, text)
//     with exceptions:    the statement throws E, and its what() is `text`
//                         (the reason validate() gives: the A3 agreement)
//     without:            TAP_SR_EXPECT_REJECTED(statement, E); the text is
//                         validate()'s, which each use pins beside it
//
//   TAP_SR_EXPECT_ACCEPTED(statement)
//     with exceptions:    EXPECT_NO_THROW(statement)
//     without:            the statement, run; a rejection terminates the
//                         battery, which fails it as surely
//
// TAP_SR_TEST_HAS_EXCEPTIONS says which build a file is in, for the tests
// that read an exception's text.
#ifndef TAP_SR_TEST_EXPECT_REJECTED_H
#define TAP_SR_TEST_EXPECT_REJECTED_H

#include <gtest/gtest.h>

#include "tap/dsp/detail/expects.h"

#define TAP_SR_TEST_HAS_EXCEPTIONS TAP_DSP_HAS_EXCEPTIONS

#if TAP_DSP_HAS_EXCEPTIONS
#define TAP_SR_EXPECT_REJECTED(statement, exception) EXPECT_THROW(statement, exception)
#define TAP_SR_EXPECT_ACCEPTED(statement) EXPECT_NO_THROW(statement)
#define TAP_SR_EXPECT_REJECTED_WITH(statement, exception, text)                                                        \
    do {                                                                                                               \
        bool tap_sr_rejected_ = false;                                                                                 \
        try {                                                                                                          \
            statement;                                                                                                 \
        }                                                                                                              \
        catch (const exception& tap_sr_e_) {                                                                           \
            tap_sr_rejected_ = true;                                                                                   \
            EXPECT_STREQ(tap_sr_e_.what(), (text)) << #statement;                                                      \
        }                                                                                                              \
        EXPECT_TRUE(tap_sr_rejected_) << #statement " did not throw " #exception;                                      \
    } while (false)
#else
#if defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
#define TAP_SR_EXPECT_REJECTED(statement, exception) EXPECT_DEATH(statement, "")
#else
#define TAP_SR_EXPECT_REJECTED(statement, exception) static_cast<void>(0)
#endif
#define TAP_SR_EXPECT_ACCEPTED(statement)                                                                              \
    do {                                                                                                               \
        statement;                                                                                                     \
    } while (false)
#define TAP_SR_EXPECT_REJECTED_WITH(statement, exception, text) TAP_SR_EXPECT_REJECTED(statement, exception)
#endif

#endif
