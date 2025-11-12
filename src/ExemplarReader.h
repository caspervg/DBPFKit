#pragma once

#include <span>

#include "ExemplarStructures.h"
#include "ParseTypes.h"

namespace Exemplar {

    [[nodiscard]] ParseExpected<Record> Parse(std::span<const uint8_t> buffer);

} // namespace Exemplar
