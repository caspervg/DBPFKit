#pragma once

#include <cstdint>
#include <vector>

#define QFS_DEBUG true

namespace QFS {

    constexpr uint16_t MAGIC_COMPRESSED = 0x10FB;
    constexpr uint16_t MAGIC_UNCOMPRESSED = 0x0010;

    class Decompressor {
    public:
        static bool Decompress(const uint8_t* input, size_t inputSize, std::vector<uint8_t>& output);
        static bool IsQFSCompressed(const uint8_t* data, size_t size);
        static uint32_t GetUncompressedSize(const uint8_t* data, size_t size);

    private:
        static bool DecompressInternal(const uint8_t* input, size_t inputSize,
                                       uint8_t* output, size_t outputSize);
    };

} // namespace QFS
