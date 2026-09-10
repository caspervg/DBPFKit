#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string_view>

#include "NetworkRUL.h"
#include "RUL1.h"
#include "RUL2.h"

namespace {
    std::span<const uint8_t> Bytes(const std::string_view text) {
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
    }

    // The four orthogonal shapes of a road that the Road RUL opens with, in the
    // exact form the shipped file writes them.
    constexpr std::string_view kRoadSample =
        "#RoadRules#\r\n"
        ";\r\n"
        ";orthogonal road\r\n"
        ";\r\n"
        "1,00,00,00,00\r\n"
        "3,0,0x00000100,0,0\r\n"
        "1,00,02,00,02\r\n"
        "3,0,0x00004B00,0,0\r\n"
        "1,02,00,02,00\r\n"
        "3,0,0x00004B00,3,0\r\n"
        "1,02,02,02,02\r\n"
        "3,0,0x00020700,0,0\r\n";
}

TEST_CASE("NetworkRUL reads the header and the rule blocks", "[rul][network]") {
    const auto parsed = NetworkRUL::Parse(Bytes(kRoadSample));
    REQUIRE(parsed.has_value());
    CHECK(parsed->header == NetworkRUL::HeaderKind::NETWORK);
    CHECK(parsed->headerText == "#RoadRules#");
    REQUIRE(parsed->rules.size() == 4);

    const auto& straight = parsed->rules[1];
    REQUIRE(straight.Centre() != nullptr);
    // A north-south road connects on its north and south sides only.
    CHECK(straight.Centre()->edges[static_cast<size_t>(NetworkRUL::Side::NORTH)] ==
          NetworkRUL::EdgeFlag::STRAIGHT);
    CHECK(straight.Centre()->edges[static_cast<size_t>(NetworkRUL::Side::SOUTH)] ==
          NetworkRUL::EdgeFlag::STRAIGHT);
    CHECK(straight.Centre()->edges[static_cast<size_t>(NetworkRUL::Side::WEST)] ==
          NetworkRUL::EdgeFlag::NONE);

    const auto* placement = straight.PlacementFor(0);
    REQUIRE(placement != nullptr);
    CHECK(placement->textureId == 0x00004B00);
    CHECK(placement->rotation == 0);
    CHECK_FALSE(placement->flip);
}

TEST_CASE("NetworkRUL reads type two neighbour constraints", "[rul][network]") {
    constexpr std::string_view kSample =
        "#Transmogrify\r\n"
        "1,00,02,00,02\r\n"
        "2,3,00,00,00,02\r\n"
        "3,0,0x00001234,1,1\r\n";

    const auto parsed = NetworkRUL::Parse(Bytes(kSample));
    REQUIRE(parsed.has_value());
    CHECK(parsed->header == NetworkRUL::HeaderKind::TRANSMOGRIFY);
    REQUIRE(parsed->rules.size() == 1);

    const auto& rule = parsed->rules.front();
    REQUIRE(rule.constraints.size() == 2);
    CHECK(rule.constraints[0].position == 0);
    CHECK(rule.constraints[1].position == 3); // due north
    REQUIRE(rule.placements.size() == 1);
    CHECK(rule.placements[0].flip);
    CHECK(rule.placements[0].rotation == 1);
}

TEST_CASE("NetworkRUL positions walk two rings clockwise from due west", "[rul][network]") {
    int32_t x = 99;
    int32_t z = 99;

    REQUIRE(NetworkRUL::PositionOffset(0, x, z));
    CHECK(x == 0);
    CHECK(z == 0);

    REQUIRE(NetworkRUL::PositionOffset(1, x, z)); // west
    CHECK(x == -1);
    CHECK(z == 0);
    REQUIRE(NetworkRUL::PositionOffset(3, x, z)); // north
    CHECK(x == 0);
    CHECK(z == -1);
    REQUIRE(NetworkRUL::PositionOffset(5, x, z)); // east
    CHECK(x == 1);
    CHECK(z == 0);
    REQUIRE(NetworkRUL::PositionOffset(7, x, z)); // south
    CHECK(x == 0);
    CHECK(z == 1);

    REQUIRE(NetworkRUL::PositionOffset(9, x, z)); // two tiles west
    CHECK(x == -2);
    CHECK(z == 0);
    REQUIRE(NetworkRUL::PositionOffset(24, x, z)); // west-southwest
    CHECK(x == -2);
    CHECK(z == 1);

    CHECK_FALSE(NetworkRUL::PositionOffset(25, x, z));
}

