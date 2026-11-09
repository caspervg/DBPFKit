#include "MappedFile.h"

#include <algorithm>
#include <fstream>
#include <print>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace io {

#ifdef _WIN32
    static std::filesystem::path ExtendedPath(const std::filesystem::path& path) {
        const auto& native = path.native();
        if (!path.is_absolute() || native.starts_with(LR"(\\?\)") || native.starts_with(LR"(\\.\)")) {
            return path;
        }
        return native.starts_with(LR"(\\)")
            ? std::filesystem::path(LR"(\\?\UNC\)" + native.substr(2))
            : std::filesystem::path(LR"(\\?\)" + native);
    }
#endif

    void MappedFile::Range::Reset() {
        mFallback.clear();
        mSpan = {};
    }

    bool MappedFile::Open(const std::filesystem::path& path) {
        Close();

#ifdef _WIN32
        mPath = ExtendedPath(path);
#else
        mPath = path;
#endif
        std::error_code ec;
        const auto size = std::filesystem::file_size(mPath, ec);
        if (ec) {
            std::println("[MappedFile] Failed to stat {}: {}", path.string(), ec.message());
            mPath.clear();
            return false;
        }

        mFileSize = static_cast<uint64_t>(size);
        mIsOpen = true;

        // Map the whole file once: mapping per entry reopens the file every time, which dominates scans.
        // When this fails the file stays open unmapped and MapRange reads through ReadFallback.
        std::error_code mapError;
#ifdef _WIN32
        mMap.map(mPath.native(), mapError);
#else
        // mio holds on to a descriptor it opens itself. Plugin folders can exceed the descriptor limit,
        // and a mapping outlives its descriptor, so open and close it here.
        if (const int descriptor = ::open(mPath.c_str(), O_RDONLY); descriptor != -1) {
            mMap.map(descriptor, mapError);
            ::close(descriptor);
        }
#endif
        if (mMap.is_mapped()) {
            mFileSize = static_cast<uint64_t>(mMap.size());
        }
        return true;
    }

    void MappedFile::Close() {
        mMap.unmap();
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

        if (mMap.is_mapped()) {
            outRange.mSpan = std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(mMap.data()) + static_cast<size_t>(offset),
                static_cast<size_t>(clampedLength));
            return true;
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
