#include "ExemplarReader.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <format>
#include <limits>
#include <string_view>

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
        const bool versionOk = sig[4] == '1' || sig[4] == '#'; // There are a few cases where the version is not filled and instead has #
        const bool suffixOk = sig[5] == '#' && sig[6] == '#' && sig[7] == '#';

        info.isValid = (isBinary || info.isText) && versionOk && suffixOk && (info.isCohort || isExemplar);
        info.label.assign(sig, 8);
        return info;
    }

    ParseExpected<Exemplar::Property> ParseBinaryProperty(SpanReader& reader) {
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

    struct TextCursor {
        const char* ptr = nullptr;
        const char* end = nullptr;

        [[nodiscard]] bool AtEnd() const { return ptr >= end; }
        [[nodiscard]] size_t Remaining() const { return static_cast<size_t>(end - ptr); }
        [[nodiscard]] char Peek() const { return AtEnd() ? '\0' : *ptr; }
    };

    void SkipWhitespace(TextCursor& cursor) {
        while (!cursor.AtEnd() && std::isspace(static_cast<unsigned char>(*cursor.ptr))) {
            ++cursor.ptr;
        }
    }

    bool ConsumeLiteralCaseInsensitive(TextCursor& cursor, std::string_view literal) {
        TextCursor probe = cursor;
        for (char expected : literal) {
            if (probe.AtEnd()) {
                return false;
            }
            char actual = *probe.ptr++;
            if (std::tolower(static_cast<unsigned char>(actual)) != std::tolower(static_cast<unsigned char>(expected))) {
                return false;
            }
        }
        cursor = probe;
        return true;
    }

    bool ConsumeChar(TextCursor& cursor, char ch) {
        if (cursor.AtEnd() || cursor.Peek() != ch) {
            return false;
        }
        ++cursor.ptr;
        return true;
    }

    ParseExpected<void> ExpectChar(TextCursor& cursor, char ch, std::string_view context) {
        SkipWhitespace(cursor);
        if (!ConsumeChar(cursor, ch)) {
            return Fail(std::format("Expected '{}' while parsing {}", ch, context));
        }
        return {};
    }

    ParseExpected<void> ExpectLiteral(TextCursor& cursor, std::string_view literal, std::string_view context) {
        SkipWhitespace(cursor);
        if (!ConsumeLiteralCaseInsensitive(cursor, literal)) {
            return Fail(std::format("Expected {} while parsing {}", literal, context));
        }
        return {};
    }

    ParseExpected<std::string> ParseStringLiteral(TextCursor& cursor) {
        SkipWhitespace(cursor);
        if (!ConsumeChar(cursor, '{') || !ConsumeChar(cursor, '"')) {
            return Fail("String literal must start with {\"");
        }

        std::string value;
        value.reserve(32);

        while (!cursor.AtEnd()) {
            char c = *cursor.ptr;
            if (c == '"' && cursor.Remaining() >= 2 && cursor.ptr[1] == '}') {
                cursor.ptr += 2;
                return value;
            }
            value.push_back(c);
            ++cursor.ptr;
        }

        return Fail("Unterminated string literal");
    }

    ParseExpected<std::string> ParseIdentifier(TextCursor& cursor) {
        SkipWhitespace(cursor);
        const char* start = cursor.ptr;
        while (!cursor.AtEnd() && (std::isalpha(static_cast<unsigned char>(cursor.Peek())) || std::isdigit(static_cast<unsigned char>(cursor.Peek())))) {
            ++cursor.ptr;
        }
        if (start == cursor.ptr) {
            return Fail("Expected identifier");
        }
        return std::string(start, static_cast<size_t>(cursor.ptr - start));
    }

    [[nodiscard]] std::string ToLower(std::string_view text) {
        std::string lower;
        lower.reserve(text.size());
        for (char c : text) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return lower;
    }

    ParseExpected<int64_t> ParseIntegerLiteral(TextCursor& cursor,
                                               bool interpretHexAsSigned = false,
                                               int signedBits = 64) {
        SkipWhitespace(cursor);
        if (cursor.AtEnd()) {
            return Fail("Unexpected end of buffer while reading integer literal");
        }

        bool negative = false;
        if (cursor.Peek() == '-') {
            negative = true;
            ++cursor.ptr;
            if (cursor.AtEnd()) {
                return Fail("Dangling '-' in integer literal");
            }
        }

        bool isHex = false;
        const char* literalStart = cursor.ptr;
        if (cursor.Remaining() >= 2 && cursor.ptr[0] == '0' && (cursor.ptr[1] == 'x' || cursor.ptr[1] == 'X')) {
            isHex = true;
            cursor.ptr += 2;
            literalStart = cursor.ptr;
            while (!cursor.AtEnd() && std::isxdigit(static_cast<unsigned char>(cursor.Peek()))) {
                ++cursor.ptr;
            }
            if (literalStart == cursor.ptr) {
                return Fail("Invalid hexadecimal literal");
            }
            uint64_t value = 0;
            auto result = std::from_chars(literalStart, cursor.ptr, value, 16);
            if (result.ec != std::errc{}) {
                return Fail("Failed to parse hexadecimal literal");
            }

            if (interpretHexAsSigned) {
                if (signedBits < 1 || signedBits > 64) {
                    return Fail("Invalid signed bit width");
                }
                int64_t signedValue = 0;
                if (signedBits < 64) {
                    const uint64_t limit = uint64_t{1} << signedBits;
                    if (value >= limit) {
                        return Fail("Hex literal exceeds {}-bit range", signedBits);
                    }
                    const uint64_t signBit = uint64_t{1} << (signedBits - 1);
                    if (value & signBit) {
                        signedValue = static_cast<int64_t>(value) - static_cast<int64_t>(limit);
                    } else {
                        signedValue = static_cast<int64_t>(value);
                    }
                } else {
                    signedValue = static_cast<int64_t>(value);
                }
                if (negative) {
                    signedValue = -signedValue;
                }
                return signedValue;
            }

            if (negative) {
                if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    return Fail("Hex literal out of int64 range");
                }
                return -static_cast<int64_t>(value);
            }
            if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return Fail("Hex literal out of int64 range");
            }
            return static_cast<int64_t>(value);
        }

        while (!cursor.AtEnd() && std::isdigit(static_cast<unsigned char>(cursor.Peek()))) {
            ++cursor.ptr;
        }
        if (literalStart == cursor.ptr) {
            return Fail("Invalid decimal literal");
        }

        auto value = 0LL;
        auto [ptr, ec] = std::from_chars(literalStart, cursor.ptr, value, 10);
        if (ec != std::errc{}) {
            return Fail("Failed to parse decimal literal");
        }
        if (negative) {
            value = -value;
        }
        return static_cast<int64_t>(value);
    }

    ParseExpected<float> ParseFloatLiteral(TextCursor& cursor) {
        SkipWhitespace(cursor);
        if (cursor.AtEnd()) {
            return Fail("Unexpected end of buffer while reading float literal");
        }
        const char* start = cursor.ptr;
        bool consumed = false;
        while (!cursor.AtEnd()) {
            char c = cursor.Peek();
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++cursor.ptr;
                consumed = true;
            } else {
                break;
            }
        }
        if (!consumed) {
            return Fail("Invalid float literal");
        }
        std::string token(start, static_cast<size_t>(cursor.ptr - start));
        char* endPtr = nullptr;
        const float value = std::strtof(token.c_str(), &endPtr);
        if (endPtr == token.c_str() || *endPtr != '\0') {
            return Fail("Failed to parse float literal");
        }
        return value;
    }

    ParseExpected<bool> ParseBoolLiteral(TextCursor& cursor) {
        SkipWhitespace(cursor);
        if (cursor.AtEnd()) {
            return Fail("Unexpected end of buffer while reading bool literal");
        }
        if (std::isalpha(static_cast<unsigned char>(cursor.Peek()))) {
            const char* start = cursor.ptr;
            while (!cursor.AtEnd() && std::isalpha(static_cast<unsigned char>(cursor.Peek()))) {
                ++cursor.ptr;
            }
            std::string word(start, static_cast<size_t>(cursor.ptr - start));
            auto lower = ToLower(word);
            if (lower == "true") {
                return true;
            }
            if (lower == "false") {
                return false;
            }
            return Fail("Unrecognized bool literal");
        }
        auto number = ParseIntegerLiteral(cursor);
        if (!number.has_value()) {
            return std::unexpected(number.error());
        }
        return *number != 0;
    }

    std::optional<Exemplar::ValueType> ParseValueTypeToken(const std::string& token) {
        const auto lower = ToLower(token);
        if (lower == "uint8") return Exemplar::ValueType::UInt8;
        if (lower == "uint16") return Exemplar::ValueType::UInt16;
        if (lower == "uint32") return Exemplar::ValueType::UInt32;
        if (lower == "sint32") return Exemplar::ValueType::SInt32;
        if (lower == "sint64") return Exemplar::ValueType::SInt64;
        if (lower == "float32") return Exemplar::ValueType::Float32;
        if (lower == "bool") return Exemplar::ValueType::Bool;
        if (lower == "string") return Exemplar::ValueType::String;
        return std::nullopt;
    }

    ParseExpected<Exemplar::ValueVariant> ParseValueVariant(TextCursor& cursor, Exemplar::ValueType type) {
        switch (type) {
            case Exemplar::ValueType::UInt8: {
                auto number = ParseIntegerLiteral(cursor);
                if (!number.has_value()) return std::unexpected(number.error());
                if (*number < 0 || *number > std::numeric_limits<uint8_t>::max()) {
                    return Fail("UInt8 value out of range");
                }
                return static_cast<uint8_t>(*number);
            }
            case Exemplar::ValueType::UInt16: {
                auto number = ParseIntegerLiteral(cursor);
                if (!number.has_value()) return std::unexpected(number.error());
                if (*number < 0 || *number > std::numeric_limits<uint16_t>::max()) {
                    return Fail("UInt16 value out of range");
                }
                return static_cast<uint16_t>(*number);
            }
            case Exemplar::ValueType::UInt32: {
                auto number = ParseIntegerLiteral(cursor);
                if (!number.has_value()) return std::unexpected(number.error());
                if (*number < 0 || *number > std::numeric_limits<uint32_t>::max()) {
                    return Fail("UInt32 value out of range");
                }
                return static_cast<uint32_t>(*number);
            }
            case Exemplar::ValueType::SInt32: {
                auto number = ParseIntegerLiteral(cursor, true, 32);
                if (!number.has_value()) return std::unexpected(number.error());
                if (*number < std::numeric_limits<int32_t>::min() || *number > std::numeric_limits<int32_t>::max()) {
                    return Fail("SInt32 value out of range");
                }
                return static_cast<int32_t>(*number);
            }
            case Exemplar::ValueType::SInt64: {
                auto number = ParseIntegerLiteral(cursor, true, 64);
                if (!number.has_value()) return std::unexpected(number.error());
                return static_cast<int64_t>(*number);
            }
            case Exemplar::ValueType::Float32: {
                auto value = ParseFloatLiteral(cursor);
                if (!value.has_value()) return std::unexpected(value.error());
                return *value;
            }
            case Exemplar::ValueType::Bool: {
                auto value = ParseBoolLiteral(cursor);
                if (!value.has_value()) return std::unexpected(value.error());
                return *value;
            }
            case Exemplar::ValueType::String:
                return Fail("String values are handled separately");
        }
        return Fail("Unsupported value type");
    }

    void ConsumeOptionalNameKey(TextCursor& cursor) {
        SkipWhitespace(cursor);
        const char* start = cursor.ptr;
        const char* scan = cursor.ptr;
        while (scan < cursor.end) {
            char c = *scan;
            if (c == ':') {
                if (scan == start) {
                    break;
                }
                cursor.ptr = scan + 1;
                SkipWhitespace(cursor);
                return;
            }
            if (c == ',' || c == '}' || c == '"') {
                break;
            }
            ++scan;
        }
        cursor.ptr = start;
    }

    ParseExpected<std::vector<Exemplar::ValueVariant>> ParseValueArray(TextCursor& cursor, Exemplar::ValueType type) {
        std::vector<Exemplar::ValueVariant> values;
        values.reserve(4);

        if (auto result = ExpectChar(cursor, '{', "property value list"); !result.has_value()) {
            return std::unexpected(result.error());
        }

        while (true) {
            SkipWhitespace(cursor);
            if (cursor.AtEnd()) {
                return Fail("Unexpected end of buffer while reading property list");
            }
            if (cursor.Peek() == '}') {
                ++cursor.ptr;
                break;
            }
            ConsumeOptionalNameKey(cursor);
            SkipWhitespace(cursor);
            auto value = ParseValueVariant(cursor, type);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            values.push_back(std::move(*value));
            SkipWhitespace(cursor);
            if (cursor.AtEnd()) {
                return Fail("Unexpected end of buffer while reading property list");
            }
            if (cursor.Peek() == ',') {
                ++cursor.ptr;
                continue;
            }
            if (cursor.Peek() == '}') {
                ++cursor.ptr;
                break;
            }
            return Fail("Expected ',' or '}' in property list");
        }

        return values;
    }

    ParseExpected<DBPF::Tgi> ParseTextParent(TextCursor& cursor) {
        if (auto result = ExpectLiteral(cursor, "ParentCohort=Key:", "text exemplar parent block"); !result.has_value()) {
            return std::unexpected(result.error());
        }
        if (auto result = ExpectChar(cursor, '{', "parent TGI list"); !result.has_value()) {
            return std::unexpected(result.error());
        }

        std::array<int64_t, 3> parts{};
        for (size_t i = 0; i < parts.size(); ++i) {
            auto value = ParseIntegerLiteral(cursor);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            parts[i] = *value;
            if (i + 1 < parts.size()) {
                if (auto result = ExpectChar(cursor, ',', "parent TGI separator"); !result.has_value()) {
                    return std::unexpected(result.error());
                }
            }
        }
        if (auto result = ExpectChar(cursor, '}', "parent TGI terminator"); !result.has_value()) {
            return std::unexpected(result.error());
        }

        DBPF::Tgi parent{};
        for (int64_t part : parts) {
            if (part < 0 || part > std::numeric_limits<uint32_t>::max()) {
                return Fail("ParentCohort values must be unsigned 32-bit integers");
            }
        }
        parent.group = static_cast<uint32_t>(parts[0]);
        parent.instance = static_cast<uint32_t>(parts[1]);
        parent.type = static_cast<uint32_t>(parts[2]);
        return parent;
    }

    ParseExpected<uint32_t> ParseTextPropertyCount(TextCursor& cursor) {
        if (auto result = ExpectLiteral(cursor, "PropCount=", "property count"); !result.has_value()) {
            return std::unexpected(result.error());
        }
        auto count = ParseIntegerLiteral(cursor);
        if (!count.has_value()) {
            return std::unexpected(count.error());
        }
        if (*count < 0 || *count > std::numeric_limits<uint32_t>::max()) {
            return Fail("PropCount out of range");
        }
        return static_cast<uint32_t>(*count);
    }

    ParseExpected<Exemplar::Property> ParseTextProperty(TextCursor& cursor) {
        auto idValue = ParseIntegerLiteral(cursor);
        if (!idValue.has_value()) {
            return std::unexpected(idValue.error());
        }
        if (*idValue < 0 || *idValue > std::numeric_limits<uint32_t>::max()) {
            return Fail("Property id out of range");
        }
        if (auto result = ExpectChar(cursor, ':', "property descriptor separator"); !result.has_value()) {
            return std::unexpected(result.error());
        }
        auto description = ParseStringLiteral(cursor);
        if (!description.has_value()) {
            return std::unexpected(description.error());
        }
        (void)description;

        if (auto result = ExpectChar(cursor, '=', "property assignment"); !result.has_value()) {
            return std::unexpected(result.error());
        }
        auto typeToken = ParseIdentifier(cursor);
        if (!typeToken.has_value()) {
            return std::unexpected(typeToken.error());
        }
        auto type = ParseValueTypeToken(*typeToken);
        if (!type.has_value()) {
            return Fail("Unsupported property value type in text exemplar");
        }

        Exemplar::Property property{};
        property.id = static_cast<uint32_t>(*idValue);
        property.type = *type;

        if (auto result = ExpectChar(cursor, ':', "property value prefix"); !result.has_value()) {
            return std::unexpected(result.error());
        }

        if (property.type == Exemplar::ValueType::String) {
            auto length = ParseIntegerLiteral(cursor);
            if (!length.has_value()) {
                return std::unexpected(length.error());
            }
            if (*length < 0) {
                return Fail("String length cannot be negative");
            }
            if (auto result = ExpectChar(cursor, ':', "string literal separator"); !result.has_value()) {
                return std::unexpected(result.error());
            }
            auto value = ParseStringLiteral(cursor);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            property.isList = false;
            property.values.emplace_back(std::move(*value));
            return property;
        }

        auto repetitions = ParseIntegerLiteral(cursor);
        if (!repetitions.has_value()) {
            return std::unexpected(repetitions.error());
        }
        if (*repetitions < 0) {
            return Fail("Repetition count cannot be negative");
        }
        if (auto result = ExpectChar(cursor, ':', "property list separator"); !result.has_value()) {
            return std::unexpected(result.error());
        }

        auto values = ParseValueArray(cursor, property.type);
        if (!values.has_value()) {
            return std::unexpected(values.error());
        }
        property.values = std::move(*values);
        const bool isScalar = *repetitions == 0 && property.values.size() == 1;
        property.isList = !isScalar;
        return property;
    }

    ParseExpected<Exemplar::Record> ParseTextExemplar(std::span<const uint8_t> buffer, const SignatureInfo& info) {
        std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        if (text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) {
            text.remove_prefix(3);
        }

        TextCursor cursor{text.data(), text.data() + text.size()};
        SkipWhitespace(cursor);

        const std::string_view expectedHeader = info.isCohort ? "CQZT1###" : "EQZT1###";
        if (!ConsumeLiteralCaseInsensitive(cursor, expectedHeader)) {
            // There are a couple of cases where the version is instead replaced by an extra #
            const std::string_view alternativeHeader = info.isCohort ? "CQZT####" : "EQZT####";
            if (!ConsumeLiteralCaseInsensitive(cursor, alternativeHeader)) {
                return Fail("Text exemplar header mismatch");
            }
        }

        Exemplar::Record record{};
        record.isCohort = info.isCohort;

        SkipWhitespace(cursor);
        auto parent = ParseTextParent(cursor);
        if (!parent.has_value()) {
            return std::unexpected(parent.error());
        }
        record.parent = *parent;

        SkipWhitespace(cursor);
        auto declaredCount = ParseTextPropertyCount(cursor);
        if (!declaredCount.has_value()) {
            return std::unexpected(declaredCount.error());
        }
        const uint32_t expectedCount = *declaredCount;
        record.properties.reserve(expectedCount);

        SkipWhitespace(cursor);
        while (!cursor.AtEnd()) {
            auto property = ParseTextProperty(cursor);
            if (!property.has_value()) {
                return std::unexpected(property.error());
            }
            record.properties.push_back(std::move(*property));
            SkipWhitespace(cursor);
        }

        record.isText = true;
        return record;
    }

    ParseExpected<Exemplar::Record> ParseBinaryExemplar(std::span<const uint8_t> buffer, const SignatureInfo& info) {
        Exemplar::Record record{};
        record.isCohort = info.isCohort;

        SpanReader reader{buffer.data() + 8, buffer.data() + buffer.size()};

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
            const auto propertyExpected = ParseBinaryProperty(reader);
            if (propertyExpected.has_value()) {
                record.properties.push_back(std::move(propertyExpected.value()));
            } else {
                return Fail(std::format("Failed to parse property {}: {}", i, propertyExpected.error().message));
            }
        }

        record.isText = false;
        return record;
    }

} // namespace

namespace Exemplar {

    ParseExpected<Record> Parse(const std::span<const uint8_t> buffer) {
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
            return Fail(("Invalid exemplar signature: " + info.label));
        }

        if (info.isText) {
            return ParseTextExemplar(buffer, info);
        }

        return ParseBinaryExemplar(buffer, info);
    }

} // namespace Exemplar
