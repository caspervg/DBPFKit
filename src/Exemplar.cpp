#include "Exemplar.h"

#include <cstring>
#include <format>

namespace {

    constexpr size_t kHeaderSize = 24;

    struct SpanReader {
        const uint8_t* ptr = nullptr;
        const uint8_t* end = nullptr;

        [[nodiscard]] bool CanRead(size_t bytes) const { return ptr + bytes <= end; }

        template<typename T>
        bool ReadLE(T& out) {
            if (!CanRead(sizeof(T))) {
                return false;
            }
            T value = 0;
            std::memcpy(&value, ptr, sizeof(T));
            out = value;
            ptr += sizeof(T);
            return true;
        }

        bool ReadBytes(size_t length, std::string& out) {
            if (!CanRead(length)) {
                return false;
            }
            out.assign(reinterpret_cast<const char*>(ptr), length);
            ptr += length;
            return true;
        }
    };

    std::optional<Exemplar::ValueType> ToValueType(uint16_t raw) {
        using Exemplar::ValueType;
        switch (raw) {
            case 0x0100: return ValueType::UInt8;
            case 0x0200: return ValueType::UInt16;
            case 0x0300: return ValueType::UInt32;
            case 0x0700: return ValueType::SInt32;
            case 0x0800: return ValueType::SInt64;
            case 0x0900: return ValueType::Float32;
            case 0x0B00: return ValueType::Bool;
            case 0x0C00: return ValueType::String;
            default: return std::nullopt;
        }
    }

    bool ReadValue(SpanReader& reader, Exemplar::ValueType type, Exemplar::ValueVariant& out) {
        switch (type) {
            case Exemplar::ValueType::UInt8: {
                uint8_t value = 0;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::UInt16: {
                uint16_t value = 0;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::UInt32: {
                uint32_t value = 0;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::SInt32: {
                int32_t value = 0;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::SInt64: {
                int64_t value = 0;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::Float32: {
                float value = 0.0f;
                if (!reader.ReadLE(value)) return false;
                out = value;
                return true;
            }
            case Exemplar::ValueType::Bool: {
                uint8_t raw = 0;
                if (!reader.ReadLE(raw)) return false;
                out = static_cast<bool>(raw != 0);
                return true;
            }
            case Exemplar::ValueType::String: {
                return false;
            }
        }
        return false;
    }

    bool ReadStringValue(SpanReader& reader, size_t length, Exemplar::ValueVariant& out) {
        std::string str;
        if (!reader.ReadBytes(length, str)) {
            return false;
        }
        out = std::move(str);
        return true;
    }

    std::string MakeError(const std::string& message) {
        return message;
    }

    struct SignatureInfo {
        bool valid = false;
        bool isCohort = false;
        bool isText = false;
        std::string label;
    };

    SignatureInfo ParseSignature(const uint8_t* data, size_t size) {
        SignatureInfo info;
        if (size < 8) {
            info.label = "buffer_too_small";
            return info;
        }
        const char* sig = reinterpret_cast<const char*>(data);
        info.isCohort = sig[0] == 'C';
        bool isExemplar = sig[0] == 'E';
        info.isText = sig[3] == 'T';
        bool isBinary = sig[3] == 'B';
        bool versionOk = sig[4] == '1';
        bool suffixOk = sig[5] == '#' && sig[6] == '#' && sig[7] == '#';

        info.valid = (isBinary || info.isText) && versionOk && suffixOk && (info.isCohort || isExemplar);
        info.label.assign(sig, 8);
        return info;
    }

    bool ParseProperty(SpanReader& reader, Exemplar::Property& property, std::string& error) {
        if (!reader.CanRead(8)) {
            error = MakeError("unexpected end of buffer while reading property header");
            return false;
        }

        reader.ReadLE(property.id);
        uint16_t rawValueType = 0;
        reader.ReadLE(rawValueType);
        auto type = ToValueType(rawValueType);
        if (!type) {
            error = MakeError("unsupported property value type");
            return false;
        }
        property.type = *type;

        uint16_t keyType = 0;
        reader.ReadLE(keyType);

        if (keyType == 0x0000) {
            if (!reader.CanRead(1)) {
                error = MakeError("unexpected end of buffer while reading single-value repetition byte");
                return false;
            }
            uint8_t lengthOrFlag = *reader.ptr++;

            Exemplar::ValueVariant value;
            if (property.type == Exemplar::ValueType::String) {
                if (!ReadStringValue(reader, lengthOrFlag, value)) {
                    error = MakeError("failed to read string value");
                    return false;
                }
            } else {
                if (!ReadValue(reader, property.type, value)) {
                    error = MakeError("failed to read property value");
                    return false;
                }
            }
            property.isList = false;
            property.values.push_back(std::move(value));
            return true;
        }

        if (keyType == 0x0080) {
            if (!reader.CanRead(5)) {
                error = MakeError("unexpected end of buffer while reading multi-value header");
                return false;
            }

            reader.ptr++; // skip unused flag
            uint32_t repetitions = 0;
            reader.ReadLE(repetitions);

            if (property.type == Exemplar::ValueType::String) {
                Exemplar::ValueVariant value;
                if (!ReadStringValue(reader, repetitions, value)) {
                    error = MakeError("failed to read multi-string payload");
                    return false;
                }
                property.values.push_back(std::move(value));
                property.isList = false;
                return true;
            }

            property.isList = true;
            property.values.reserve(repetitions);
            for (uint32_t i = 0; i < repetitions; ++i) {
                Exemplar::ValueVariant value;
                if (!ReadValue(reader, property.type, value)) {
                    error = MakeError("failed to read list value");
                    return false;
                }
                property.values.push_back(std::move(value));
            }
            return true;
        }

        error = MakeError("unsupported key type");
        return false;
    }

} // namespace

namespace Exemplar {

    const Property* Record::FindProperty(uint32_t id) const {
        for (const auto& prop : properties) {
            if (prop.id == id) {
                return &prop;
            }
        }
        return nullptr;
    }

    ParseResult Parse(const uint8_t* data, size_t size) {
        ParseResult result{};
        if (!data || size < kHeaderSize) {
            result.errorMessage = "buffer too small";
            return result;
        }

        SignatureInfo info = ParseSignature(data, size);
        if (!info.valid) {
            result.errorMessage = "invalid exemplar signature";
            return result;
        }

        if (info.isText) {
            result.errorMessage = "text exemplars are not supported yet";
            return result;
        }

        Record record{};
        record.isCohort = info.isCohort;

        SpanReader reader{data + 8, data + size};

        reader.ReadLE(record.parent.type);
        reader.ReadLE(record.parent.group);
        reader.ReadLE(record.parent.instance);

        uint32_t propertyCount = 0;
        reader.ReadLE(propertyCount);
        record.properties.reserve(propertyCount);

        for (uint32_t i = 0; i < propertyCount; ++i) {
            Property property;
            if (!ParseProperty(reader, property, result.errorMessage)) {
                return result;
            }
            record.properties.push_back(std::move(property));
        }

        result.record = std::move(record);
        result.success = true;
        result.errorMessage.clear();
        return result;
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
