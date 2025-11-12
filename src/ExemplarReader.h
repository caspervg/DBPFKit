#pragma once

#include <span>

#include "ExemplarStructures.h"

namespace Exemplar {

    [[nodiscard]] ParseResult Parse(std::span<const uint8_t> buffer);

} // namespace Exemplar

