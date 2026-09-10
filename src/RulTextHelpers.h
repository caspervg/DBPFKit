#pragma once

#include <charconv>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Small shared helpers for the line-oriented RUL files: the Individual Network
// RULs, RUL1 and RUL2. RUL0 is INI shaped and keeps its own helpers.
namespace RulText {

    constexpr char kCommentPrefix = ';';
    constexpr char kFieldDelimiter = ',';

    [[nodiscard]] inline std::string_view Trim(std::string_view text) {
        const auto isSpace = [](const char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        };
        while (!text.empty() && isSpace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && isSpace(text.back())) {
            text.remove_suffix(1);
        }
        return text;
    }

    // RUL files write plain numbers in decimal and resource identifiers with an
    // 0x prefix. A leading zero is not octal here: edge flags are written two
    // digits wide, so "02" has to stay two.
    [[nodiscard]] inline bool ParseNumber(std::string_view text, uint32_t& out) {
        text = Trim(text);
        if (text.empty()) {
            return false;
        }
        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
        }
        uint32_t value = 0;
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            return false;
        }
        out = value;
        return true;
    }

    [[nodiscard]] inline std::vector<std::string_view> Split(const std::string_view text,
                                                             const char separator) {
        std::vector<std::string_view> parts;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t next = text.find(separator, start);
            if (next == std::string_view::npos) {
                parts.push_back(text.substr(start));
                break;
            }
            parts.push_back(text.substr(start, next - start));
            start = next + 1;
        }
        return parts;
    }

    // Splits the buffer into trimmed lines. RUL files are latin-1 with CRLF
    // endings, so the carriage return is trimmed away with the whitespace.
    [[nodiscard]] inline std::vector<std::string_view> Lines(const std::span<const uint8_t> buffer) {
        const std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        std::vector<std::string_view> lines;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t next = text.find('\n', start);
            if (next == std::string_view::npos) {
                lines.push_back(Trim(text.substr(start)));
                break;
            }
            lines.push_back(Trim(text.substr(start, next - start)));
            start = next + 1;
        }
        return lines;
    }

    [[nodiscard]] inline bool IsSkippable(const std::string_view line) {
        return line.empty() || line.front() == kCommentPrefix;
    }
}
