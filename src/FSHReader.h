#pragma once

#include <cstring>
#include <span>
#include <vector>

#include "FSHStructures.h"
#include "ParseTypes.h"

namespace FSH {

class Reader {
public:
    static ParseExpected<Record> Parse(std::span<const uint8_t> buffer);
    static bool ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA);

private:
    static void ARGB4444ToRGBA8(uint16_t color, uint8_t* rgba);
    static void RGB565ToRGBA8(uint16_t color, uint8_t* rgba);
    static void ARGB1555ToRGBA8(uint16_t color, uint8_t* rgba);
};

} // namespace FSH
