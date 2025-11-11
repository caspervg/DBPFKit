#include "DBPFReader.h"

#include <algorithm>
#include <cstdio>
#include <print>

#include "QFSDecompressor.h"

namespace {

    constexpr uint32_t kMagicDBPF =
        static_cast<uint32_t>('D') | (static_cast<uint32_t>('B') << 8) |
        (static_cast<uint32_t>('P') << 16) | (static_cast<uint32_t>('F') << 24);
    constexpr size_t kHeaderSize = 0x60;
    constexpr uint32_t kSupportedIndexType = 7;

    uint32_t ReadUInt32LE(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24);
    }

} // namespace

namespace DBPF {

    bool Reader::LoadFile(const std::filesystem::path& path) {
        FILE* file = std::fopen(path.string().c_str(), "rb");
        if (!file) {
            return false;
        }

        if (std::fseek(file, 0, SEEK_END) != 0) {
            std::fclose(file);
            return false;
        }
        long size = std::ftell(file);
        if (size <= 0) {
            std::fclose(file);
            return false;
        }
        if (std::fseek(file, 0, SEEK_SET) != 0) {
            std::fclose(file);
            return false;
        }

        mFileBuffer.resize(static_cast<size_t>(size));
        if (std::fread(mFileBuffer.data(), 1, mFileBuffer.size(), file) != mFileBuffer.size()) {
            mFileBuffer.clear();
            std::fclose(file);
            return false;
        }
        std::fclose(file);
        if (!ParseBuffer(std::span<const uint8_t>(mFileBuffer.data(), mFileBuffer.size()))) {
            mFileBuffer.clear();
            return false;
        }
        return true;
    }

    bool Reader::LoadBuffer(const uint8_t* data, size_t size) {
        if (!data || size < kHeaderSize) {
            return false;
        }

        mFileBuffer.assign(data, data + size);
        if (!ParseBuffer(std::span<const uint8_t>(mFileBuffer.data(), mFileBuffer.size()))) {
            mFileBuffer.clear();
            return false;
        }
        return true;
    }

    bool IsChunkHeader(const uint8_t* data, size_t size,
                       uint32_t& chunkHeaderSize, uint32_t& chunkBodySize) {
        if (size < 9) {
            return false;
        }
        uint32_t chunkSize = ReadUInt32LE(data);
        uint32_t uncompressed = ReadUInt32LE(data + 4);
        size_t flagOffset = 8;
        uint8_t code = data[flagOffset];
        if ((code != 0x10 && code != 0x11) && size >= 11) {
            flagOffset = 10;
            code = data[flagOffset];
        }

        if (code == 0x10 && chunkSize > 0 && flagOffset + 1 + chunkSize <= size) {
            chunkHeaderSize = static_cast<uint32_t>(flagOffset + 1);
            chunkBodySize = chunkSize;
            return true;
        }
        if (code == 0x11 && size >= flagOffset + 5) {
            chunkHeaderSize = static_cast<uint32_t>(flagOffset + 5);
            uint32_t body = ReadUInt32LE(data + flagOffset + 1);
            if (body == 0 || chunkHeaderSize + body > size) {
                return false;
            }
            chunkBodySize = body;
            return true;
        }

        (void)uncompressed;
        return false;
    }

    bool AlignToQfsSignature(const uint8_t*& dataStart, size_t& dataSize) {
        for (size_t i = 0; i + 1 < dataSize && i < 16; ++i) {
            uint16_t candidate = static_cast<uint16_t>(static_cast<uint16_t>(dataStart[i]) << 8) |
                                 static_cast<uint16_t>(dataStart[i + 1]);
            if (candidate == QFS::MAGIC_COMPRESSED) {
                if (i > 0) {
                    dataStart += i;
                    dataSize -= i;
                }
                return true;
            }
        }
        return false;
    }

    std::optional<std::vector<uint8_t>> Reader::ReadEntryData(const IndexEntry& entry) const {
        const auto start = static_cast<size_t>(entry.offset);
        const auto length = static_cast<size_t>(entry.size);
        if (mFileBuffer.empty() || start + length > mFileBuffer.size()) {
            std::println("[DBPF] Invalid bounds for entry {} (offset {}, size {})",
                         entry.tgi.ToString(), entry.offset, entry.size);
            return std::nullopt;
        }

        const uint8_t* dataStart = mFileBuffer.data() + start;
        size_t dataSize = length;

        uint32_t chunkHeaderSize = 0;
        uint32_t chunkBodySize = 0;
        if (IsChunkHeader(dataStart, dataSize, chunkHeaderSize, chunkBodySize)) {
            std::println("[DBPF] Entry {} uses chunk header ({} bytes) body size {}",
                         entry.tgi.ToString(), chunkHeaderSize, chunkBodySize);
            dataStart += chunkHeaderSize;
            dataSize = chunkBodySize;
        }

        bool aligned = AlignToQfsSignature(dataStart, dataSize);
        if (aligned) {
            std::println("[DBPF] Entry {} aligned to QFS signature at new size {}", entry.tgi.ToString(), dataSize);
        }

        std::vector<uint8_t> data(dataStart, dataStart + dataSize);

        if (QFS::Decompressor::IsQFSCompressed(data.data(), data.size())) {
            std::println("[DBPF] Decompressing entry {} ({} bytes)",
                         entry.tgi.ToString(), data.size());
            std::vector<uint8_t> decompressed;
            if (!QFS::Decompressor::Decompress(data.data(), data.size(), decompressed)) {
                std::println("[DBPF] QFS decompression failed for {}", entry.tgi.ToString());
                return std::nullopt;
            }
            std::println("[DBPF] Entry {} decompressed to {} bytes",
                         entry.tgi.ToString(), decompressed.size());
            return decompressed;
        }
        std::println("[DBPF] Entry {} not compressed ({} bytes)", entry.tgi.ToString(), data.size());
        return data;
    }

