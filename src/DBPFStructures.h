#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "TGI.h"

namespace DBPF {
    struct IndexEntry {
        Tgi tgi;
        uint32_t offset = 0;
        uint32_t size = 0;
        std::optional<uint32_t> decompressedSize;

        [[nodiscard]] uint32_t GetSize() { return decompressedSize.value_or(size); }

        [[nodiscard]] std::string ToString() {
            return std::format("IndexEntry({0}, {1}, {2})", tgi.ToString(), offset, GetSize());
        }
    };

    struct Entry {
        IndexEntry index;
        std::vector<uint8_t> data;
    };

} // namespace DBPF
