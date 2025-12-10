#include "MappedFile.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace io {

    void MappedFile::Range::Reset() {
        if (mMap.is_mapped()) {
            mMap.unmap();
        }
        mFallback.clear();
        mSpan = {};
    }

    bool MappedFile::Open(const std::filesystem::path& path) {
        Close();

        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            // std::println("[MappedFile] Failed to stat {}: {}", path.string(), ec.message());
            return false;
        }

        mPath = path;
        mFileSize = static_cast<uint64_t>(size);
        mIsOpen = true;
        return true;
    }

    void MappedFile::Close() {
        mIsOpen = false;
        mFileSize = 0;
        mPath.clear();
    }

    bool MappedFile::MapRange(uint64_t offset, size_t length, Range& outRange) const {
        if (!mIsOpen) {
            return false;
        }
        if (offset > mFileSize) {
            return false;
        }
        const uint64_t clampedLength = std::min<uint64_t>(length, mFileSize - offset);
        if (length > clampedLength) {
            return false;
        }

        outRange.Reset();
        if (clampedLength == 0) {
            return true;
        }

        std::error_code ec;
        outRange.mMap.map(mPath.native(),
                          static_cast<size_t>(offset),
                          static_cast<size_t>(clampedLength),
                          ec);
        if (!ec && outRange.mMap.is_mapped()) {
            outRange.mSpan = std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(outRange.mMap.data()),
                outRange.mMap.length());
            return true;
        }

        if (outRange.mMap.is_mapped()) {
            outRange.mMap.unmap();
        }

        const bool fallbackResult = ReadFallback(offset, static_cast<size_t>(clampedLength), outRange);
        if (!fallbackResult) {
            outRange.Reset();
        }
        return fallbackResult;
    }

    bool MappedFile::ReadFallback(uint64_t offset, size_t length, Range& outRange) const {
        std::ifstream file(mPath, std::ios::binary);
        if (!file) {
            return false;
        }

        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file) {
            return false;
        }

        outRange.mFallback.resize(length);
        if (length > 0) {
            file.read(reinterpret_cast<char*>(outRange.mFallback.data()), static_cast<std::streamsize>(length));
            if (!file) {
                outRange.mFallback.clear();
                return false;
            }
        }

        outRange.mSpan = std::span<const uint8_t>(outRange.mFallback.data(), outRange.mFallback.size());
        return true;
    }

} // namespace io
