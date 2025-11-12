#pragma once
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace DBPF {

    struct Tgi {
        uint32_t type = 0;
        uint32_t group = 0;
        uint32_t instance = 0;

        auto operator<=>(const Tgi&) const = default;

        [[nodiscard]] bool operator==(const Tgi& other) const {
            return type == other.type && group == other.group && instance == other.instance;
        }

        [[nodiscard]] bool operator!=(const Tgi& other) const {
            return !(*this == other);
        }

        [[nodiscard]] std::string ToString() const {
            return std::format("TGI(0x{0:08x}, 0x{1:08x}, 0x{2:08x})", type, group, instance);
        }
    };

    struct TgiHash {
        size_t operator()(const Tgi& t) const {
            // Basic mixing from Boost's old hash_combine recipe
            auto h = std::hash<uint32_t>{}(t.type);
            h ^= std::hash<uint32_t>{}(t.group) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(t.instance) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct TgiMask {
        std::optional<uint32_t> type, group, instance;

        [[nodiscard]] bool Matches(const Tgi& t) const {
            const auto typeMatches = type.has_value() ? type.value() == t.type : true;
            const auto groupMatches = group.has_value() ? group.value() == t.group : true;
            const auto instanceMatches = instance.has_value() ? instance.value() == t.instance : true;
            return typeMatches && groupMatches && instanceMatches;
        }
    };

    struct TgiLabel {
        TgiMask mask;
        std::string_view label{};
    };

    std::string_view Describe(const Tgi& tgi);
    std::optional<TgiMask> MaskForLabel(std::string_view label);
}
