#pragma once

#include <cstring>
#include <span>
#include <vector>

#include "FSHStructures.h"

namespace FSH {

class Reader {
public:
    static bool Parse(std::span<const uint8_t> buffer, File& outFile);
    static bool ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA);

private:
    template<typename T>
    static bool ReadValue(const uint8_t*& ptr, const uint8_t* end, T& out) {
        if (ptr + sizeof(T) > end) {
            return false;
        }
        T value = 0;
        std::memcpy(&value, ptr, sizeof(T));
        out = value;
        ptr += sizeof(T);
        return true;
    }

    static bool ReadBytes(const uint8_t*& ptr, const uint8_t* end, void* dest, size_t length) {
        if (ptr + length > end) {
            return false;
        }
        std::memcpy(dest, ptr, length);
        ptr += length;
        return true;
    }

    static void ARGB4444ToRGBA8(uint16_t color, uint8_t* rgba);
    static void RGB565ToRGBA8(uint16_t color, uint8_t* rgba);
    static void ARGB1555ToRGBA8(uint16_t color, uint8_t* rgba);
};

} // namespace FSH
