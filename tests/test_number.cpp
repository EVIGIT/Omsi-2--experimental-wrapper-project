// SPDX-License-Identifier: MIT
//
// OMSI's lenient number parsing.

#include <doctest/doctest.h>

#include "omsi/cfg/cfg.hpp"

using omsi::cfg::parseF32;
using omsi::cfg::parseF64;
using omsi::cfg::parseI32;
using omsi::cfg::parseI64;

TEST_CASE("parseF64 reads what OMSI writes") {
    CHECK(parseF64("0") == 0.0);
    CHECK(parseF64("1") == 1.0);
    CHECK(parseF64("-3.5") == -3.5);
    CHECK(parseF64("+7") == 7.0);

    // The stock files carry values with a long mantissa and an exponent.
    CHECK(parseF64("6.21874087223886E-7") == doctest::Approx(6.21874087223886e-7));
}

TEST_CASE("parseF64 accepts the shapes Delphi accepts") {
    CHECK(parseF64("1.") == 1.0);     // Delphi: "1." is one
    CHECK(parseF64(".5") == 0.5);     // Delphi: ".5" is a half
    CHECK(parseF64("1,5") == 1.5);    // a comma is the decimal separator in many locales
    CHECK(parseF64("-.25") == -0.25);
}

TEST_CASE("parseF64 ignores a unit or comment after whitespace") {
    CHECK(parseF64("5 (metres)") == 5.0);
    CHECK(parseF64("2.5 km") == 2.5);
    CHECK(parseF64("40\tkm/h") == 40.0);
}

TEST_CASE("parseF64 falls back to the longest numeric prefix") {
    CHECK(parseF64("5x") == 5.0);
    CHECK(parseF64("12.5abc") == 12.5);
}

TEST_CASE("garbage is zero, as Delphi's default") {
    CHECK(parseF64("") == 0.0);
    CHECK(parseF64("   ") == 0.0);
    CHECK(parseF64("NORDSPITZE") == 0.0);
    CHECK(parseF64("-") == 0.0);
    CHECK(parseF64(".") == 0.0);
}

TEST_CASE("a value that is not finite is garbage") {
    // Rust and C++ both read "inf" and "nan" as numbers; OMSI never produces one, and a
    // NaN that reached a sort would be far worse than a zero.
    CHECK(parseF64("inf") == 0.0);
    CHECK(parseF64("nan") == 0.0);
    CHECK(parseF64("1e999") == 0.0);
}

TEST_CASE("parseF32 refuses a double beyond its range") {
    CHECK(parseF32("1") == 1.0F);
    CHECK(parseF32("1e300") == 0.0F);  // finite as a double, infinite as a float
}

TEST_CASE("integers truncate the way OMSI does") {
    CHECK(parseI64("42") == 42);
    CHECK(parseI64("-7") == -7);
    // StrToInt fails on "1.0"; OMSI goes through StrToFloat and truncates.
    CHECK(parseI64("1.9") == 1);
    CHECK(parseI64("-1.9") == -1);
    CHECK(parseI64("3.7 km") == 3);
    CHECK(parseI64("nope") == 0);
}

TEST_CASE("parseI32 clamps instead of wrapping") {
    CHECK(parseI32("2147483648") == 2147483647);
    CHECK(parseI32("-2147483649") == -2147483647 - 1);
}