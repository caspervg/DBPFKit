#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace FSH {

constexpr uint32_t kMagicSHPI = 0x49504853; // 'SHPI'
constexpr uint32_t kMagicG264 = 0x34363247; // 'G264'
constexpr uint32_t kMagicG266 = 0x36363247; // 'G266'
constexpr uint32_t kMagicG354 = 0x34353347; // 'G354'

constexpr uint8_t kCodeDXT1 = 0x60;
constexpr uint8_t kCodeDXT3 = 0x61;
constexpr uint8_t kCode32Bit = 0x7D;
constexpr uint8_t kCode24Bit = 0x7F;
constexpr uint8_t kCode4444 = 0x6D;
constexpr uint8_t kCode0565 = 0x78;
constexpr uint8_t kCode1555 = 0x7E;

struct DirectoryEntry {
    char name[4];
    uint32_t offset;
};

struct FileHeader {
    uint32_t magic = 0;
    uint32_t size = 0;
    uint32_t numEntries = 0;
    uint32_t dirId = 0;

    [[nodiscard]] bool IsValid() const {
        return magic == kMagicSHPI || magic == kMagicG264 ||
               magic == kMagicG266 || magic == kMagicG354;
    }
};

struct Bitmap {
    uint8_t code = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    std::vector<uint8_t> data;

    [[nodiscard]] bool IsDXT() const { return code == kCodeDXT1 || code == kCodeDXT3; }

    [[nodiscard]] size_t BytesPerPixel() const {
        switch (code) {
            case kCode32Bit: return 4;
            case kCode24Bit: return 3;
            case kCode4444:
            case kCode0565:
            case kCode1555: return 2;
            default: return 0;
        }
    }

    [[nodiscard]] size_t ExpectedDataSize() const {
        if (code == kCodeDXT1) {
            return ((width + 3) / 4) * ((height + 3) / 4) * 8;
        }
        if (code == kCodeDXT3) {
            return ((width + 3) / 4) * ((height + 3) / 4) * 16;
        }
        return static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixel();
    }
};

struct File {
    FileHeader header;
    std::vector<Bitmap> bitmaps;
};

} // namespace FSH
