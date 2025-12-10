#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ParseTypes.h"

namespace LText {

    struct Record {
        std::u16string text;

        [[nodiscard]] std::string ToUtf8() const;
        [[nodiscard]] std::u16string_view View() const { return text; }
    };

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);

} // namespace LText
