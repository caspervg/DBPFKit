#include "RUL2.h"

#include "RulTextHelpers.h"

namespace RUL2 {
    namespace {
        // Each side of a rule names two tiles, three fields apiece.
        constexpr size_t kFieldsPerTile = 3;
        constexpr size_t kFieldsPerSide = kFieldsPerTile * 2;

        [[nodiscard]] bool ReadTile(const std::span<const std::string_view> fields,
                                    const size_t at, Tile& tile) {
            uint32_t rotation = 0;
            uint32_t flip = 0;
            if (!RulText::ParseNumber(fields[at], tile.textureId) ||
                !RulText::ParseNumber(fields[at + 1], rotation) ||
                !RulText::ParseNumber(fields[at + 2], flip)) {
                return false;
            }
            tile.rotation = static_cast<uint8_t>(rotation & 0x03u);
            tile.flip = flip != 0;
            return true;
        }
    }

    ParseExpected<Record> Parse(const std::span<const uint8_t> buffer) {
        if (buffer.empty()) {
            return Fail("RUL2 buffer is empty");
        }

        Record record;
        for (const std::string_view line : RulText::Lines(buffer)) {
            // The whole file is one section, so a bracketed line is only the
            // section title and carries no rule.
            if (RulText::IsSkippable(line) || line.front() == '[') {
                continue;
            }
            const size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                continue;
            }

            const auto match = RulText::Split(line.substr(0, equals), RulText::kFieldDelimiter);
            const auto replace = RulText::Split(line.substr(equals + 1), RulText::kFieldDelimiter);
            if (match.size() < kFieldsPerSide || replace.size() < kFieldsPerSide) {
                continue;
            }

            Rule rule;
            const bool valid = ReadTile(match, 0, rule.left) &&
                               ReadTile(match, kFieldsPerTile, rule.right) &&
                               ReadTile(replace, 0, rule.newLeft) &&
                               ReadTile(replace, kFieldsPerTile, rule.newRight);
            if (!valid) {
                continue;
            }
            record.rules.push_back(rule);
        }

        if (record.rules.empty()) {
            return Fail("RUL2 holds no override rules");
        }
        return record;
    }
}
