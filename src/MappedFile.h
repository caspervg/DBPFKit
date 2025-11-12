#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <mio/mmap.hpp>

namespace io {

    class MappedFile {
    public:
        class Range {
        public:
            Range() = default;
            Range(const Range&) = delete;
            Range& operator=(const Range&) = delete;
            Range(Range&&) noexcept = default;
            Range& operator=(Range&&) noexcept = default;
            ~Range() = default;

            [[nodiscard]] std::span<const uint8_t> View() const { return mSpan; }
            [[nodiscard]] bool Empty() const { return mSpan.empty(); }

        private:
            friend class MappedFile;
            void Reset();

            mio::mmap_source mMap;
            std::vector<uint8_t> mFallback;
            std::span<const uint8_t> mSpan{};
        };

        MappedFile() = default;
        MappedFile(const MappedFile&) = delete;
        MappedFile& operator=(const MappedFile&) = delete;
        MappedFile(MappedFile&&) noexcept = default;
        MappedFile& operator=(MappedFile&&) noexcept = default;
        ~MappedFile() = default;

        bool Open(const std::filesystem::path& path);
        void Close();

        [[nodiscard]] bool IsOpen() const { return mIsOpen; }
        [[nodiscard]] uint64_t FileSize() const { return mFileSize; }
        [[nodiscard]] const std::filesystem::path& Path() const { return mPath; }

        bool MapRange(uint64_t offset, size_t length, Range& outRange) const;

    private:
        bool ReadFallback(uint64_t offset, size_t length, Range& outRange) const;

        std::filesystem::path mPath;
        uint64_t mFileSize = 0;
        bool mIsOpen = false;
    };

} // namespace io
