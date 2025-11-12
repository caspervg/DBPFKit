#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "DBPFStructures.h"
#include "MappedFile.h"

namespace DBPF {
    struct TgiHash;
    struct Tgi;

    constexpr Tgi kDirectoryTgi{0xE86B1EEF, 0xE86B1EEF, 0x286B1F03};

    struct Header {
        uint32_t majorVersion = 0;
        uint32_t minorVersion = 0;
        uint32_t dateCreated = 0;
        uint32_t dateModified = 0;
        uint32_t indexType = 0;
        uint32_t indexEntryCount = 0;
        uint32_t indexOffsetLocation = 0;
        uint32_t indexSize = 0;
        uint32_t holeEntryCount = 0;
        uint32_t holeOffsetLocation = 0;
        uint32_t holeSize = 0;
    };

    class Reader {
    public:
        bool LoadFile(const std::filesystem::path& path);
        bool LoadBuffer(const uint8_t* data, size_t size);

        [[nodiscard]] const Header& GetHeader() const { return mHeader; }
        [[nodiscard]] const std::vector<IndexEntry>& GetIndex() const { return mIndex; }
        [[nodiscard]] std::optional<std::vector<uint8_t>> ReadEntryData(const IndexEntry& entry) const;

    private:
        enum class DataSource {
            kNone,
            kBuffer,
            kMappedFile
        };

        struct EntryData {
            std::span<const uint8_t> span{};
            io::MappedFile::Range mappedRange{};
        };

        bool ParseBuffer(std::span<const uint8_t> buffer);
        bool ParseHeader(std::span<const uint8_t> buffer);
        bool ParseIndex(std::span<const uint8_t> buffer);
        bool ParseIndexSpan(std::span<const uint8_t> buffer);
        bool ApplyDirectoryMetadata();
        bool ParseMappedFile();
        bool LoadEntryData(const IndexEntry& entry, EntryData& out) const;

        std::vector<uint8_t> mFileBuffer;
        io::MappedFile mMappedFile;
        Header mHeader{};
        std::vector<IndexEntry> mIndex;

        std::unordered_map<Tgi, IndexEntry*, TgiHash> mTGIIndex;
        std::unordered_multimap<uint32_t, IndexEntry*> mTypeIndex;
        std::unordered_multimap<uint32_t, IndexEntry*> mGroupIndex;
        std::unordered_multimap<uint32_t, IndexEntry*> mInstanceIndex;
        DataSource mDataSource = DataSource::kNone;
    };

} // namespace DBPF
