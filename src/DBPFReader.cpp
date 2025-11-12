#include "DBPFReader.h"

#include <algorithm>
#include <format>
#include <print>

#include "ExemplarReader.h"
#include "FSHReader.h"
#include "LTextReader.h"
#include "RUL0.h"
#include "QFSDecompressor.h"
#include "S3DReader.h"

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
        const uint32_t chunkSize = ReadUInt32LE(data);
        const uint32_t uncompressed = ReadUInt32LE(data + 4);
        size_t flagOffset = 8;
        uint8_t code = data[flagOffset];
        if ((code != 0x10 && code != 0x11) && size >= 11) {
            flagOffset = 10;
            code = data[flagOffset];
        }

        if (code == 0x10 && chunkSize > 0 && flagOffset + 1 + chunkSize <= size) {
            chunkHeaderSize = flagOffset + 1;
            chunkBodySize = chunkSize;
            return true;
        }
        if (code == 0x11 && size >= flagOffset + 5) {
            chunkHeaderSize = flagOffset + 5;
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
            const uint16_t candidate = static_cast<uint16_t>(static_cast<uint16_t>(dataStart[i]) << 8) |
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
            dataStart += chunkHeaderSize;
            dataSize = chunkBodySize;
        }

        bool _ = AlignToQfsSignature(dataStart, dataSize);

        std::vector data(dataStart, dataStart + dataSize);

        std::span<const uint8_t> dataSpan(data.data(), data.size());

        if (QFS::Decompressor::IsQFSCompressed(dataSpan)) {
            std::vector<uint8_t> decompressed;
            auto result = QFS::Decompressor::Decompress(dataSpan, decompressed);
            if (!result.has_value()) {
                return std::nullopt;
            }
            return decompressed;
        }
        return data;
    }

    std::optional<std::vector<uint8_t>> Reader::ReadEntryData(const Tgi& tgi) const {
        const IndexEntry* entry = FindEntry(tgi);
        if (!entry) {
            return std::nullopt;
        }
        return ReadEntryData(*entry);
    }

    const IndexEntry* Reader::FindEntry(const Tgi& tgi) const {
        const auto it = mTGIIndex.find(tgi);
        if (it == mTGIIndex.end()) {
            return nullptr;
        }
        return it->second;
    }

    std::vector<const IndexEntry*> Reader::FindEntries(const TgiMask& mask) const {
        std::vector<const IndexEntry*> matches;
        auto pushIfMatches = [&](const IndexEntry* entry) {
            if (entry && mask.Matches(entry->tgi)) {
                matches.push_back(entry);
            }
        };

        if (mask.type.has_value()) {
            const auto [begin, end] = mTypeIndex.equal_range(*mask.type);
            for (auto it = begin; it != end; ++it) {
                pushIfMatches(it->second);
            }
            return matches;
        }

        if (mask.group.has_value()) {
            const auto [begin, end] = mGroupIndex.equal_range(*mask.group);
            for (auto it = begin; it != end; ++it) {
                pushIfMatches(it->second);
            }
            return matches;
        }

        if (mask.instance.has_value()) {
            const auto [begin, end] = mInstanceIndex.equal_range(*mask.instance);
            for (auto it = begin; it != end; ++it) {
                pushIfMatches(it->second);
            }
            return matches;
        }

        for (const auto& entry : mIndex) {
            pushIfMatches(&entry);
        }
        return matches;
    }

    std::vector<const IndexEntry*> Reader::FindEntries(std::string_view label) const {
        const auto mask = MaskForLabel(label);
        if (!mask) {
            return {};
        }
        return FindEntries(*mask);
    }

    std::optional<IndexEntry> Reader::FindFirstEntry(std::string_view label) const {
        const auto entries = FindEntries(label);
        if (entries.empty()) {
            return std::nullopt;
        }
        return *entries.front();
    }

    std::optional<std::vector<uint8_t>> Reader::ReadFirstMatching(const TgiMask& mask) const {
        const auto entries = FindEntries(mask);
        if (entries.empty()) {
            return std::nullopt;
        }
        return ReadEntryData(*entries.front());
    }

    std::optional<std::vector<uint8_t>> Reader::ReadFirstMatching(std::string_view label) const {
        const auto mask = MaskForLabel(label);
        if (!mask) {
            return std::nullopt;
        }
        return ReadFirstMatching(*mask);
    }

    ParseExpected<FSH::Record> Reader::LoadFSH(const IndexEntry& entry) const {
        auto payload = ReadEntryData(entry);
        if (!payload) {
            return Fail("failed to read data for {}", entry.tgi.ToString());
        }
        return FSH::Reader::Parse(std::span<const uint8_t>(payload->data(), payload->size()));
    }

    ParseExpected<FSH::Record> Reader::LoadFSH(const Tgi& tgi) const {
        const IndexEntry* entry = FindEntry(tgi);
        if (!entry) {
            return Fail("No entry found for {}", tgi.ToString());
        }
        return LoadFSH(*entry);
    }

    ParseExpected<FSH::Record> Reader::LoadFSH(const TgiMask& mask) const {
        const auto entries = FindEntries(mask);
        if (entries.empty()) {
            return Fail("No entry matched the provided mask");
        }
        return LoadFSH(*entries.front());
    }

    ParseExpected<FSH::Record> Reader::LoadFSH(std::string_view label) const {
        const auto entries = FindEntries(label);
        if (entries.empty()) {
            return Fail("No entries found for label {}", label);
        }
        return LoadFSH(*entries.front());
    }

    ParseExpected<S3D::Record> Reader::LoadS3D(const IndexEntry& entry) const {
        auto payload = ReadEntryData(entry);
        if (!payload) {
            return Fail("Failed to read data for {}", entry.tgi.ToString());
        }
        return S3D::Reader::Parse(std::span<const uint8_t>(payload->data(), payload->size()));
    }

    ParseExpected<S3D::Record> Reader::LoadS3D(const Tgi& tgi) const {
        const IndexEntry* entry = FindEntry(tgi);
        if (!entry) {
            return Fail("No entry found for {}", tgi.ToString());
        }
        return LoadS3D(*entry);
    }

    ParseExpected<S3D::Record> Reader::LoadS3D(const TgiMask& mask) const {
        auto entries = FindEntries(mask);
        if (entries.empty()) {
            return Fail("no entry matched the provided mask");
        }
        return LoadS3D(*entries.front());
    }

    ParseExpected<S3D::Record> Reader::LoadS3D(std::string_view label) const {
        auto entries = FindEntries(label);
        if (entries.empty()) {
            return Fail("No entries found for label {}", label);
        }
        return LoadS3D(*entries.front());
    }

    ParseExpected<Exemplar::Record> Reader::LoadExemplar(const IndexEntry& entry) const {
        auto payload = ReadEntryData(entry);
        if (!payload) {
            return Fail("Failed to read data for {}", entry.tgi.ToString());
        }
        return Exemplar::Parse(std::span<const uint8_t>(payload->data(), payload->size()));
    }

    ParseExpected<Exemplar::Record> Reader::LoadExemplar(const Tgi& tgi) const {
        const IndexEntry* entry = FindEntry(tgi);
        if (!entry) {
            return Fail("No entry found for {}", tgi.ToString());
        }
        return LoadExemplar(*entry);
    }

    ParseExpected<Exemplar::Record> Reader::LoadExemplar(const TgiMask& mask) const {
        auto entries = FindEntries(mask);
        if (entries.empty()) {
            return Fail("No entry matched the provided mask");
        }
        return LoadExemplar(*entries.front());
    }

    ParseExpected<Exemplar::Record> Reader::LoadExemplar(std::string_view label) const {
        auto entries = FindEntries(label);
        if (entries.empty()) {
            return Fail("No entries found for label {}", label);
        }
        return LoadExemplar(*entries.front());
    }

    ParseExpected<LText::Record> Reader::LoadLText(const IndexEntry& entry) const {
        auto payload = ReadEntryData(entry);
        if (!payload.has_value()) {
            return Fail("Failed to read entry data for {}", entry.tgi.ToString());
        }
        std::span<const uint8_t> buffer(payload->data(), payload->size());
        return LText::Parse(buffer);
    }

    ParseExpected<LText::Record> Reader::LoadLText(const Tgi& tgi) const {
        const IndexEntry* entry = FindEntry(tgi);
        if (!entry) {
            return Fail("No entry found for {}", tgi.ToString());
        }
        return LoadLText(*entry);
    }

    ParseExpected<LText::Record> Reader::LoadLText(const TgiMask& mask) const {
        auto entries = FindEntries(mask);
        if (entries.empty()) {
            return Fail("No entry matched the provided mask");
        }
        return LoadLText(*entries.front());
    }

    ParseExpected<LText::Record> Reader::LoadLText(std::string_view label) const {
        auto entries = FindEntries(label);
        if (entries.empty()) {
            return Fail("No entries found for label {}", label);
        }
        return LoadLText(*entries.front());
    }

    ParseExpected<RUL0::Record> Reader::LoadRUL0(const IndexEntry& entry) const {
        auto payload = ReadEntryData(entry);
        if (!payload.has_value()) {
            return Fail("Failed to read entry data for {}", entry.tgi.ToString());
        }
        const std::span<const uint8_t> buffer(payload->data(), payload->size());
        return RUL0::Parse(buffer);
    }

    ParseExpected<RUL0::Record> Reader::LoadRUL0() const {
        const auto entry = FindFirstEntry("RUL0 (Intersection Ordering)");
        if (!entry.has_value()) {
            return Fail("No RUL0 (Intersection Ordering) entry found");
        }
        return LoadRUL0(entry.value());
    }

    bool Reader::ParseBuffer(const std::span<const uint8_t> buffer) {
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
        if (buffer.size() < mHeader.indexEntryCount * 20) {
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
        if (!mMappedFile.MapRange(mHeader.indexOffsetLocation,
                                  mHeader.indexSize,
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
        const size_t start = entry.offset;
        const size_t length = entry.size;

        switch (mDataSource) {
        case DataSource::kBuffer: {
            if (mFileBuffer.empty() || start > mFileBuffer.size() || start + length > mFileBuffer.size()) {
                return false;
            }
            out.span = std::span(mFileBuffer.data() + start, length);
            return true;
        }
        case DataSource::kMappedFile: {
            if (!mMappedFile.IsOpen()) {
                return false;
            }
            if (!mMappedFile.MapRange(entry.offset, length, out.mappedRange)) {
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
