#include "LTextReader.h"

#include <cstring>
#include <string_view>

namespace {

    constexpr uint16_t kControlChar = 0x1000;

    std::string EncodeUtf8(std::u16string_view text) {
        std::string out;
        out.reserve(text.size());
        size_t i = 0;
        while (i < text.size()) {
            const char16_t unit = text[i++];
            uint32_t codePoint = unit;

            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (i >= text.size()) {
                    codePoint = 0xFFFD; // unmatched lead surrogate
                } else {
                    const char16_t trail = text[i];
                    if (trail < 0xDC00 || trail > 0xDFFF) {
                        codePoint = 0xFFFD;
                    } else {
                        ++i;
                        codePoint = 0x10000 + (((unit - 0xD800) << 10) | (trail - 0xDC00));
                    }
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                codePoint = 0xFFFD; // stray trail surrogate
            }

            if (codePoint <= 0x7F) {
                out.push_back(static_cast<char>(codePoint));
            } else if (codePoint <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else if (codePoint <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }
        return out;
    }

    std::u16string DecodeUtf8Lossy(std::string_view text) {
        std::u16string out;
        size_t i = 0;
        while (i < text.size()) {
            uint32_t codePoint = 0xFFFD;
            unsigned char byte = static_cast<unsigned char>(text[i++]);
            if (byte < 0x80) {
                codePoint = byte;
            } else if ((byte >> 5) == 0x6 && i < text.size()) {
                unsigned char b1 = static_cast<unsigned char>(text[i++]);
                codePoint = ((byte & 0x1F) << 6) | (b1 & 0x3F);
            } else if ((byte >> 4) == 0xE && i + 1 < text.size()) {
                unsigned char b1 = static_cast<unsigned char>(text[i++]);
                unsigned char b2 = static_cast<unsigned char>(text[i++]);
                codePoint = ((byte & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            } else if ((byte >> 3) == 0x1E && i + 2 < text.size()) {
                unsigned char b1 = static_cast<unsigned char>(text[i++]);
                unsigned char b2 = static_cast<unsigned char>(text[i++]);
                unsigned char b3 = static_cast<unsigned char>(text[i++]);
                codePoint = ((byte & 0x07) << 18) | ((b1 & 0x3F) << 12) |
                            ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            }

            if (codePoint <= 0xFFFF) {
                out.push_back(static_cast<char16_t>(codePoint));
            } else {
                codePoint -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 | ((codePoint >> 10) & 0x3FF)));
                out.push_back(static_cast<char16_t>(0xDC00 | (codePoint & 0x3FF)));
            }
        }
        return out;
    }

    ParseExpected<LText::Record> ParseFallback(std::span<const uint8_t> buffer) {
        std::string_view raw(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        const auto nul = raw.find('\0');
        if (nul != std::string_view::npos) {
            raw = raw.substr(0, nul);
        }
        if (raw.empty()) {
            return Fail("LText fallback payload is empty");
        }
        LText::Record record;
        record.text = DecodeUtf8Lossy(raw);
        return record;
    }

} // namespace

namespace LText {

    std::string Record::ToUtf8() const {
        return EncodeUtf8(text);
    }

    ParseExpected<Record> Parse(std::span<const uint8_t> buffer) {
        if (buffer.empty()) {
            return Fail("LText payload is empty");
        }
        if (buffer.size() < 4) {
            // Maybe there is still some data in there that is just raw ASCII instead of proper LText
            return ParseFallback(buffer);
        }

        const uint8_t* data = buffer.data();
        uint16_t charCount = 0;
        std::memcpy(&charCount, data, sizeof(uint16_t));
        uint16_t control = 0;
        std::memcpy(&control, data + 2, sizeof(uint16_t));

        const size_t payloadBytes = buffer.size() - 4;
        const size_t expectedBytes = static_cast<size_t>(charCount) * 2;
        const bool hasControl = control == kControlChar;
        const bool lengthMatches = payloadBytes == expectedBytes && (payloadBytes % 2 == 0);

        if (!hasControl || !lengthMatches) {
            auto fallback = ParseFallback(buffer);
            if (fallback.has_value()) {
                return fallback;
            }
            return Fail("Invalid LText header and fallback failed: {}", fallback.error().message);
        }

        Record record;
        record.text.resize(charCount);
        if (charCount > 0) {
            std::memcpy(record.text.data(), data + 4, payloadBytes);
        }
        return record;
    }

} // namespace LText
