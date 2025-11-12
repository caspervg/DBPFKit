#pragma once

#include <cstdint>
#include <span>
#include <vector>

#define QFS_DEBUG true

namespace QFS {

    constexpr uint16_t MAGIC_COMPRESSED = 0x10FB;
    constexpr uint16_t MAGIC_UNCOMPRESSED = 0x0010;

    class Decompressor {
    public:
        static bool Decompress(std::span<const uint8_t> input, std::vector<uint8_t>& output);
        static bool IsQFSCompressed(std::span<const uint8_t> buffer);
        static uint32_t GetUncompressedSize(std::span<const uint8_t> buffer);

    private:
        static bool DecompressInternal(const uint8_t* input, size_t inputSize,
                                       uint8_t* output, size_t outputSize);
    };

} // namespace QFS
