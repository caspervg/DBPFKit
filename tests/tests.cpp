#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DBPFReader.h"
#include "DBPFStructures.h"
#include "QFSDecompressor.h"

namespace {

std::vector<uint8_t> SampleQfsPayload() {
    return {
        0x10, 0xFB, 0x00, 0x00, 0x04, // signature + size (big-endian 24-bit)
        0xE0, 'S', 'C', '4', '!',     // literal control block
        0xFC, 0x00                    // terminator
    };
}

struct TestEntry {
    DBPF::Tgi tgi;
    std::vector<uint8_t> data;
};

void WriteUInt32LE(std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
    buffer[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

std::vector<uint8_t> BuildDbpf(const std::vector<TestEntry>& entries) {
    constexpr size_t kHeaderSize = 0x60;
    const size_t indexCount = entries.size();
    size_t totalDataSize = 0;
    for (const auto& entry : entries) {
        totalDataSize += entry.data.size();
    }

    const size_t indexOffset = kHeaderSize + totalDataSize;
    const size_t totalSize = indexOffset + indexCount * 20;

    std::vector<uint8_t> buffer(totalSize, 0);
    buffer[0] = 'D';
    buffer[1] = 'B';
    buffer[2] = 'P';
    buffer[3] = 'F';

    WriteUInt32LE(buffer, 4, 1); // major
    WriteUInt32LE(buffer, 8, 0); // minor
    WriteUInt32LE(buffer, 32, 7); // index type
    WriteUInt32LE(buffer, 36, static_cast<uint32_t>(indexCount));
    WriteUInt32LE(buffer, 40, static_cast<uint32_t>(indexOffset));
    WriteUInt32LE(buffer, 44, static_cast<uint32_t>(indexCount * 20));

    size_t dataCursor = kHeaderSize;
    std::vector<uint32_t> offsets;
    offsets.reserve(entries.size());
    for (const auto& entry : entries) {
        offsets.push_back(static_cast<uint32_t>(dataCursor));
        std::copy(entry.data.begin(), entry.data.end(), buffer.begin() + dataCursor);
        dataCursor += entry.data.size();
    }

    size_t indexCursor = indexOffset;
    for (size_t i = 0; i < entries.size(); ++i) {
        WriteUInt32LE(buffer, indexCursor + 0, entries[i].tgi.type);
        WriteUInt32LE(buffer, indexCursor + 4, entries[i].tgi.group);
        WriteUInt32LE(buffer, indexCursor + 8, entries[i].tgi.instance);
        WriteUInt32LE(buffer, indexCursor + 12, offsets[i]);
        WriteUInt32LE(buffer, indexCursor + 16, static_cast<uint32_t>(entries[i].data.size()));
        indexCursor += 20;
    }

    return buffer;
}

std::vector<uint8_t> BuildDbpf(std::initializer_list<TestEntry> entries) {
    return BuildDbpf(std::vector<TestEntry>(entries));
}

std::vector<uint8_t> BuildDirectoryPayload(const DBPF::Tgi& entryTgi, uint32_t decompressedSize) {
    std::vector<uint8_t> payload(16, 0);
    WriteUInt32LE(payload, 0, entryTgi.type);
    WriteUInt32LE(payload, 4, entryTgi.group);
    WriteUInt32LE(payload, 8, entryTgi.instance);
    WriteUInt32LE(payload, 12, decompressedSize);
    return payload;
}

} // namespace

TEST_CASE("QFS decompressor matches reference literal handling") {
    auto compressed = SampleQfsPayload();
    std::vector<uint8_t> output;
    REQUIRE(QFS::Decompressor::IsQFSCompressed(compressed.data(), compressed.size()));
    REQUIRE(QFS::Decompressor::GetUncompressedSize(compressed.data(), compressed.size()) == 4);
    REQUIRE(QFS::Decompressor::Decompress(compressed.data(), compressed.size(), output));
    REQUIRE(output == std::vector<uint8_t>{'S', 'C', '4', '!'});
}

TEST_CASE("DBPF reader parses uncompressed entries") {
    const DBPF::Tgi tgi{0x00000001, 0x00000002, 0x00000003};
    const std::vector<TestEntry> entries{
        TestEntry{tgi, {'T', 'E', 'S', 'T'}},
    };
    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    const auto& header = reader.GetHeader();
    CHECK(header.majorVersion == 1);
    CHECK(header.minorVersion == 0);
    CHECK(header.indexEntryCount == 1);

    const auto& index = reader.GetIndex();
    REQUIRE(index.size() == 1);
    CHECK(index[0].tgi == tgi);
    CHECK(index[0].offset == 0x60);
    CHECK(index[0].size == 4);

    auto data = reader.ReadEntryData(index[0]);
    REQUIRE(data.has_value());
    REQUIRE(*data == std::vector<uint8_t>{'T', 'E', 'S', 'T'});
}

TEST_CASE("DBPF reader decompresses QFS entries without directory metadata") {
    const DBPF::Tgi tgi{0x11111111, 0x22222222, 0x33333333};
    const std::vector<TestEntry> entries{
        TestEntry{tgi, SampleQfsPayload()},
    };
    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    const auto& index = reader.GetIndex();
    REQUIRE(index.size() == 1);
    auto data = reader.ReadEntryData(index[0]);
    REQUIRE(data.has_value());
    REQUIRE(*data == std::vector<uint8_t>{'S', 'C', '4', '!'});
}

TEST_CASE("DBPF reader applies directory metadata when present") {
    const DBPF::Tgi dataTgi{0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC};
    const TestEntry dataEntry{dataTgi, SampleQfsPayload()};
    const TestEntry dirEntry{DBPF::kDirectoryTgi, BuildDirectoryPayload(dataTgi, 4)};
    auto buffer = BuildDbpf({dataEntry, dirEntry});

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    const auto& index = reader.GetIndex();
    REQUIRE(index.size() == 2);

    auto it = std::find_if(index.begin(), index.end(), [&](const DBPF::IndexEntry& entry) {
        return entry.tgi == dataTgi;
    });
    REQUIRE(it != index.end());
    REQUIRE(it->decompressedSize.has_value());
    CHECK(it->decompressedSize.value() == 4);

    auto data = reader.ReadEntryData(*it);
    REQUIRE(data.has_value());
    REQUIRE(*data == std::vector<uint8_t>{'S', 'C', '4', '!'});
}
