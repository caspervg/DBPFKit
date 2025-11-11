#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "DBPFStructures.h"

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
        bool ParseBuffer(std::span<const uint8_t> buffer);
        bool ParseHeader(std::span<const uint8_t> buffer);
        bool ParseIndex(std::span<const uint8_t> buffer);
        bool ApplyDirectoryMetadata(std::span<const uint8_t> buffer);
        [[nodiscard]] std::optional<std::span<const uint8_t>> GetEntrySpan(const IndexEntry& entry,
                                                                           std::span<const uint8_t> buffer) const;

        std::vector<uint8_t> mFileBuffer;
        Header mHeader{};
        std::vector<IndexEntry> mIndex;

        std::unordered_map<Tgi, const IndexEntry*, TgiHash> mTGIIndex;
        std::unordered_multimap<uint32_t, const IndexEntry*> mTypeIndex;
        std::unordered_multimap<uint32_t, const IndexEntry*> mGroupIndex;
        std::unordered_multimap<uint32_t, const IndexEntry*> mInstanceIndex;
    };

} // namespace DBPF
