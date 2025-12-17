#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "RUL0.h"

using namespace RUL0::ParseHelpers;

TEST_CASE("Trim removes leading and trailing whitespace")
{
    CHECK(Trim("  abc ") == "abc");
    CHECK(Trim("\txyz\t") == "xyz");
    CHECK(Trim("no-space") == "no-space");
    CHECK(Trim("   ").empty());
}

TEST_CASE("ParseInt parses signed integers")
{
    int value = 0;
    CHECK(ParseInt("42", value));
    CHECK(value == 42);

    CHECK(ParseInt("-7", value));
    CHECK(value == -7);

    CHECK_FALSE(ParseInt("12a", value));
}

TEST_CASE("ParseIntAuto handles decimal, octal, and hex")
{
    auto value = 0;
    CHECK(ParseIntAuto("10", value));
    CHECK(value == 10);

    CHECK(ParseIntAuto("012", value)); // octal -> 10 decimal
    CHECK(value == 10);

    CHECK(ParseIntAuto("0007", value)); // octal -> 7 decimal
    CHECK(value == 7);

    CHECK(ParseIntAuto("0x1A", value));
    CHECK(value == 26);

    CHECK(ParseIntAuto("  0Xf  ", value));
    CHECK(value == 15);

    CHECK_FALSE(ParseIntAuto("0x", value));
    CHECK_FALSE(ParseIntAuto("089", value)); // invalid octal digits
    CHECK_FALSE(ParseIntAuto("09", value));  // invalid octal digits
}

TEST_CASE("ParseFloat parses floating point numbers")
{
    auto value = 0.0f;
    CHECK(ParseFloat("3.14", value));
    CHECK(value == Catch::Approx(3.14f));

    CHECK(ParseFloat(" -2.5 ", value));
    CHECK(value == Catch::Approx(-2.5f));

    CHECK_FALSE(ParseFloat("nan-ish", value));
}

TEST_CASE("ParseHex accepts optional 0x prefix")
{
    uint32_t value = 0;
    CHECK(ParseHex("1a", value));
    CHECK(value == 0x1a);

    CHECK(ParseHex("0xFF", value));
    CHECK(value == 0xFF);

    CHECK_FALSE(ParseHex("0x", value));
    CHECK_FALSE(ParseHex("G1", value));
}

TEST_CASE("ParseIntPair parses comma-separated integer pairs")
{
    int a = 0;
    int b = 0;
    CHECK(ParseIntPair("1,2", a, b));
    CHECK(a == 1);
    CHECK(b == 2);

    CHECK(ParseIntPair("  -3 , 4", a, b));
    CHECK(a == -3);
    CHECK(b == 4);

    CHECK_FALSE(ParseIntPair("1;", a, b));
}

TEST_CASE("Case-insensitive helpers")
{
    CHECK(EqualsIgnoreCase("Piece", "piece"));
    CHECK_FALSE(EqualsIgnoreCase("Piece", "pieces"));

    CHECK(StartsWithIgnoreCase("ReplacementIntersection", "replacement"));
    CHECK_FALSE(StartsWithIgnoreCase("Ordering", "Orderx"));
}
