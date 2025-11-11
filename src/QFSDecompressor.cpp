#include "QFSDecompressor.h"

namespace {

    constexpr size_t kHeaderSize = 5;
    constexpr size_t kChunkedHeaderSize = 8;
    constexpr uint8_t kChunkFlag = 0x01;

    struct QFSHeader {
        uint32_t uncompressedSize = 0;
        size_t dataOffset = 0;
    };

    uint16_t ReadSignature(const uint8_t* data) {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[0] & 0xFE) << 8) | data[1]);
    }

    bool ParseHeader(const uint8_t* input, size_t inputSize, QFSHeader& header) {
        if (!input || inputSize < kHeaderSize) {
            return false;
        }

        if (ReadSignature(input) != QFS::MAGIC_COMPRESSED) {
            return false;
        }

        header.uncompressedSize = (static_cast<uint32_t>(input[2]) << 16) |
            (static_cast<uint32_t>(input[3]) << 8) |
            static_cast<uint32_t>(input[4]);

        header.dataOffset = (input[0] & kChunkFlag) ? kChunkedHeaderSize : kHeaderSize;
        if (header.dataOffset > inputSize) {
            return false;
        }

        return true;
    }

    bool CopyFromHistory(uint8_t* output, size_t& outPos, size_t outputSize,
                         size_t offset, size_t length) {
        if (offset == 0 || offset > outPos) {
            return false;
        }
        if (outPos + length > outputSize) {
            return false;
        }

        size_t srcPos = outPos - offset;
        for (size_t i = 0; i < length; ++i) {
            output[outPos++] = output[srcPos++];
        }
        return true;
    }

} // namespace

namespace QFS {

    bool Decompressor::Decompress(const uint8_t* input, size_t inputSize, std::vector<uint8_t>& output) {
        QFSHeader header{};
        if (!ParseHeader(input, inputSize, header)) {
            return false;
        }

        output.assign(header.uncompressedSize, 0);
        if (header.uncompressedSize == 0) {
            return true;
        }

        const uint8_t* compressedData = input + header.dataOffset;
        const size_t compressedSize = inputSize - header.dataOffset;
        if (!DecompressInternal(compressedData, compressedSize, output.data(), header.uncompressedSize)) {
            output.clear();
            return false;
        }

        return true;
    }

    bool Decompressor::IsQFSCompressed(const uint8_t* data, size_t size) {
        if (!data || size < kHeaderSize) {
            return false;
        }
        return ReadSignature(data) == MAGIC_COMPRESSED;
    }

    uint32_t Decompressor::GetUncompressedSize(const uint8_t* data, size_t size) {
        QFSHeader header{};
        if (!ParseHeader(data, size, header)) {
            return 0;
        }
        return header.uncompressedSize;
    }

    bool Decompressor::DecompressInternal(const uint8_t* input, size_t inputSize,
                                          uint8_t* output, size_t outputSize) {
        size_t inPos = 0;
        size_t outPos = 0;

        while (inPos < inputSize && input[inPos] < 0xFC) {
            uint8_t packcode = input[inPos];

            if ((packcode & 0x80) == 0) {
                if (inPos + 1 >= inputSize) {
                    return false;
                }
                uint8_t a = input[inPos + 1];
                size_t literalLen = packcode & 0x03;
                if (inPos + 2 + literalLen > inputSize) {
                    return false;
                }
                if (outPos + literalLen > outputSize) {
                    return false;
                }
                std::memcpy(output + outPos, input + inPos + 2, literalLen);
                outPos += literalLen;
                inPos += literalLen + 2;

                size_t copyLen = ((packcode & 0x1C) >> 2) + 3;
                size_t offset = ((packcode >> 5) << 8) + a + 1;
                if (!CopyFromHistory(output, outPos, outputSize, offset, copyLen)) {
                    return false;
                }
            }
            else if ((packcode & 0x40) == 0) {
                if (inPos + 2 >= inputSize) {
                    return false;
                }
                uint8_t a = input[inPos + 1];
                uint8_t b = input[inPos + 2];

                size_t literalLen = (a >> 6) & 0x03;
                if (inPos + 3 + literalLen > inputSize) {
                    return false;
                }
                if (outPos + literalLen > outputSize) {
                    return false;
                }
                std::memcpy(output + outPos, input + inPos + 3, literalLen);
                outPos += literalLen;
                inPos += literalLen + 3;

                size_t copyLen = (packcode & 0x3F) + 4;
                size_t offset = (static_cast<size_t>(a & 0x3F) << 8) + b + 1;
                if (!CopyFromHistory(output, outPos, outputSize, offset, copyLen)) {
                    return false;
                }
            }
            else if ((packcode & 0x20) == 0) {
                if (inPos + 3 >= inputSize) {
                    return false;
                }
                uint8_t a = input[inPos + 1];
                uint8_t b = input[inPos + 2];
                uint8_t c = input[inPos + 3];

                size_t literalLen = packcode & 0x03;
                if (inPos + 4 + literalLen > inputSize) {
                    return false;
                }
                if (outPos + literalLen > outputSize) {
                    return false;
                }
                std::memcpy(output + outPos, input + inPos + 4, literalLen);
                outPos += literalLen;
                inPos += literalLen + 4;

                size_t copyLen = (static_cast<size_t>((packcode >> 2) & 0x03) << 8) + c + 5;
                size_t offset = (static_cast<size_t>(packcode & 0x10) << 12) +
                    (static_cast<size_t>(a) << 8) + b + 1;
                if (!CopyFromHistory(output, outPos, outputSize, offset, copyLen)) {
                    return false;
                }
            }
            else {
                size_t literalLen = ((packcode & 0x1F) << 2) + 4;
                if (inPos + 1 + literalLen > inputSize) {
                    return false;
                }
                if (outPos + literalLen > outputSize) {
                    return false;
                }
                std::memcpy(output + outPos, input + inPos + 1, literalLen);
                outPos += literalLen;
                inPos += literalLen + 1;
            }
        }

        if (inPos >= inputSize) {
            return false;
        }

        if (outPos < outputSize) {
            size_t literalLen = input[inPos] & 0x03;
            if (inPos + 1 + literalLen > inputSize) {
                return false;
            }
            if (outPos + literalLen > outputSize) {
                return false;
            }
            std::memcpy(output + outPos, input + inPos + 1, literalLen);
            outPos += literalLen;
            inPos += literalLen + 1;
        }

        return outPos == outputSize;
    }

} // namespace QFS