TEST_CASE("NetworkRUL turns a rule round its centre", "[rul][network]") {
    NetworkRUL::Rule rule;
    NetworkRUL::Constraint centre;
    centre.edges[static_cast<size_t>(NetworkRUL::Side::WEST)] = NetworkRUL::EdgeFlag::STRAIGHT;
    rule.constraints.push_back(centre);

    NetworkRUL::Constraint neighbour;
    neighbour.position = 1; // due west
    neighbour.edges[static_cast<size_t>(NetworkRUL::Side::EAST)] = NetworkRUL::EdgeFlag::STRAIGHT;
    rule.constraints.push_back(neighbour);
    rule.placements.push_back({0, 0x1234, 0, false});

    const auto turned = NetworkRUL::Rotate(rule, 1);
    // The west edge becomes the north edge.
    CHECK(turned.constraints[0].edges[static_cast<size_t>(NetworkRUL::Side::NORTH)] ==
          NetworkRUL::EdgeFlag::STRAIGHT);
    CHECK(turned.constraints[0].edges[static_cast<size_t>(NetworkRUL::Side::WEST)] == 0);
    // The tile due west ends up due north, and its east edge becomes its south.
    CHECK(turned.constraints[1].position == 3);
    CHECK(turned.constraints[1].edges[static_cast<size_t>(NetworkRUL::Side::SOUTH)] ==
          NetworkRUL::EdgeFlag::STRAIGHT);
    CHECK(turned.placements[0].rotation == 1);

    const auto full = NetworkRUL::Rotate(rule, 4);
    CHECK(full.constraints[0].edges == rule.constraints[0].edges);
    CHECK(full.constraints[1].position == rule.constraints[1].position);
    CHECK(full.placements[0].rotation == rule.placements[0].rotation);
}

TEST_CASE("NetworkRUL names the rule files of each network", "[rul][network]") {
    // The rule files are numbered in their own order, not the networks' order.
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::HIGHWAY) == 0x01);
    CHECK(NetworkRUL::AdvancedInstance(NetworkRUL::NetworkType::HIGHWAY) == 0x02);
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::RAIL) == 0x05);
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::ROAD) == 0x07);
    CHECK(NetworkRUL::AdvancedInstance(NetworkRUL::NetworkType::ROAD) == 0x08);
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::STREET) == 0x09);
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::GROUND_HIGHWAY) == 0x17);
    CHECK(NetworkRUL::AdvancedInstance(NetworkRUL::NetworkType::GROUND_HIGHWAY) == 0x18);
    // Power lines are laid by their own code and ship no rule file.
    CHECK_FALSE(NetworkRUL::HasRuleFiles(NetworkRUL::NetworkType::POWERLINE));
    CHECK(NetworkRUL::BasicInstance(NetworkRUL::NetworkType::POWERLINE) == 0);
}

TEST_CASE("NetworkRUL rejects a file with no header", "[rul][network]") {
    CHECK_FALSE(NetworkRUL::Parse(Bytes("1,00,00,00,00\r\n3,0,0x1,0,0\r\n")).has_value());
    CHECK_FALSE(NetworkRUL::Parse(Bytes("")).has_value());
}

TEST_CASE("RUL1 reads sections and their rules", "[rul][rul1]") {
    constexpr std::string_view kSample =
        "[RoadRailIntersectionSolutions]\r\n"
        "; straight road, straight rail\r\n"
        "rot0=0x02000200,0x00020002,0x03010100,0,0\r\n"
        "rot1=0x00020002,0x02000200,0x03010100,1,0\r\n";

    const auto parsed = RUL1::Parse(Bytes(kSample));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->sections.size() == 1);

    const auto& section = parsed->sections.front();
    CHECK(section.title == "RoadRailIntersectionSolutions");
    CHECK(section.pairName == "RoadRail");
    REQUIRE(section.rules.size() == 2);
    CHECK(section.rules[0].name == "rot0");
    CHECK(section.rules[0].textureId == 0x03010100);
    CHECK(section.rules[0].rotation == 0);
    CHECK(section.rules[1].rotation == 1);
}

TEST_CASE("RUL1 packs edge flags one byte per side", "[rul][rul1]") {
    // A north-south road: straight on north and south, nothing east or west.
    const auto edges = RUL1::UnpackEdges(0x02000200u);
    CHECK(edges[0] == 0x00u); // west
    CHECK(edges[1] == 0x02u); // north
    CHECK(edges[2] == 0x00u); // east
    CHECK(edges[3] == 0x02u); // south
    CHECK(RUL1::PackEdges(edges) == 0x02000200u);
}

TEST_CASE("RUL2 reads a side by side replacement", "[rul][rul2]") {
    constexpr std::string_view kSample =
        "[RuleOverrides]\r\n"
        ";road to street transition\r\n"
        "0x00004B00,0,0,0x05004B00,0,0=0x00004B00,0,0,0x05004B01,1,1\r\n";

    const auto parsed = RUL2::Parse(Bytes(kSample));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->rules.size() == 1);

    const auto& rule = parsed->rules.front();
    CHECK(rule.left.textureId == 0x00004B00);
    CHECK(rule.right.textureId == 0x05004B00);
    CHECK(rule.newLeft == rule.left);
    CHECK(rule.newRight.textureId == 0x05004B01);
    CHECK(rule.newRight.rotation == 1);
    CHECK(rule.newRight.flip);
}

TEST_CASE("RUL2 rejects a file with no rules", "[rul][rul2]") {
    CHECK_FALSE(RUL2::Parse(Bytes("[RuleOverrides]\r\n;nothing here\r\n")).has_value());
    CHECK_FALSE(RUL2::Parse(Bytes("")).has_value());
}
