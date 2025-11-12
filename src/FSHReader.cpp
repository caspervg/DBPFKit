#include "FSHReader.h"

#include <algorithm>
#include <string>
#include <squish/squish.h>

#include "QFSDecompressor.h"

namespace {

bool ReadUInt24(const uint8_t*& ptr, const uint8_t* end, uint32_t& out) {
    if (ptr + 3 > end) {
        return false;
    }
    out = (static_cast<uint32_t>(ptr[0]) << 16) |
          (static_cast<uint32_t>(ptr[1]) << 8) |
          static_cast<uint32_t>(ptr[2]);
    ptr += 3;
    return true;
}

std::string MakeName(const char name[4]) {
    std::string s(name, name + 4);
    auto nullPos = s.find('\0');
    if (nullPos != std::string::npos) {
        s.resize(nullPos);
    }
    return s;
}

} // namespace

namespace FSH {

ParseExpected<File> Reader::Parse(std::span<const uint8_t> buffer) {
    if (buffer.size() < sizeof(FileHeader)) {
        return Fail("Buffer too small for FSH header");
    }

    std::vector<uint8_t> decompressed;
    std::span<const uint8_t> fileSpan = buffer;

    if (QFS::Decompressor::IsQFSCompressed(buffer)) {
        auto result = QFS::Decompressor::Decompress(buffer, decompressed);
        if (!result.has_value()) {
            return Fail("Failed to decompress FSH payload: {}", result.error().message);
        }
        fileSpan = std::span<const uint8_t>(decompressed.data(), decompressed.size());
    }

    const uint8_t* filePtr = fileSpan.data();
    const size_t fileSize = fileSpan.size();

    const uint8_t* ptr = filePtr;
    const uint8_t* end = filePtr + fileSize;

    File outFile;
    if (!ReadValue(ptr, end, outFile.header.magic) ||
        !ReadValue(ptr, end, outFile.header.size) ||
        !ReadValue(ptr, end, outFile.header.numEntries) ||
        !ReadValue(ptr, end, outFile.header.dirId)) {
        return Fail("Failed to read FSH header");
    }

    if (!outFile.header.IsValid()) {
        return Fail("Invalid FSH header");
    }

    struct DirEntryParsed {
        std::string name;
        uint32_t offset{};
    };

    std::vector<DirEntryParsed> directory(outFile.header.numEntries);
    for (uint32_t i = 0; i < outFile.header.numEntries; ++i) {
        DirectoryEntry dir{};
        if (!ReadBytes(ptr, end, dir.name, sizeof(dir.name)) ||
            !ReadValue(ptr, end, dir.offset)) {
            return Fail("Failed to read FSH directory entry");
        }
        directory[i].name = MakeName(dir.name);
        directory[i].offset = dir.offset;
    }

    outFile.entries.clear();
    outFile.entries.reserve(outFile.header.numEntries);

    for (uint32_t i = 0; i < outFile.header.numEntries; ++i) {
        const uint32_t offset = directory[i].offset;
        const uint32_t nextOffset = (i + 1 < directory.size())
                                        ? directory[i + 1].offset
                                        : static_cast<uint32_t>(fileSize);
        if (offset >= fileSize || offset >= nextOffset) {
            return Fail("Invalid FSH directory offsets");
        }

        const uint8_t* entryPtr = filePtr + offset;
        const uint8_t* entryEnd = filePtr + nextOffset;

        Entry entry{};
        entry.name = directory[i].name;

        uint8_t record = 0;
        uint32_t blockSize = 0;
        if (!ReadBytes(entryPtr, entryEnd, &record, 1) ||
            !ReadUInt24(entryPtr, entryEnd, blockSize)) {
            return Fail("Failed to read FSH record header");
        }

        uint16_t width = 0, height = 0;
        uint16_t xCenter = 0, yCenter = 0;
        uint16_t xOffset = 0, yOffset = 0;
        if (!ReadValue(entryPtr, entryEnd, width) ||
            !ReadValue(entryPtr, entryEnd, height) ||
            !ReadValue(entryPtr, entryEnd, xCenter) ||
            !ReadValue(entryPtr, entryEnd, yCenter) ||
            !ReadValue(entryPtr, entryEnd, xOffset) ||
            !ReadValue(entryPtr, entryEnd, yOffset)) {
            return Fail("Failed to read FSH record dimensions");
        }

        entry.formatCode = record & 0x7F;
        entry.width = width;
        entry.height = height;
        entry.mipCount = static_cast<uint8_t>((yOffset >> 12) & 0x0F);

        for (uint8_t mip = 0; mip <= entry.mipCount; ++mip) {
            uint16_t mipWidth = static_cast<uint16_t>(std::max<int>(1, width >> mip));
            uint16_t mipHeight = static_cast<uint16_t>(std::max<int>(1, height >> mip));
            if ((entry.formatCode == kCodeDXT1 || entry.formatCode == kCodeDXT3) &&
                (mipWidth % 4 != 0 || mipHeight % 4 != 0)) {
                break;
            }
            Bitmap bitmap;
            bitmap.code = entry.formatCode;
            bitmap.width = mipWidth;
            bitmap.height = mipHeight;
            bitmap.mipLevel = mip;
            const size_t dataSize = bitmap.ExpectedDataSize();
            if (entryPtr + dataSize > entryEnd) {
                return Fail("Bitmap data exceeds entry bounds");
            }
            bitmap.data.assign(entryPtr, entryPtr + dataSize);
            entryPtr += dataSize;
            entry.bitmaps.push_back(std::move(bitmap));
        }

        if (blockSize != 0) {
            const uint32_t attachmentOffset = offset + blockSize;
            if (attachmentOffset + 4 < nextOffset) {
                const uint8_t* attachment = filePtr + attachmentOffset;
                if (attachment[0] == 0x70) {
                    auto labelStart = reinterpret_cast<const char*>(attachment + 4);
                    auto labelEnd = reinterpret_cast<const char*>(filePtr + nextOffset);
                    const char* terminator = std::find(labelStart, labelEnd, '\0');
                    entry.label.assign(labelStart, terminator);
                }
            }
        }

        outFile.entries.push_back(std::move(entry));
    }

    return outFile;
}

bool Reader::ConvertToRGBA8(const Bitmap& bitmap, std::vector<uint8_t>& outRGBA) {
    if (bitmap.width == 0 || bitmap.height == 0) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height);
    outRGBA.assign(pixelCount * 4, 0);

    if (bitmap.IsDXT()) {
        if (bitmap.width % 4 != 0 || bitmap.height % 4 != 0) {
            return false;
        }
    }

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
                Reader::ARGB4444ToRGBA8(color, dst);
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
                Reader::RGB565ToRGBA8(color, dst);
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
                Reader::ARGB1555ToRGBA8(color, dst);
                dst += 4;
            }
            return true;
        }
        case kCodeDXT1:
        case kCodeDXT3:
        case kCodeDXT5: {
            int squishFlags = squish::kDxt1;
            if (bitmap.code == kCodeDXT3) {
                squishFlags = squish::kDxt3;
            } else if (bitmap.code == kCodeDXT5) {
                squishFlags = squish::kDxt5;
            }
            squish::DecompressImage(outRGBA.data(),
                                    static_cast<int>(bitmap.width),
                                    static_cast<int>(bitmap.height),
                                    bitmap.data.data(),
                                    squishFlags);
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
