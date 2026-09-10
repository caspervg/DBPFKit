#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ParseTypes.h"

// RUL 0x10000001, the Intersection Solutions file.
//
// It says what to lay where one network crosses a *different* one. Crossings
// of a network with itself are handled by that network's own rule files; see
// NetworkRUL. The file is a set of bracketed sections, one per ordered pair of
// networks, each holding lines of the form:
//
//   name=network1Flags,network2Flags,textureId,rotation,flip
//
// The flags are the two networks' edge values on the tile, packed one byte per
// side. Only the plain connection values are valid here, because two networks
// can only cross on a straight run of both.
namespace RUL1 {

    constexpr uint32_t kTypeId = 0x0A5BCF4B;
    constexpr uint32_t kGroupId = 0xAA5BCF57;
    constexpr uint32_t kInstanceId = 0x10000001;

    constexpr size_t kSideCount = 4;
    constexpr auto kSectionSuffix = "IntersectionSolutions";

    struct Rule {
        uint32_t firstEdges = 0;  // packed edge flags of the first network
        uint32_t secondEdges = 0; // packed edge flags of the second network
        uint32_t textureId = 0;
        uint8_t rotation = 0;
        bool flip = false;
        std::string name; // the key of the line; unused by the game
    };

    struct Section {
        std::string title;      // the section name exactly as written
        std::string pairName;   // the title with "IntersectionSolutions" removed
        std::vector<Rule> rules;
    };

    struct Record {
        std::vector<Section> sections;
        // Lines of the trailing "Allowed Overrides" style sections that are not
        // network pairs are kept whole, so nothing is silently dropped.
        std::vector<std::string> otherSections;
    };

    // Unpacks the four side flags of a rule. They are stored one byte per side:
    // west in the low byte, then north, east and south.
    [[nodiscard]] std::array<uint32_t, kSideCount> UnpackEdges(uint32_t packed) noexcept;
    [[nodiscard]] uint32_t PackEdges(const std::array<uint32_t, kSideCount>& edges) noexcept;

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);
}
