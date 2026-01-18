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

        // Get a value with automatic type conversion for numeric types
        // Supports conversion between all integer types (uint8, uint16, uint32, int32, int64)
        // Returns nullopt if index is out of bounds or type conversion is not supported
        template <typename T>
        [[nodiscard]] std::optional<T> GetScalarAs(size_t index = 0) const {
            if (index >= values.size()) {
                return std::nullopt;
            }

            return std::visit([]<typename U>(U&& val) -> std::optional<T> {
                using V = std::decay_t<U>;

                // Handle string type
                if constexpr (std::is_same_v<V, std::string>) {
                    if constexpr (std::is_same_v<T, std::string>) {
                        return val;
                    }
                    return std::nullopt;
                }
                // Handle bool type
                else if constexpr (std::is_same_v<V, bool>) {
                    if constexpr (std::is_same_v<T, bool>) {
                        return val;
                    }
                    return std::nullopt;
                }
                // Handle float type
                else if constexpr (std::is_same_v<V, float>) {
                    if constexpr (std::is_same_v<T, float>) {
                        return val;
                    }
                    return std::nullopt;
                }
                // Handle numeric conversions (all integer types)
                else if constexpr (std::is_integral_v<V> && std::is_integral_v<T>) {
                    return static_cast<T>(val);
                }
                else {
                    return std::nullopt;
                }
            }, values[index]);
        }
    };

    struct Record {
        DBPF::Tgi parent{};
        bool isCohort = false;
        bool isText = false;
        std::vector<Property> properties;

        [[nodiscard]] const Property* FindProperty(uint32_t id) const;
        [[nodiscard]] bool FindProperties(uint32_t id, std::vector<Property>& result) const;

        template <typename T>
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
