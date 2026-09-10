#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ParseTypes.h"

// Individual Network RUL files (type 0x0A5BCF4B, group 0xAA5BCF57,
// instances 0x01 to 0x18).
//
// Each transit network owns two of these: a basic file covering its ordinary
// placement and an advanced file covering the harder single-network cases.
// A file is a header line followed by rule blocks, where each block is a type
// one line naming the tile the rule is about, any number of type two lines
// naming tiles that must match around it, and type three lines saying what to
// lay on each of them.
//
// Crossings between two *different* networks live in RUL1 instead, and
// side-by-side replacements live in RUL2.
namespace NetworkRUL {

    constexpr uint32_t kTypeId = 0x0A5BCF4B;
    constexpr uint32_t kGroupId = 0xAA5BCF57;

    constexpr size_t kSideCount = 4;
    // The two rings of tiles a rule may constrain around its centre.
    constexpr uint32_t kMaxPosition = 24;

    // The transit networks, numbered the way SimCity 4 numbers them. This is
    // the same order RUL0::NetworkType uses, and the order that indexes the
    // per-network arrays of the simulator tuning exemplars.
    enum class NetworkType : uint32_t {
        ROAD = 0,
        RAIL = 1,
        HIGHWAY = 2, // the elevated highway
        STREET = 3,
        PIPE = 4,
        POWERLINE = 5,
        AVENUE = 6,
        SUBWAY = 7,
        LIGHT_RAIL = 8, // the elevated train
        MONORAIL = 9,
        ONE_WAY_ROAD = 10,
        DIRT_ROAD = 11,
        GROUND_HIGHWAY = 12,
        COUNT = 13,
    };

    // Instance of each network's basic rule file. The rule files are numbered
    // in their own order, which is not the order the networks are, so a table
    // bridges the two. Power lines ship no rule file and map to zero.
    inline constexpr std::array<uint32_t, static_cast<size_t>(NetworkType::COUNT)>
        kBasicRuleInstance{
            0x07, // road
            0x05, // rail
            0x01, // elevated highway
            0x09, // street
            0x03, // pipe
            0x00, // power lines have no rule file
            0x0D, // avenue
            0x0B, // subway
            0x0F, // elevated rail
            0x15, // monorail
            0x11, // one way road
            0x13, // dirt road
            0x17, // ground highway
        };

    [[nodiscard]] constexpr uint32_t BasicInstance(const NetworkType network) noexcept {
        const auto index = static_cast<size_t>(network);
        return index < kBasicRuleInstance.size() ? kBasicRuleInstance[index] : 0u;
    }
    // The advanced file always follows the basic one.
    [[nodiscard]] constexpr uint32_t AdvancedInstance(const NetworkType network) noexcept {
        const uint32_t basic = BasicInstance(network);
        return basic == 0u ? 0u : basic + 1u;
    }
    [[nodiscard]] constexpr bool HasRuleFiles(const NetworkType network) noexcept {
        return BasicInstance(network) != 0u;
    }

    [[nodiscard]] std::string_view NetworkName(NetworkType network) noexcept;

    // Sides of a tile, in the order a rule line lists them.
    enum class Side : size_t {
        WEST = 0,
        NORTH = 1,
        EAST = 2,
        SOUTH = 3,
    };

    // Edge flag values documented on the Network Flags page. A rule is free to
    // use others, notably for rail, so these are named constants rather than a
    // closed enumeration.
    namespace EdgeFlag {
        constexpr uint32_t NONE = 0;      // no connection
        constexpr uint32_t LEFT_45 = 1;   // left at forty five degrees
        constexpr uint32_t STRAIGHT = 2;  // straight connection
        constexpr uint32_t RIGHT_45 = 3;  // right at forty five degrees
        constexpr uint32_t MEDIAN = 4;    // shared median of a two tile network
        constexpr uint32_t BLEND_LEFT = 11;
        constexpr uint32_t BLEND_RIGHT = 13;
    }

    enum class HeaderKind {
        UNKNOWN,
        // "#<network>Rules#": every orientation is written out in the file.
        NETWORK,
        // "#Transmogrify": each rule stands for all four of its rotations.
        TRANSMOGRIFY,
    };

    // A tile the rule constrains. Position zero is the tile the rule is about;
    // one to twenty four walk the two rings around it, clockwise from due west.
    struct Constraint {
        uint32_t position = 0;
        std::array<uint32_t, kSideCount> edges{};
    };

    // A texture or model the rule lays on one of its tiles.
    struct Placement {
        uint32_t position = 0;
        uint32_t textureId = 0; // FSH instance, or an exemplar naming a model
        uint8_t rotation = 0;   // quarter turns clockwise
        bool flip = false;
    };

    struct Rule {
        std::vector<Constraint> constraints; // the first is always position zero
        std::vector<Placement> placements;

        [[nodiscard]] const Constraint* Centre() const noexcept {
            return constraints.empty() ? nullptr : &constraints.front();
        }
        [[nodiscard]] const Placement* PlacementFor(uint32_t position) const noexcept;
    };

    struct Record {
        HeaderKind header = HeaderKind::UNKNOWN;
        std::string headerText;
        std::vector<Rule> rules;
    };

    // Offset of a constraint position from the tile the rule is about.
    // Position zero is that tile. Returns false for a position out of range.
    [[nodiscard]] bool PositionOffset(uint32_t position, int32_t& outX, int32_t& outZ) noexcept;

    // The same rule turned a quarter circle clockwise, the given number of
    // times: the edges move round the tile, the constrained positions move
    // round the rings, and the placements gain the turn. Flips are not
    // expanded, because a mirrored rule is not simply a turned one.
    [[nodiscard]] Rule Rotate(const Rule& rule, uint32_t quarterTurns);

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);
}
