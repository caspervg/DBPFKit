#pragma once

#include <bit>
#include <concepts>
#include <cstring>
#include <span>
#include <string>

#include "ParseTypes.h"

namespace DBPF {

class SafeSpanReader {
public:
    explicit SafeSpanReader(std::span<const uint8_t> data)
        : mData(data), mOffset(0) {}
    
    explicit SafeSpanReader(std::span<const std::byte> data)
        : mData(reinterpret_cast<const uint8_t*>(data.data()), data.size()), mOffset(0) {}

    // Read a trivially copyable value in little-endian format
    template<std::integral T>
    [[nodiscard]] ParseExpected<T> ReadLE() {
        if (mOffset + sizeof(T) > mData.size()) {
            return Fail("Buffer underrun: need {} bytes at offset {}, but only {} bytes remain",
                        sizeof(T), mOffset, mData.size() - mOffset);
        }
        T value{};
        std::memcpy(&value, mData.data() + mOffset, sizeof(T));
        mOffset += sizeof(T);
        // Convert from little-endian if needed
        if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1) {
            value = std::byteswap(value);
        }
        return value;
    }

    // Read a trivially copyable struct/value (no endian conversion)
    template<typename T>
        requires std::is_trivially_copyable_v<T> && (!std::integral<T>)
    [[nodiscard]] ParseExpected<T> Read() {
        if (mOffset + sizeof(T) > mData.size()) {
            return Fail("Buffer underrun: need {} bytes at offset {}, but only {} bytes remain",
                        sizeof(T), mOffset, mData.size() - mOffset);
        }
        T value{};
        std::memcpy(&value, mData.data() + mOffset, sizeof(T));
        mOffset += sizeof(T);
        return value;
    }

    // Read raw bytes into a string
    [[nodiscard]] ParseExpected<std::string> ReadString(size_t length) {
        if (mOffset + length > mData.size()) {
            return Fail("Buffer underrun: need {} bytes at offset {}, but only {} bytes remain",
                        length, mOffset, mData.size() - mOffset);
        }
        std::string result(reinterpret_cast<const char*>(mData.data() + mOffset), length);
        mOffset += length;
        return result;
    }

    // Read raw bytes into a provided buffer
    [[nodiscard]] ParseExpected<void> ReadBytes(void* dest, size_t length) {
        if (mOffset + length > mData.size()) {
            return Fail("Buffer underrun: need {} bytes at offset {}, but only {} bytes remain",
                        length, mOffset, mData.size() - mOffset);
        }
        std::memcpy(dest, mData.data() + mOffset, length);
        mOffset += length;
        return {};
    }

    // Get a subspan of remaining data without advancing
    [[nodiscard]] ParseExpected<std::span<const uint8_t>> PeekBytes(size_t length) const {
        if (mOffset + length > mData.size()) {
            return Fail("Buffer underrun: need {} bytes at offset {}, but only {} bytes remain",
                        length, mOffset, mData.size() - mOffset);
        }
        return mData.subspan(mOffset, length);
    }

    // Skip bytes
    [[nodiscard]] ParseExpected<void> Skip(size_t bytes) {
        if (mOffset + bytes > mData.size()) {
            return Fail("Cannot skip {} bytes at offset {}: only {} bytes remain",
                        bytes, mOffset, mData.size() - mOffset);
        }
        mOffset += bytes;
        return {};
    }

    // Check if we can read N more bytes
    [[nodiscard]] bool CanRead(size_t bytes) const {
        return mOffset + bytes <= mData.size();
    }

    // Get current position
    [[nodiscard]] size_t Offset() const { return mOffset; }

    // Get remaining bytes
    [[nodiscard]] size_t Remaining() const { return mData.size() - mOffset; }

    // Check if at end
    [[nodiscard]] bool AtEnd() const { return mOffset >= mData.size(); }

    // Get a view of remaining data
    [[nodiscard]] std::span<const uint8_t> RemainingSpan() const {
        return mData.subspan(mOffset);
    }

    // Seek to absolute position
    [[nodiscard]] ParseExpected<void> Seek(size_t position) {
        if (position > mData.size()) {
            return Fail("Cannot seek to position {}: buffer size is {}", position, mData.size());
        }
        mOffset = position;
        return {};
    }

private:
    std::span<const uint8_t> mData;
    size_t mOffset;
};

} // namespace DBPF
