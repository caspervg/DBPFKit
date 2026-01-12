#include "ExemplarStructures.h"

#include <format>

namespace Exemplar {

    const Property* Record::FindProperty(const uint32_t id) const {
        for (const auto& prop : properties) {
            if (prop.id == id) {
                return &prop;
            }
        }
        return nullptr;
    }

    bool Record::FindProperties(const uint32_t id, std::vector<Property>& result) const {
        auto found = false;
        for (const auto& prop : properties) {
            if (prop.id == id) {
                result.push_back(prop);
                found = true;
            }
        }
        return found;
    }

    std::string Property::ToString() const {
        auto typeLabel = [this]() -> const char* {
            switch (type) {
                case ValueType::UInt8: return "UInt8";
                case ValueType::UInt16: return "UInt16";
                case ValueType::UInt32: return "UInt32";
                case ValueType::SInt32: return "SInt32";
                case ValueType::SInt64: return "SInt64";
                case ValueType::Float32: return "Float32";
                case ValueType::Bool: return "Bool";
                case ValueType::String: return "String";
            }
            return "Unknown";
        };

        auto valueToString = [](const ValueVariant& value) -> std::string {
            return std::visit([]<typename T0>(T0&& v) -> std::string {
                using T = std::decay_t<T0>;
                if constexpr (std::is_same_v<T, std::string>) {
                    return std::string("\"") + v + "\"";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_floating_point_v<T>) {
                    return std::format("{:.3f}", v);
                } else if constexpr (std::is_integral_v<T>) {
                    return std::format("0x{:08X} ({})", static_cast<uint32_t>(v), static_cast<int64_t>(v));
                } else {
                    return std::format("{}", v);
                }
            }, value);
        };

        std::string header = std::format("0x{:08X} [{}] ", id, typeLabel());
        if (values.empty()) {
            return header + "(empty)";
        }

        std::string result = header;
        if (isList || values.size() > 1) {
            result += "[";
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    result += ", ";
                }
                result += valueToString(values[i]);
            }
            result += "]";
        } else {
            result += valueToString(values.front());
        }
        return result;
    }

} // namespace Exemplar

