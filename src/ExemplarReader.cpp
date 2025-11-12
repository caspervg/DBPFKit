#include "ExemplarReader.h"

#include <cstring>

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

    struct SignatureInfo {
        bool isValid = false;
        bool isCohort = false;
        bool isText = false;
        std::string label;
    };

    ParseExpected<SignatureInfo> ParseSignature(const uint8_t* data, size_t size) {
        SignatureInfo info;

        if (size < 8) {
            return Fail("Buffer too small for exemplar signature");
        }

        const auto sig = reinterpret_cast<const char*>(data);
        info.isCohort = sig[0] == 'C';
        const bool isExemplar = sig[0] == 'E';
        info.isText = sig[3] == 'T';
        const bool isBinary = sig[3] == 'B';
        const bool versionOk = sig[4] == '1';
        const bool suffixOk = sig[5] == '#' && sig[6] == '#' && sig[7] == '#';

        info.isValid = (isBinary || info.isText) && versionOk && suffixOk && (info.isCohort || isExemplar);
        info.label.assign(sig, 8);
        return info;
    }

    ParseExpected<Exemplar::Property> ParseProperty(SpanReader& reader) {
        if (!reader.CanRead(8)) {
            return Fail("Unexpected end of buffer while reading property header");
        }

        Exemplar::Property property{};

        reader.ReadLE(property.id);
        uint16_t rawValueType = 0;
        reader.ReadLE(rawValueType);
        auto type = ToValueType(rawValueType);
        if (!type) {
            return Fail("Unsupported property value type");
        }
        property.type = *type;

        uint16_t keyType = 0;
        reader.ReadLE(keyType);

        if (keyType == 0x0000) {
            if (!reader.CanRead(1)) {
                return Fail("Unexpected end of buffer while reading single-value repetition byte");
            }
            uint8_t lengthOrFlag = *reader.ptr++;

            Exemplar::ValueVariant value;
            if (property.type == Exemplar::ValueType::String) {
                if (!ReadStringValue(reader, lengthOrFlag, value)) {
                    return Fail("Failed to read string value");
                }
            } else {
                if (!ReadValue(reader, property.type, value)) {
                    return Fail("Failed to read property value");
                }
            }
            property.isList = false;
            property.values.push_back(std::move(value));
            return property;
        }

        if (keyType == 0x0080) {
            if (!reader.CanRead(5)) {
                return Fail("Unexpected end of buffer while reading multi-value header");
            }

            reader.ptr++; // skip unused flag
            uint32_t repetitions = 0;
            reader.ReadLE(repetitions);

            if (property.type == Exemplar::ValueType::String) {
                Exemplar::ValueVariant value;
                if (!ReadStringValue(reader, repetitions, value)) {
                    return Fail("Failed to read multi-string payload");
                }
                property.isList = false;
                property.values.push_back(std::move(value));
                return property;
            }

            property.isList = true;
            property.values.reserve(repetitions);
            for (uint32_t i = 0; i < repetitions; ++i) {
                Exemplar::ValueVariant value;
                if (!ReadValue(reader, property.type, value)) {
                    return Fail("Failed to read list value");
                }
                property.values.push_back(std::move(value));
            }
            return property;
        }

        if (keyType == 0x0081) {
            if (!reader.CanRead(9)) {
                return Fail("Unexpected end of buffer while reading string-array header");
            }

            reader.ptr++; // skip unused flag
            uint32_t totalLength = 0;
            reader.ReadLE(totalLength);
            uint32_t entryCount = 0;
            reader.ReadLE(entryCount);

            property.isList = true;
            property.values.reserve(entryCount);

            if (!reader.CanRead(totalLength)) {
                return Fail("Unexpected end of buffer while reading string-array payload");
            }

            const uint8_t* offsetPtr = reader.ptr;
            const uint8_t* stringPtr = reader.ptr + entryCount * 4;
            if (stringPtr > reader.end || stringPtr + totalLength > reader.end) {
                return Fail("String-array payload exceeds buffer bounds");
            }

            for (uint32_t i = 0; i < entryCount; ++i) {
                uint32_t length = 0;
                std::memcpy(&length, offsetPtr + i * 4, sizeof(uint32_t));
                Exemplar::ValueVariant value;
                SpanReader stringReader{stringPtr, stringPtr + length};
                if (!ReadStringValue(stringReader, length, value)) {
                    return Fail("Failed to read string-array entry");
                }
                property.values.push_back(std::move(value));
                stringPtr += length;
            }

            reader.ptr += totalLength;
            return property;
        }

        return Fail(std::format("Unsupported property key type: {}", keyType));
    }

} // namespace

namespace Exemplar {

    ParseExpected<Record> Parse(std::span<const uint8_t> buffer) {
        if (buffer.size() < kHeaderSize) {
            return Fail("Buffer too small");
        }

        const uint8_t* data = buffer.data();
        const size_t size = buffer.size();

        auto infoExpected = ParseSignature(data, size);
        if (!infoExpected.has_value()) {
            return Fail(std::format("Invalid exemplar signature: {}", infoExpected.error().message));
        }
        const auto& info = infoExpected.value();

        if (!info.isValid) {
            return Fail(("Invalid exemplar signature"));
        }

        if (info.isText) {
            return Fail(("Text exemplars are not supported yet"));
        }

        Record record{};
        record.isCohort = info.isCohort;

        SpanReader reader{data + 8, data + size};

        if (!reader.ReadLE(record.parent.type) ||
            !reader.ReadLE(record.parent.group) ||
            !reader.ReadLE(record.parent.instance)) {
            return Fail(("Failed to read exemplar parent"));
        }

        uint32_t propertyCount = 0;
        if (!reader.ReadLE(propertyCount)) {
            return Fail(("Failed to read property count"));
        }
        record.properties.reserve(propertyCount);

        for (uint32_t i = 0; i < propertyCount; ++i) {
            const auto propertyExpected = ParseProperty(reader);
            if (propertyExpected.has_value()) {
                record.properties.push_back(std::move(propertyExpected.value()));
            } else {
                return Fail(std::format("Failed to parse property {}: {}", i, propertyExpected.error().message));
            }
        }

        return record;
    }

} // namespace Exemplar
