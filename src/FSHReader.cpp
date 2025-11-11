#include "FSHReader.h"

#include <algorithm>

#include "QFSDecompressor.h"

namespace FSH {

bool Reader::Parse(const uint8_t* data, size_t size, File& outFile) {
    if (!data || size < sizeof(FileHeader)) {
        return false;
    }

    std::vector<uint8_t> decompressed;
    const uint8_t* filePtr = data;
    size_t fileSize = size;

    if (QFS::Decompressor::IsQFSCompressed(data, size)) {
        if (!QFS::Decompressor::Decompress(data, size, decompressed)) {
            return false;
        }
        filePtr = decompressed.data();
        fileSize = decompressed.size();
    }

    const uint8_t* ptr = filePtr;
    const uint8_t* end = filePtr + fileSize;

    if (!ReadValue(ptr, end, outFile.header.magic)) return false;
    if (!ReadValue(ptr, end, outFile.header.size)) return false;
    if (!ReadValue(ptr, end, outFile.header.numEntries)) return false;
    if (!ReadValue(ptr, end, outFile.header.dirId)) return false;

    if (!outFile.header.IsValid()) {
        return false;
    }

    std::vector<DirectoryEntry> directory(outFile.header.numEntries);
    for (uint32_t i = 0; i < outFile.header.numEntries; ++i) {
        if (!ReadBytes(ptr, end, directory[i].name, sizeof(directory[i].name))) return false;
        if (!ReadValue(ptr, end, directory[i].offset)) return false;
    }

    outFile.bitmaps.clear();
    outFile.bitmaps.reserve(outFile.header.numEntries);

    for (const auto& entry : directory) {
        if (entry.offset >= fileSize) {
            return false;
        }
        const uint8_t* bitmapPtr = filePtr + entry.offset;
        const uint8_t* bitmapEnd = filePtr + fileSize;
        Bitmap bitmap;
        if (!ParseBitmap(bitmapPtr, bitmapEnd, bitmap)) {
            return false;
        }
        outFile.bitmaps.push_back(std::move(bitmap));
    }

    return true;
}

bool Reader::ParseBitmap(const uint8_t*& ptr, const uint8_t* end, Bitmap& outBitmap) {
    uint32_t code = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t misc[4] = {};

    if (!ReadValue(ptr, end, code)) return false;
    if (!ReadValue(ptr, end, width)) return false;
    if (!ReadValue(ptr, end, height)) return false;
    for (uint16_t& value : misc) {
        if (!ReadValue(ptr, end, value)) return false;
    }

    outBitmap.code = static_cast<uint8_t>(code & 0x7F);
    outBitmap.width = width;
    outBitmap.height = height;

    const size_t expected = outBitmap.ExpectedDataSize();
    if (expected == 0) {
        return false;
    }

    const size_t remaining = static_cast<size_t>(end - ptr);
    const size_t dataSize = std::min(expected, remaining);

    outBitmap.data.resize(dataSize);
    if (!ReadBytes(ptr, end, outBitmap.data.data(), dataSize)) {
        return false;
    }

    return dataSize == expected;
}

bool Reader::ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA) {
    if (bitmap.width == 0 || bitmap.height == 0) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height);
    outRGBA.assign(pixelCount * 4, 0);

    switch (bitmap.code) {
        case kCode32Bit: {
            const uint8_t* src = bitmap.data.data();
            uint8_t* dst = outRGBA.data();
            for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t b = *src++;
                uint8_t g = *src++;
                uint8_t r = *src++;
                uint8_t a = *src++;
                *dst++ = r;
                *dst++ = g;
                *dst++ = b;
                *dst++ = a;
            }
            return true;
        }
        case kCode24Bit: {
            const uint8_t* src = bitmap.data.data();
            uint8_t* dst = outRGBA.data();
            for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t b = *src++;
                uint8_t g = *src++;
                uint8_t r = *src++;
                *dst++ = r;
                *dst++ = g;
                *dst++ = b;
                *dst++ = 255;
            }
            return true;
        }
        case kCode4444: {
            const uint8_t* src = bitmap.data.data();
            uint8_t* dst = outRGBA.data();
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t color;
                std::memcpy(&color, src, sizeof(uint16_t));
                src += sizeof(uint16_t);
                ARGB4444ToRGBA8(color, dst);
                dst += 4;
            }
            return true;
        }
        case kCode0565: {
            const uint8_t* src = bitmap.data.data();
            uint8_t* dst = outRGBA.data();
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t color;
                std::memcpy(&color, src, sizeof(uint16_t));
                src += sizeof(uint16_t);
                RGB565ToRGBA8(color, dst);
                dst += 4;
            }
            return true;
        }
        case kCode1555: {
            const uint8_t* src = bitmap.data.data();
            uint8_t* dst = outRGBA.data();
            for (size_t i = 0; i < pixelCount; ++i) {
                uint16_t color;
                std::memcpy(&color, src, sizeof(uint16_t));
                src += sizeof(uint16_t);
                ARGB1555ToRGBA8(color, dst);
                dst += 4;
            }
            return true;
        }
        default:
            return false;
    }
}

void Reader::ARGB4444ToRGBA8(uint16_t color, uint8_t* rgba) {
    uint8_t a = (color >> 12) & 0xF;
    uint8_t r = (color >> 8) & 0xF;
    uint8_t g = (color >> 4) & 0xF;
    uint8_t b = color & 0xF;
    rgba[0] = static_cast<uint8_t>((r << 4) | r);
    rgba[1] = static_cast<uint8_t>((g << 4) | g);
    rgba[2] = static_cast<uint8_t>((b << 4) | b);
    rgba[3] = static_cast<uint8_t>((a << 4) | a);
}

void Reader::RGB565ToRGBA8(uint16_t color, uint8_t* rgba) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    rgba[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    rgba[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
    rgba[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
    rgba[3] = 255;
}

void Reader::ARGB1555ToRGBA8(uint16_t color, uint8_t* rgba) {
    uint8_t a = (color >> 15) & 0x1;
    uint8_t r = (color >> 10) & 0x1F;
    uint8_t g = (color >> 5) & 0x1F;
    uint8_t b = color & 0x1F;
    rgba[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
    rgba[1] = static_cast<uint8_t>((g << 3) | (g >> 2));
    rgba[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
    rgba[3] = a ? 255 : 0;
}

} // namespace FSH
