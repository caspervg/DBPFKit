#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ParseTypes.h"

// RUL 0x10000002, the Override file.
//
// It replaces a pair of tiles that sit side by side. Each line reads:
//
//   id1,rot1,flip1,id2,rot2,flip2=id3,rot3,flip3,id4,rot4,flip4
//
// meaning that wherever tile one is found with tile two immediately to its
// right, tile three replaces the first and tile four replaces the second. The
// left and right of a rule are not fixed to compass directions: a rule holds
// wherever the right edge of the first tile meets the left edge of the second,
// so one line covers all four orientations.
namespace RUL2 {

    constexpr uint32_t kTypeId = 0x0A5BCF4B;
    constexpr uint32_t kGroupId = 0xAA5BCF57;
    constexpr uint32_t kInstanceId = 0x10000002;

    struct Tile {
        uint32_t textureId = 0;
        uint8_t rotation = 0;
        bool flip = false;

        [[nodiscard]] bool operator==(const Tile&) const = default;
    };

    struct Rule {
        Tile left;      // the tile to match on the left
        Tile right;     // the tile to match on its right
        Tile newLeft;   // what replaces the left tile
        Tile newRight;  // what replaces the right tile
    };

    struct Record {
        std::vector<Rule> rules;
    };

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);
}
