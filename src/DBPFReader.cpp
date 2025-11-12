#include "DBPFReader.h"

#include <algorithm>
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
        mFileBuffer.clear();
        mMappedFile.Close();
        mDataSource = DataSource::kNone;

        if (!mMappedFile.Open(path)) {
            return false;
        }

        mDataSource = DataSource::kMappedFile;
        if (!ParseMappedFile()) {
            mMappedFile.Close();
            mDataSource = DataSource::kNone;
            return false;
        }
        return true;
    }

    bool Reader::LoadBuffer(const uint8_t* data, size_t size) {
        if (!data || size < kHeaderSize) {
            return false;
        }

        mMappedFile.Close();
        mFileBuffer.assign(data, data + size);
        mDataSource = DataSource::kBuffer;
        if (!ParseBuffer(std::span<const uint8_t>(mFileBuffer.data(), mFileBuffer.size()))) {
            mFileBuffer.clear();
            mDataSource = DataSource::kNone;
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
        EntryData entryData;
        if (!LoadEntryData(entry, entryData)) {
            std::println("[DBPF] Invalid bounds for entry {} (offset {}, size {})",
                         entry.tgi.ToString(), entry.offset, entry.size);
            return std::nullopt;
        }

        const uint8_t* dataStart = entryData.span.data();
        size_t dataSize = entryData.span.size();

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

        std::span<const uint8_t> dataSpan(data.data(), data.size());

        if (QFS::Decompressor::IsQFSCompressed(dataSpan)) {
            std::println("[DBPF] Decompressing entry {} ({} bytes)",
                         entry.tgi.ToString(), data.size());
            std::vector<uint8_t> decompressed;
            if (!QFS::Decompressor::Decompress(dataSpan, decompressed)) {
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
        mTGIIndex.clear();
        mTypeIndex.clear();
        mGroupIndex.clear();
        mInstanceIndex.clear();
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
        if (!ApplyDirectoryMetadata()) {
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
        mHeader.indexSize = ReadUInt32LE(buffer.data() + 44);
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

        return ParseIndexSpan(buffer.subspan(indexOffset, mHeader.indexSize));
    }

    bool Reader::ParseIndexSpan(std::span<const uint8_t> buffer) {
        if (buffer.size() < static_cast<size_t>(mHeader.indexEntryCount) * 20) {
            return false;
        }

        const uint8_t* ptr = buffer.data();
        const uint8_t* end = buffer.data() + buffer.size();

        std::vector<IndexEntry> parsed;
        parsed.reserve(mHeader.indexEntryCount);

        for (uint32_t i = 0; i < mHeader.indexEntryCount; ++i) {
            if (ptr + 20 > end) {
                return false;
            }
            IndexEntry entry;
            entry.tgi.type = ReadUInt32LE(ptr);
            entry.tgi.group = ReadUInt32LE(ptr + 4);
            entry.tgi.instance = ReadUInt32LE(ptr + 8);
            entry.offset = ReadUInt32LE(ptr + 12);
            entry.size = ReadUInt32LE(ptr + 16);
            parsed.push_back(entry);
            ptr += 20;
        }

        mIndex = std::move(parsed);
        mTGIIndex.clear();
        mTypeIndex.clear();
        mGroupIndex.clear();
        mInstanceIndex.clear();

        for (auto& entry : mIndex) {
            mTGIIndex.emplace(entry.tgi, &entry);
            mTypeIndex.emplace(entry.tgi.type, &entry);
            mGroupIndex.emplace(entry.tgi.group, &entry);
            mInstanceIndex.emplace(entry.tgi.instance, &entry);
        }

        return true;
    }

    bool Reader::ApplyDirectoryMetadata() {
        const auto dirIt = mTGIIndex.find(kDirectoryTgi);
        if (dirIt == mTGIIndex.end() || dirIt->second == nullptr) {
            return true;
        }

        EntryData directoryData;
        if (!LoadEntryData(*dirIt->second, directoryData)) {
            return false;
        }

        const auto span = directoryData.span;
        const uint8_t* ptr = span.data();
        const uint8_t* end = ptr + span.size();

        // Each directory record is 16 bytes: type, group, instance, decompressedSize
        constexpr size_t kRecordSize = 16;
        while (static_cast<size_t>(end - ptr) >= kRecordSize) {
            Tgi tgi;
            tgi.type = ReadUInt32LE(ptr);
            tgi.group = ReadUInt32LE(ptr + 4);
            tgi.instance = ReadUInt32LE(ptr + 8);
            uint32_t decompressed = ReadUInt32LE(ptr + 12);

            auto it = mTGIIndex.find(tgi);
            if (it != mTGIIndex.end() && it->second != nullptr) {
                it->second->decompressedSize = decompressed;
            }

            ptr += kRecordSize;
        }
        return true;
    }

    bool Reader::ParseMappedFile() {
        mIndex.clear();
        mTGIIndex.clear();
        mTypeIndex.clear();
        mGroupIndex.clear();
        mInstanceIndex.clear();
        io::MappedFile::Range headerRange;
        if (!mMappedFile.MapRange(0, kHeaderSize, headerRange)) {
            return false;
        }
        if (!ParseHeader(headerRange.View())) {
            return false;
        }

        io::MappedFile::Range indexRange;
        if (!mMappedFile.MapRange(static_cast<uint64_t>(mHeader.indexOffsetLocation),
                                  static_cast<size_t>(mHeader.indexSize),
                                  indexRange)) {
            return false;
        }
        if (!ParseIndexSpan(indexRange.View())) {
            return false;
        }
        if (!ApplyDirectoryMetadata()) {
            return false;
        }
        return true;
    }

    bool Reader::LoadEntryData(const IndexEntry& entry, EntryData& out) const {
        const size_t start = static_cast<size_t>(entry.offset);
        const size_t length = static_cast<size_t>(entry.size);

        switch (mDataSource) {
        case DataSource::kBuffer: {
            if (mFileBuffer.empty() || start > mFileBuffer.size() || start + length > mFileBuffer.size()) {
                return false;
            }
            out.span = std::span<const uint8_t>(mFileBuffer.data() + start, length);
            return true;
        }
        case DataSource::kMappedFile: {
            if (!mMappedFile.IsOpen()) {
                return false;
            }
            if (!mMappedFile.MapRange(static_cast<uint64_t>(entry.offset), length, out.mappedRange)) {
                return false;
            }
            out.span = out.mappedRange.View();
            return out.span.size() == length;
        }
        default:
            return false;
        }
    }

} // namespace DBPF
