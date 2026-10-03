// SPDX-License-Identifier: MIT
//
// Keyword recognition and the sequential reader.

#include <doctest/doctest.h>

#include "omsi/cfg/cfg.hpp"

using omsi::cfg::CfgFile;
using omsi::cfg::CfgReader;
using omsi::cfg::KeywordRule;
using omsi::cfg::keywordOf;
using omsi::cfg::keywordWith;

TEST_CASE("a keyword is the whole line") {
    CHECK(keywordOf("[mesh]") == "mesh");
    CHECK(keywordOf("[setvar]") == "setvar");
}

TEST_CASE("free text is not a keyword") {
    // Leading whitespace: the stock files' help texts look exactly like this.
    CHECK(keywordOf("  [mesh]").empty());
    CHECK(keywordOf("\t[mesh]").empty());
    // Trailing whitespace makes it free text too.
    CHECK(keywordOf("[mesh] ").empty());
    // Not the whole line.
    CHECK(keywordOf("[mesh] makes a mesh").empty());
    CHECK(keywordOf("see [mesh] below").empty());
    CHECK(keywordOf("").empty());
    CHECK(keywordOf("[").empty());
    CHECK(keywordOf("[]").empty());
    CHECK(keywordOf("mesh").empty());
    // A second bracket never appears in a keyword.
    CHECK(keywordOf("[a[b]").empty());
}

TEST_CASE("a keyword the original knows is spelled its way") {
    // The original compares by plain equality, so its spelling is the only one that is a
    // keyword; a mod's different spelling is free text.
    CHECK(keywordOf("[matl_noZwrite]") == "matl_noZwrite");
    CHECK(keywordOf("[matl_nozwrite]").empty());
    CHECK(keywordOf("[LOD]") == "LOD");
    CHECK(keywordOf("[lod]").empty());
    CHECK(keywordOf("[NightMapMode]") == "NightMapMode");
    CHECK(keywordOf("[nightmapmode]").empty());
}

TEST_CASE("a keyword the original does not know ignores case") {
    CHECK(keywordOf("[SomeModKey]") == "SomeModKey");
    CHECK(keywordOf("[somemodkey]") == "somemodkey");
    CHECK(keywordOf("[SOMEMODKEY]") == "SOMEMODKEY");
}

TEST_CASE("the .hof rule cuts trailing tabs, spaces and quotes") {
    CHECK(keywordWith("[infosystem_trip]\t\t\t\t", KeywordRule::TrimEnd) == "infosystem_trip");
    CHECK(keywordWith("[infosystem_trip]   ", KeywordRule::TrimEnd) == "infosystem_trip");
    CHECK(keywordWith("[infosystem_trip]\"", KeywordRule::TrimEnd) == "infosystem_trip");
    // Without the rule the same line is free text.
    CHECK(keywordWith("[infosystem_trip]\t", KeywordRule::Exact).empty());
}

TEST_CASE("the ailists rule ignores case entirely") {
    CHECK(keywordWith("[LOD]", KeywordRule::AnyCase) == "LOD");
    CHECK(keywordWith("[lod]", KeywordRule::AnyCase) == "lod");
    CHECK(keywordWith("[Matl_nozwrite]", KeywordRule::AnyCase) == "Matl_nozwrite");
}

TEST_CASE("the reader walks blocks and reads their parameters") {
    const auto file =
        CfgFile::fromString("t.bus", "[mesh]\nrad2.obj\n0 0 0\n[mesh]\nrad3.obj\n1 1 1\n");
    CfgReader reader(file);

    REQUIRE(reader.seek());
    CHECK(reader.keyword() == "mesh");
    CHECK(reader.next() == "rad2.obj");
    CHECK(reader.next() == "0 0 0");

    REQUIRE(reader.seek());
    CHECK(reader.keyword() == "mesh");
    CHECK(reader.next() == "rad3.obj");
    CHECK(reader.next() == "1 1 1");

    CHECK(!reader.seek());
}

TEST_CASE("free text between blocks is ignored") {
    const auto file = CfgFile::fromString(
        "t.cfg", "This file does something.\r\n\r\n[general]\r\nname Test\r\n\r\nAnd a note.\r\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.keyword() == "general");
    CHECK(reader.nextNonEmpty() == "name Test");
}

TEST_CASE("an empty line is a legal parameter") {
    const auto file = CfgFile::fromString("t.cfg", "[a]\n\n\n[b]\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    // Read verbatim: the two empty lines are parameters in their own right.
    CHECK(reader.next().empty());
    CHECK(reader.next().empty());
    REQUIRE(reader.seek());
    CHECK(reader.keyword() == "b");
}

TEST_CASE("nextNonEmpty steps over the blank separators") {
    const auto file = CfgFile::fromString("t.cfg", "[a]\n\n   \nvalue\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.nextNonEmpty() == "value");
}

TEST_CASE("is() compares without regard to case") {
    const auto file = CfgFile::fromString("t.cfg", "[NewAttachment]\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.is("newattachment"));
    CHECK(reader.is("NEWATTACHMENT"));
    CHECK(!reader.is("mesh"));
}

TEST_CASE("the reader reports where a block started") {
    const auto file = CfgFile::fromString("t.cfg", "junk\n[one]\na\n\n[two]\nb\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.keywordLine() == 1);
    REQUIRE(reader.seek());
    CHECK(reader.keywordLine() == 4);
}

TEST_CASE("bare tokens can be accepted alongside keywords") {
    const auto file = CfgFile::fromString("t.cfg", "[a]\n-<DISABLED>-\n[b]\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.keyword() == "a");
    REQUIRE(reader.seek({"-<DISABLED>-"}));
    CHECK(reader.keyword() == "-<DISABLED>-");
}

TEST_CASE("disabled blocks are skipped when the loader honours the markers") {
    const std::string text =
        "[mesh]\n"
        "keep.obj\n"
        "-<DISABLED>-\n"
        "[mesh]\n"
        "gone.obj\n"
        "-<ENABLED>-\n"
        "[mesh]\n"
        "also-keep.obj\n";

    SUBCASE("the .bus loader skips what is switched off") {
        const auto file = CfgFile::fromString("t.bus", text);
        CfgReader reader(file);
        reader.skippingDisabledBlocks();

        REQUIRE(reader.seek());
        CHECK(reader.next() == "keep.obj");
        REQUIRE(reader.seek());
        // The block between the markers is gone, its keyword line included.
        CHECK(reader.next() == "also-keep.obj");
        CHECK(!reader.seek());
    }

    SUBCASE("a loader that does not still sees them") {
        const auto file = CfgFile::fromString("t.map", text);
        CfgReader reader(file);

        int meshes = 0;
        while (reader.seek()) {
            if (reader.is("mesh")) {
                ++meshes;
            }
        }
        CHECK(meshes == 3);
    }
}

TEST_CASE("values are parsed with OMSI's rules") {
    const auto file = CfgFile::fromString("t.cfg", "[m]\n12.5\n-3\n2.5 km\nnope\n");
    CfgReader reader(file);
    REQUIRE(reader.seek());
    CHECK(reader.nextF32() == 12.5F);
    CHECK(reader.nextF32() == -3.0F);
    CHECK(reader.nextFloat() == 2.5);
    CHECK(reader.nextF32() == 0.0F);  // garbage is zero
}