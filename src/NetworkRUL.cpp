#include "NetworkRUL.h"

#include <utility>

#include "RulTextHelpers.h"

namespace NetworkRUL {
    namespace {
        // The two rings a rule may constrain, clockwise from due west. The
        // first eight touch the centre; the next sixteen are the ring beyond.
        constexpr std::array<std::pair<int32_t, int32_t>, kMaxPosition> kPositions{{
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}, {0, -2}, {1, -2}, {2, -2}, {2, -1},
            {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {-1, 2}, {-2, 2}, {-2, 1},
        }};
    }

    std::string_view NetworkName(const NetworkType network) noexcept {
        switch (network) {
            case NetworkType::ROAD: return "road";
            case NetworkType::RAIL: return "rail";
            case NetworkType::HIGHWAY: return "highway";
            case NetworkType::STREET: return "street";
            case NetworkType::PIPE: return "pipe";
            case NetworkType::POWERLINE: return "power line";
            case NetworkType::AVENUE: return "avenue";
            case NetworkType::SUBWAY: return "subway";
            case NetworkType::LIGHT_RAIL: return "elevated rail";
            case NetworkType::MONORAIL: return "monorail";
            case NetworkType::ONE_WAY_ROAD: return "one way road";
            case NetworkType::DIRT_ROAD: return "dirt road";
            case NetworkType::GROUND_HIGHWAY: return "ground highway";
            default: return "unknown";
        }
    }

    const Placement* Rule::PlacementFor(const uint32_t position) const noexcept {
        for (const auto& placement : placements) {
            if (placement.position == position) {
                return &placement;
            }
        }
        return nullptr;
    }

    bool PositionOffset(const uint32_t position, int32_t& outX, int32_t& outZ) noexcept {
        if (position == 0) {
            outX = 0;
            outZ = 0;
            return true;
        }
        if (position > kMaxPosition) {
            return false;
        }
        const auto [x, z] = kPositions[position - 1];
        outX = x;
        outZ = z;
        return true;
    }

    Rule Rotate(const Rule& rule, const uint32_t quarterTurns) {
        const uint32_t turns = quarterTurns & 0x03u;
        if (turns == 0) {
            return rule;
        }

        // A quarter turn clockwise carries the west edge to the north, the
        // north edge to the east, and so on round the tile.
        const auto turnEdges = [turns](const std::array<uint32_t, kSideCount>& edges) {
            std::array<uint32_t, kSideCount> result{};
            for (size_t side = 0; side < kSideCount; ++side) {
                result[(side + turns) % kSideCount] = edges[side];
            }
            return result;
        };

        // The inner ring holds eight tiles, so a quarter turn is two steps
        // round it; the outer ring holds sixteen, so the same turn is four.
        const auto turnPosition = [turns](const uint32_t position) -> uint32_t {
            if (position == 0) {
                return 0;
            }
            if (position <= 8) {
                return ((position - 1 + turns * 2) % 8) + 1;
            }
            if (position <= kMaxPosition) {
                return ((position - 9 + turns * 4) % 16) + 9;
            }
            return position;
        };

        Rule turned;
        turned.constraints.reserve(rule.constraints.size());
        turned.placements.reserve(rule.placements.size());
        for (const auto& constraint : rule.constraints) {
            turned.constraints.push_back({turnPosition(constraint.position),
                                          turnEdges(constraint.edges)});
        }
        for (const auto& placement : rule.placements) {
            Placement moved = placement;
            moved.position = turnPosition(placement.position);
            moved.rotation = static_cast<uint8_t>((placement.rotation + turns) & 0x03u);
            turned.placements.push_back(moved);
        }
        return turned;
    }

    ParseExpected<Record> Parse(const std::span<const uint8_t> buffer) {
        if (buffer.empty()) {
            return Fail("Network RUL buffer is empty");
        }
        const auto lines = RulText::Lines(buffer);

        size_t index = 0;
        while (index < lines.size() && lines[index].empty()) {
            ++index;
        }
        if (index == lines.size() || lines[index].front() != '#') {
            return Fail("Network RUL has no header line");
        }

        Record record;
        record.headerText = std::string(lines[index]);
        record.header = record.headerText.starts_with("#Transmogrify")
                            ? HeaderKind::TRANSMOGRIFY
                        : record.headerText.ends_with("Rules#")
                            ? HeaderKind::NETWORK
                            : HeaderKind::UNKNOWN;
        ++index;

        Rule current;
        bool building = false;
        const auto flush = [&record, &current, &building] {
            if (building && !current.constraints.empty()) {
                record.rules.push_back(current);
            }
            current = Rule{};
            building = false;
        };

        for (; index < lines.size(); ++index) {
            const std::string_view line = lines[index];
            if (RulText::IsSkippable(line)) {
                continue;
            }
            const auto fields = RulText::Split(line, RulText::kFieldDelimiter);
            if (fields.empty()) {
                continue;
            }
            uint32_t kind = 0;
            if (!RulText::ParseNumber(fields[0], kind)) {
                continue;
            }

            if (kind == 1) {
                // A type one line opens a rule and describes its centre tile.
                flush();
                if (fields.size() < 1 + kSideCount) {
                    continue;
                }
                Constraint centre;
                bool valid = true;
                for (size_t side = 0; side < kSideCount; ++side) {
                    valid = valid && RulText::ParseNumber(fields[side + 1], centre.edges[side]);
                }
                if (!valid) {
                    continue;
                }
                current.constraints.push_back(centre);
                building = true;
            } else if (kind == 2 && building) {
                // A type two line names a tile that must match around it.
                if (fields.size() < 2 + kSideCount) {
                    continue;
                }
                Constraint neighbour;
                bool valid = RulText::ParseNumber(fields[1], neighbour.position);
                for (size_t side = 0; side < kSideCount; ++side) {
                    valid = valid && RulText::ParseNumber(fields[side + 2], neighbour.edges[side]);
                }
                if (!valid) {
                    continue;
                }
                current.constraints.push_back(neighbour);
            } else if (kind == 3 && building) {
                // A type three line lays a texture on one of the rule's tiles.
                if (fields.size() < 5) {
                    continue;
                }
                Placement placement;
                uint32_t rotation = 0;
                uint32_t flip = 0;
                const bool valid = RulText::ParseNumber(fields[1], placement.position) &&
                                   RulText::ParseNumber(fields[2], placement.textureId) &&
                                   RulText::ParseNumber(fields[3], rotation) &&
                                   RulText::ParseNumber(fields[4], flip);
                if (!valid) {
                    continue;
                }
                placement.rotation = static_cast<uint8_t>(rotation & 0x03u);
                placement.flip = flip != 0;
                current.placements.push_back(placement);
            }
        }
        flush();

        if (record.rules.empty()) {
            return Fail("Network RUL '{}' holds no rules", record.headerText);
        }
        return record;
    }
}