    bool Reader::ParseBuffer(std::span<const uint8_t> buffer) {
        mIndex.clear();
        if (buffer.size() < kHeaderSize) {
            return false;
        }

        if (!ParseHeader(buffer.subspan(0, kHeaderSize))) {
            return false;
        }
        if (!ParseIndex(buffer)) {
            mIndex.clear();
            return false;
        }
        if (!ApplyDirectoryMetadata(buffer)) {
            return false;
        }
        return true;
    }

    bool Reader::ParseHeader(std::span<const uint8_t> buffer) {
        if (buffer.size() < kHeaderSize) {
            return false;
        }
        if (ReadUInt32LE(buffer.data()) != kMagicDBPF) {
            return false;
        }

        mHeader.majorVersion = ReadUInt32LE(buffer.data() + 4);
        mHeader.minorVersion = ReadUInt32LE(buffer.data() + 8);
        mHeader.dateCreated = ReadUInt32LE(buffer.data() + 24);
        mHeader.dateModified = ReadUInt32LE(buffer.data() + 28);
        mHeader.indexType = ReadUInt32LE(buffer.data() + 32);
        mHeader.indexEntryCount = ReadUInt32LE(buffer.data() + 36);
        mHeader.indexOffsetLocation = ReadUInt32LE(buffer.data() + 40);
        mHeader
            .indexSize = ReadUInt32LE(buffer.data() + 44);
        mHeader.holeEntryCount = ReadUInt32LE(buffer.data() + 48);
        mHeader.holeOffsetLocation = ReadUInt32LE(buffer.data() + 52);
        mHeader.holeSize = ReadUInt32LE(buffer.data() + 56);

        if (mHeader.majorVersion != 1 || mHeader.minorVersion != 0) {
            return false;
        }
        if (mHeader.indexType != kSupportedIndexType) {
            return false;
        }
        return true;
    }

    bool Reader::ParseIndex(std::span<const uint8_t> buffer) {
        const size_t indexOffset = mHeader.indexOffsetLocation;
        if (indexOffset + mHeader.indexSize > buffer.size()) {
            return false;
        }

        const uint8_t* ptr = buffer.data() + indexOffset;
        mIndex.clear();
        mIndex.reserve(mHeader.indexEntryCount);

        for (uint32_t i = 0; i < mHeader.indexEntryCount; ++i) {
            if (ptr + 20 > buffer.data() + buffer.size()) {
                return false;
            }
            IndexEntry entry;
            entry.tgi.type = ReadUInt32LE(ptr);
            entry.tgi.group = ReadUInt32LE(ptr + 4);
            entry.tgi.instance = ReadUInt32LE(ptr + 8);
            entry.offset = ReadUInt32LE(ptr + 12);
            entry.size = ReadUInt32LE(ptr + 16);
            mIndex.push_back(entry);
            ptr += 20;
        }

        // Now build the quick indices
        for (auto& entry : mIndex) {
            mTGIIndex.emplace(entry.tgi, &entry);
            mTypeIndex.emplace(entry.tgi.type, &entry);
            mGroupIndex.emplace(entry.tgi.group, &entry);
            mInstanceIndex.emplace(entry.tgi.instance, &entry);
        }

        return true;
    }

    bool Reader::ApplyDirectoryMetadata(std::span<const uint8_t> buffer) {
        const auto dirIt = std::find_if(mIndex.begin(), mIndex.end(), [](const IndexEntry& entry) {
            return entry.tgi == kDirectoryTgi;
        });
        if (dirIt == mIndex.end()) {
            return true;
        }

        const auto spanOpt = GetEntrySpan(*dirIt, buffer);
        if (!spanOpt) {
            return false;
        }

        auto span = *spanOpt;
        const uint8_t* ptr = span.data();
        const uint8_t* end = ptr + span.size();
        while (ptr + 16 <= end) {
            Tgi tgi;
            tgi.type = ReadUInt32LE(ptr);
            tgi.group = ReadUInt32LE(ptr + 4);
            tgi.instance = ReadUInt32LE(ptr + 8);
            uint32_t decompressed = ReadUInt32LE(ptr + 12);

            auto target = std::find_if(mIndex.begin(), mIndex.end(), [&](IndexEntry& entry) {
                return entry.tgi == tgi;
            });
            if (target != mIndex.end()) {
                target->decompressedSize = decompressed;
            }
            ptr += 16;
        }
        return true;
    }

    std::optional<std::span<const uint8_t>> Reader::GetEntrySpan(const IndexEntry& entry,
                                                                 const std::span<const uint8_t> buffer) const {
        const auto start = static_cast<size_t>(entry.offset);
        const auto length = static_cast<size_t>(entry.size);
        if (start + length > buffer.size()) {
            return std::nullopt;
        }
        return buffer.subspan(start, length);
    }

} // namespace DBPF
