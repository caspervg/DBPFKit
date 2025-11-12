#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "DBPFStructures.h"

namespace Exemplar {

    enum class ValueType : uint16_t {
        UInt8 = 0x0100,
        UInt16 = 0x0200,
        UInt32 = 0x0300,
        SInt32 = 0x0700,
        SInt64 = 0x0800,
        Float32 = 0x0900,
        Bool = 0x0B00,
        String = 0x0C00,
    };

    using ValueVariant = std::variant<int32_t, uint32_t, int64_t, float, bool, uint8_t, uint16_t, std::string>;

    struct Property {
        uint32_t id = 0;
        ValueType type = ValueType::UInt32;
        bool isList = false;
        std::vector<ValueVariant> values;

        [[nodiscard]] bool IsString() const { return type == ValueType::String; }
        [[nodiscard]] bool IsNumericList() const { return isList && type != ValueType::String; }
        [[nodiscard]] std::string ToString() const;
    };

    struct Record {
        DBPF::Tgi parent{};
        bool isCohort = false;
        std::vector<Property> properties;

        [[nodiscard]] const Property* FindProperty(uint32_t id) const;

        template<typename T>
        std::optional<T> GetScalar(uint32_t id) const {
            const Property* prop = FindProperty(id);
            if (!prop || prop->values.empty() || prop->isList) {
                return std::nullopt;
            }
            if (auto value = std::get_if<T>(&prop->values.front())) {
                return *value;
            }
            return std::nullopt;
        }
    };

} // namespace Exemplar
