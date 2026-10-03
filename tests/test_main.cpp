// SPDX-License-Identifier: MIT
//
// The test suite's entry point.
//
// doctest generates its implementation and main() from whichever translation unit
// defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN, so it lives here alone; every other test
// file just includes the header.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>