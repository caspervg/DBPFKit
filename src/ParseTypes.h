#pragma once

#include <expected>
#include <format>
#include <string>

struct ParseError {
    std::string message;
};

template<typename T>
using ParseExpected = std::expected<T, ParseError>;

inline ParseError MakeParseError(std::string message) {
    return ParseError{std::move(message)};
}

inline std::unexpected<ParseError> Fail(const std::string& message) {
    return std::unexpected(MakeParseError(message));
}

template<typename... Args>
 std::unexpected<ParseError> Fail(std::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(MakeParseError(std::format(fmt, std::forward<Args>(args)...)));
}