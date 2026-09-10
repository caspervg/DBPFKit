#include "RUL1.h"

#include "RulTextHelpers.h"

namespace RUL1 {

    std::array<uint32_t, kSideCount> UnpackEdges(const uint32_t packed) noexcept {
        return {packed & 0xFFu, (packed >> 8u) & 0xFFu, (packed >> 16u) & 0xFFu,
                (packed >> 24u) & 0xFFu};
    }

    uint32_t PackEdges(const std::array<uint32_t, kSideCount>& edges) noexcept {
        return (edges[0] & 0xFFu) | ((edges[1] & 0xFFu) << 8u) | ((edges[2] & 0xFFu) << 16u) |
               ((edges[3] & 0xFFu) << 24u);
    }

    ParseExpected<Record> Parse(const std::span<const uint8_t> buffer) {
        if (buffer.empty()) {
            return Fail("RUL1 buffer is empty");
        }

        Record record;
        Section current;
        bool open = false;

        const auto flush = [&record, &current, &open] {
            if (open) {
                record.sections.push_back(current);
            }
            current = Section{};
            open = false;
        };

        for (const std::string_view line : RulText::Lines(buffer)) {
            if (RulText::IsSkippable(line)) {
                continue;
            }

            if (line.front() == '[') {
                flush();
                const size_t close = line.find(']');
                current.title = std::string(
                    line.substr(1, close == std::string_view::npos ? line.size() - 1 : close - 1));
                // A pair section is titled "<first><second>IntersectionSolutions".
                // The two names run together with no separator, so splitting
                // them needs a list of network names; the caller does that if
                // it cares, and the pair name is offered stripped of the suffix.
                if (current.title.ends_with(kSectionSuffix)) {
                    current.pairName =
                        current.title.substr(0, current.title.size() - std::string_view(kSectionSuffix).size());
                    open = true;
                } else {
                    record.otherSections.push_back(current.title);
                    current = Section{};
                }
                continue;
            }
            if (!open) {
                continue;
            }

            const size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                continue;
            }
            const auto fields =
                RulText::Split(line.substr(equals + 1), RulText::kFieldDelimiter);
            if (fields.size() < 5) {
                continue;
            }

            Rule rule;
            rule.name = std::string(RulText::Trim(line.substr(0, equals)));
            uint32_t rotation = 0;
            uint32_t flip = 0;
            const bool valid = RulText::ParseNumber(fields[0], rule.firstEdges) &&
                               RulText::ParseNumber(fields[1], rule.secondEdges) &&
                               RulText::ParseNumber(fields[2], rule.textureId) &&
                               RulText::ParseNumber(fields[3], rotation) &&
                               RulText::ParseNumber(fields[4], flip);
            if (!valid) {
                continue;
            }
            rule.rotation = static_cast<uint8_t>(rotation & 0x03u);
            rule.flip = flip != 0;
            current.rules.push_back(std::move(rule));
        }
        flush();

        if (record.sections.empty()) {
            return Fail("RUL1 holds no intersection sections");
        }
        return record;
    }
}
