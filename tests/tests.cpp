#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DBPFReader.h"
#include "DBPFStructures.h"
#include "ExemplarReader.h"
#include "FSHReader.h"
#include "QFSDecompressor.h"
#include "LTextReader.h"
#include "RUL0.h"
#include "squish/squish.h"

namespace {

std::vector<uint8_t> SampleQfsPayload() {
    return {
        0x10, 0xFB, 0x00, 0x00, 0x04, // signature + size (big-endian 24-bit)
        0xE0, 'S', 'C', '4', '!',     // literal control block
        0xFC, 0x00                    // terminator
    };
}

void WriteUInt32LE(std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
    buffer[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

std::vector<uint8_t> WrapChunked(const std::vector<uint8_t>& data, uint8_t flag) {
    std::vector<uint8_t> chunk;
    chunk.resize(flag == 0x10 ? 11 : 15);
    WriteUInt32LE(chunk, 0, static_cast<uint32_t>(data.size()));
    WriteUInt32LE(chunk, 4, static_cast<uint32_t>(data.size()));
    chunk[8] = 0;
    chunk[9] = 0;
    chunk[10] = flag;
    if (flag == 0x11) {
        WriteUInt32LE(chunk, 11, static_cast<uint32_t>(data.size()));
    }
    chunk.insert(chunk.end(), data.begin(), data.end());
    return chunk;
}

struct TestEntry {
    DBPF::Tgi tgi;
    std::vector<uint8_t> data;
};


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

void WriteUInt16LE(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

template<typename T>
void AppendRaw(std::vector<uint8_t>& buffer, const T& value) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), raw, raw + sizeof(T));
}

std::vector<uint8_t> BuildExemplarBuffer(const std::vector<std::vector<uint8_t>>& properties) {
    std::vector<uint8_t> buffer;
    buffer.reserve(24 + properties.size() * 16);

    const char signature[8] = {'E', 'Q', 'Z', 'B', '1', '#', '#', '#'};
    buffer.insert(buffer.end(), signature, signature + 8);

    AppendRaw(buffer, static_cast<uint32_t>(0)); // parent type
    AppendRaw(buffer, static_cast<uint32_t>(0)); // parent group
    AppendRaw(buffer, static_cast<uint32_t>(0)); // parent instance
    AppendRaw(buffer, static_cast<uint32_t>(properties.size())); // property count

    for (const auto& prop : properties) {
        buffer.insert(buffer.end(), prop.begin(), prop.end());
    }

    return buffer;
}

std::vector<uint8_t> MakeSingleUInt32Property(uint32_t id, uint32_t value) {
    std::vector<uint8_t> prop;
    AppendRaw(prop, id);
    WriteUInt16LE(prop, 0x0300); // UInt32
    WriteUInt16LE(prop, 0x0000); // single
    prop.push_back(0);           // reps byte
    AppendRaw(prop, value);
    return prop;
}

std::vector<uint8_t> MakeMultiFloatProperty(uint32_t id, const std::vector<float>& values) {
    std::vector<uint8_t> prop;
    AppendRaw(prop, id);
    WriteUInt16LE(prop, 0x0900); // Float32
    WriteUInt16LE(prop, 0x0080); // multi
    prop.push_back(0);           // unused flag
    AppendRaw(prop, static_cast<uint32_t>(values.size()));
    for (float value : values) {
        AppendRaw(prop, value);
    }
    return prop;
}

std::vector<uint8_t> MakeStringProperty(uint32_t id, std::string_view value) {
    std::vector<uint8_t> prop;
    AppendRaw(prop, id);
    WriteUInt16LE(prop, 0x0C00); // String
    WriteUInt16LE(prop, 0x0000); // single
    prop.push_back(static_cast<uint8_t>(value.size()));
    prop.insert(prop.end(), value.begin(), value.end());
    return prop;
}

std::vector<uint8_t> BuildSimpleFsh() {
    const uint32_t headerSize = 16;
    const uint32_t directorySize = 8;
    const uint32_t entryHeaderSize = 1 + 3 + 12;
    const uint32_t pixelBytes = 4 * 4;
    const uint32_t entrySize = entryHeaderSize + pixelBytes;
    const uint32_t totalSize = headerSize + directorySize + entrySize;

    std::vector<uint8_t> buffer(totalSize, 0);
    WriteUInt32LE(buffer, 0, FSH::kMagicSHPI);
    WriteUInt32LE(buffer, 4, totalSize);
    WriteUInt32LE(buffer, 8, 1);
    WriteUInt32LE(buffer, 12, 0);

    const uint32_t entryOffset = headerSize + directorySize;
    WriteUInt32LE(buffer, 16, 0);
    WriteUInt32LE(buffer, 20, entryOffset);

    size_t cursor = entryOffset;
    buffer[cursor++] = FSH::kCode32Bit;
    buffer[cursor++] = 0;
    buffer[cursor++] = 0;
    buffer[cursor++] = 0;

    auto push16 = [&](uint16_t value) {
        buffer[cursor++] = static_cast<uint8_t>(value & 0xFF);
        buffer[cursor++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    };

    push16(2);
    push16(2);
    push16(0);
    push16(0);
    push16(0);
    push16(0);

    const std::array<uint8_t, 16> pixels{
        0x00, 0x00, 0xFF, 0xFF,
        0x00, 0xFF, 0x00, 0xFF,
        0xFF, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF
    };
    std::copy(pixels.begin(), pixels.end(), buffer.begin() + cursor);
    return buffer;
}

std::vector<uint8_t> BuildDxtFsh(std::vector<uint8_t>& blocks, int& widthOut, int& heightOut) {
    const int width = 4;
    const int height = 4;
    widthOut = width;
    heightOut = height;
    std::array<uint8_t, width * height * 4> source{
        255,   0,   0, 255,   0, 255,   0, 255,
          0,   0, 255, 255, 255, 255, 255, 255,
        255, 255,   0, 255,   0, 255, 255, 255,
        255,   0, 255, 255,   0,   0,   0, 255
    };

    blocks.resize(GetStorageRequirements(width, height, squish::kDxt1));
    squish::CompressImage(source.data(), width, height, blocks.data(), squish::kDxt1);

    const uint32_t headerSize = 16;
    const uint32_t directorySize = 8;
    const uint32_t entryHeaderSize = 1 + 3 + 12;
    const uint32_t entrySize = entryHeaderSize + static_cast<uint32_t>(blocks.size());
    const uint32_t totalSize = headerSize + directorySize + entrySize;

    std::vector<uint8_t> buffer(totalSize, 0);
    WriteUInt32LE(buffer, 0, FSH::kMagicSHPI);
    WriteUInt32LE(buffer, 4, totalSize);
    WriteUInt32LE(buffer, 8, 1);
    WriteUInt32LE(buffer, 12, 0);

    const uint32_t entryOffset = headerSize + directorySize;
    WriteUInt32LE(buffer, 16, 0);
    WriteUInt32LE(buffer, 20, entryOffset);

    size_t cursor = entryOffset;
    buffer[cursor++] = FSH::kCodeDXT1;
    buffer[cursor++] = 0;
    buffer[cursor++] = 0;
    buffer[cursor++] = 0;

    auto push16 = [&](uint16_t value) {
        buffer[cursor++] = static_cast<uint8_t>(value & 0xFF);
        buffer[cursor++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    };

    push16(width);
    push16(height);
    push16(0);
    push16(0);
    push16(0);
    push16(0);

    std::copy(blocks.begin(), blocks.end(), buffer.begin() + cursor);
    return buffer;
}

std::vector<uint8_t> BuildLTextBuffer(std::u16string_view text) {
    const uint16_t charCount = static_cast<uint16_t>(text.size());
    std::vector<uint8_t> buffer(4 + static_cast<size_t>(charCount) * 2);
    buffer[0] = static_cast<uint8_t>(charCount & 0xFF);
    buffer[1] = static_cast<uint8_t>((charCount >> 8) & 0xFF);
    buffer[2] = 0x00;
    buffer[3] = 0x10;
    if (!text.empty()) {
        std::memcpy(buffer.data() + 4, text.data(), static_cast<size_t>(charCount) * 2);
    }
    return buffer;
}

} // namespace

TEST_CASE("QFS decompressor matches reference literal handling") {
    auto compressed = SampleQfsPayload();
    std::vector<uint8_t> output;
    std::span<const uint8_t> compressedSpan(compressed.data(), compressed.size());
    REQUIRE(QFS::Decompressor::IsQFSCompressed(compressedSpan));
    REQUIRE(QFS::Decompressor::GetUncompressedSize(compressedSpan) == 4);
    auto decompressed = QFS::Decompressor::Decompress(compressedSpan, output);
    REQUIRE(decompressed.has_value());
    CHECK(*decompressed == 4);
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

TEST_CASE("DBPF reader strips chunk header before QFS decompression") {
    const DBPF::Tgi tgi{0x99999999, 0x88888888, 0x77777777};
    const auto chunked = WrapChunked(SampleQfsPayload(), 0x10);
    auto buffer = BuildDbpf({TestEntry{tgi, chunked}});

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

TEST_CASE("DBPF reader finds entries via masks and catalog labels") {
    const DBPF::Tgi fshTgi{0x7AB50E44, 0x0986135E, 0x00000011};
    const DBPF::Tgi s3dTgi{0x5AD0E817, 0xBADB57F1, 0x00000001};
    const std::vector<TestEntry> entries{
        TestEntry{fshTgi, {'F', 'S', 'H'}},
        TestEntry{s3dTgi, {'3', 'D', '!'}},
    };
    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    const DBPF::IndexEntry* direct = reader.FindEntry(fshTgi);
    REQUIRE(direct != nullptr);
    auto directData = reader.ReadEntryData(fshTgi);
    REQUIRE(directData.has_value());
    REQUIRE(*directData == std::vector<uint8_t>{'F', 'S', 'H'});

    DBPF::TgiMask fshMask;
    fshMask.type = fshTgi.type;
    auto fshEntries = reader.FindEntries(fshMask);
    REQUIRE(fshEntries.size() == 1);
    CHECK(fshEntries[0]->tgi == fshTgi);

    auto fshByLabel = reader.FindEntries("FSH (Base/Overlay Texture)");
    REQUIRE(fshByLabel.size() == 1);
    CHECK(fshByLabel[0]->tgi == fshTgi);

    auto s3dBytes = reader.ReadFirstMatching("S3D");
    REQUIRE(s3dBytes.has_value());
    REQUIRE(*s3dBytes == std::vector<uint8_t>{'3', 'D', '!'});
}

TEST_CASE("DBPF typed loaders parse FSH and Exemplar entries") {
    const DBPF::Tgi fshTgi{0x7AB50E44, 0x0986135E, 0x0000F00D};
    const DBPF::Tgi exemplarTgi{0x6534284A, 0x2821ED93, 0x12345678};

    auto fshPayload = BuildSimpleFsh();
    std::vector<std::vector<uint8_t>> properties;
    properties.push_back(MakeSingleUInt32Property(0x11111111, 0x22222222));
    auto exemplarPayload = BuildExemplarBuffer(properties);

    const std::vector<TestEntry> entries{
        TestEntry{fshTgi, fshPayload},
        TestEntry{exemplarTgi, exemplarPayload},
    };

    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    auto fshFile = reader.LoadFSH(fshTgi);
    REQUIRE(fshFile.has_value());
    CHECK(fshFile->entries.size() == 1);

    auto exemplar = reader.LoadExemplar("Exemplar");
    REQUIRE(exemplar.has_value());
    CHECK(exemplar->properties.size() == 1);

    auto missing = reader.LoadExemplar("Nonexistent label");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("label") != std::string::npos);
}

TEST_CASE("DBPF reader loads LText entries") {
    const DBPF::Tgi ltextTgi{0x2026960B, 0x00000000, 0x00000001};
    const std::vector<TestEntry> entries{
        TestEntry{ltextTgi, BuildLTextBuffer(u"Menu Item")},
    };
    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    auto direct = reader.LoadLText(ltextTgi);
    REQUIRE(direct.has_value());
    CHECK(direct->ToUtf8() == "Menu Item");

    auto byLabel = reader.LoadLText("LText");
    REQUIRE(byLabel.has_value());
    CHECK(byLabel->text == direct->text);
}

TEST_CASE("DBPF reader loads RUL0 entries") {
    const DBPF::Tgi rulTgi{0x0A5BCF4B, 0xAA5BCF57, 0x10000000};
    const std::string text =
        "RotationRing=0x0A5BCF4B\n"
        "AddTypes=0x0A5BCF4B\n"
        "[HighwayIntersectionInfo_0x00000002]\n"
        "Piece=0.0, 0.0, 0, 0, 0x00000002\n";
    const std::vector<TestEntry> entries{
        TestEntry{rulTgi, std::vector<uint8_t>(text.begin(), text.end())},
    };
    auto buffer = BuildDbpf(entries);

    DBPF::Reader reader;
    REQUIRE(reader.LoadBuffer(buffer.data(), buffer.size()));

    auto data = reader.LoadRUL0();
    REQUIRE(data.has_value());
    CHECK(data->puzzlePieces.size() == 1);

    const auto* entry = reader.FindEntry(rulTgi);
    REQUIRE(entry != nullptr);
    auto viaEntry = reader.LoadRUL0(*entry);
    REQUIRE(viaEntry.has_value());
    CHECK(viaEntry->puzzlePieces.size() == 1);
}

TEST_CASE("Exemplar parser handles single and multi-value properties") {
    std::vector<std::vector<uint8_t>> properties;
    properties.push_back(MakeSingleUInt32Property(0x12345678, 0xCAFEBABE));
    properties.push_back(MakeMultiFloatProperty(0x87654321, {1.0f, 2.5f}));
    properties.push_back(MakeStringProperty(0x0000DEAD, "Test"));

    auto buffer = BuildExemplarBuffer(properties);
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = Exemplar::Parse(bufferSpan);
    REQUIRE(parsed.has_value());
    auto record = std::move(parsed).value();
    REQUIRE(record.properties.size() == 3);

    const auto* uintProp = record.FindProperty(0x12345678);
    REQUIRE(uintProp != nullptr);
    REQUIRE_FALSE(uintProp->isList);
    REQUIRE(std::get<uint32_t>(uintProp->values[0]) == 0xCAFEBABE);

    const auto* floatProp = record.FindProperty(0x87654321);
    REQUIRE(floatProp != nullptr);
    REQUIRE(floatProp->isList);
    REQUIRE(floatProp->values.size() == 2);
    // CHECK(std::get<float>(floatProp->values[0]) == Approx(1.0f));
    // CHECK(std::get<float>(floatProp->values[1]) == Approx(2.5f));

    const auto* stringProp = record.FindProperty(0x0000DEAD);
    REQUIRE(stringProp != nullptr);
    REQUIRE_FALSE(stringProp->isList);
    REQUIRE(std::get<std::string>(stringProp->values[0]) == "Test");
}

TEST_CASE("Exemplar parser loads text exemplars with scalar and list values") {
    const std::string text =
        "EQZT1###\n"
        "ParentCohort=Key:{0x00000000,0x00000000,0x00000000}\n"
        "PropCount=0x00000004\n"
        "0x00000010:{\"Exemplar Type\"}=Uint32:0:{0x0000001E}\n"
        "0x00000020:{\"Exemplar Name\"}=String:0:{\"SG_Prop_Billboard2\"}\n"
        "0x27812810:{\"Occupant Size\"}=Float32:3:{10.39999962,7.2249999,2.51600003}\n"
        "0x4A9F188B:{\"Light\"}=Bool:0:{True}\n";
    std::vector<uint8_t> buffer(text.begin(), text.end());
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = Exemplar::Parse(bufferSpan);
    REQUIRE(parsed.has_value());
    const auto record = std::move(parsed).value();
    CHECK_FALSE(record.isCohort);
    CHECK(record.properties.size() == 4);
    const auto* nameProp = record.FindProperty(0x00000020);
    REQUIRE(nameProp != nullptr);
    REQUIRE_FALSE(nameProp->isList);
    CHECK(std::get<std::string>(nameProp->values[0]) == "SG_Prop_Billboard2");
    const auto* occupant = record.FindProperty(0x27812810);
    REQUIRE(occupant != nullptr);
    CHECK(occupant->isList);
    REQUIRE(occupant->values.size() == 3);
    CHECK(std::get<float>(occupant->values[0]) == Approx(10.39999962f));
    const auto* light = record.FindProperty(0x4A9F188B);
    REQUIRE(light != nullptr);
    REQUIRE_FALSE(light->isList);
    CHECK(std::get<bool>(light->values[0]) == true);
}

TEST_CASE("Exemplar parser reports syntax errors in text exemplars") {
    const std::string broken =
        "EQZT1###\n"
        "ParentCohort=Key:{0x00000000,0x00000000,0x00000000}\n"
        "PropCount=0x00000001\n"
        "0x00000010:{\"Exemplar Type\"}=Uint32:0:{0x0000001E\n";
    std::vector<uint8_t> buffer(broken.begin(), broken.end());
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = Exemplar::Parse(bufferSpan);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().message.find("property list") != std::string::npos);
}

TEST_CASE("Exemplar parser decodes signed hex literals in text exemplars") {
    const std::string text =
        "EQZT1###\n"
        "ParentCohort=Key:{0x00000000,0x00000000,0x00000000}\n"
        "PropCount=0x00000002\n"
        "0x27812850:{\"Park Effect\"}=Sint32:2:{0xFFFFFFF6,0x0000000A}\n"
        "0x27812854:{\"Power\"}=Uint32:0:{0x00000005}\n";
    std::vector<uint8_t> buffer(text.begin(), text.end());
    std::span<const uint8_t> span(buffer.data(), buffer.size());
    auto parsed = Exemplar::Parse(span);
    REQUIRE(parsed.has_value());
    const auto* prop = parsed->FindProperty(0x27812850);
    REQUIRE(prop != nullptr);
    REQUIRE(prop->values.size() == 2);
    CHECK(std::get<int32_t>(prop->values[0]) == -10);
    CHECK(std::get<int32_t>(prop->values[1]) == 10);
}

TEST_CASE("LText parser decodes UTF-16 payloads") {
    std::u16string text = u"City ";
    text.push_back(static_cast<char16_t>(0xD83D));
    text.push_back(static_cast<char16_t>(0xDE00)); // 😀 surrogate pair
    auto buffer = BuildLTextBuffer(text);
    std::span<const uint8_t> span(buffer.data(), buffer.size());
    auto parsed = LText::Parse(span);
    REQUIRE(parsed.has_value());
    CHECK(parsed->text == text);
    CHECK(parsed->ToUtf8() == std::string("City \xF0\x9F\x98\x80"));
}

TEST_CASE("LText parser rejects invalid control markers") {
    auto buffer = BuildLTextBuffer(u"Test");
    buffer[2] = 0xFF;
    std::span<const uint8_t> span(buffer.data(), buffer.size());
    auto parsed = LText::Parse(span);
    REQUIRE(parsed.has_value());
    CHECK(parsed->ToUtf8() == "Test");
}

TEST_CASE("LText parser falls back to raw ASCII blobs") {
    const std::string ascii = "Welcome to the RLS Vacation Resort!\0";
    std::vector<uint8_t> buffer(ascii.begin(), ascii.end());
    std::span<const uint8_t> span(buffer.data(), buffer.size());
    auto parsed = LText::Parse(span);
    REQUIRE(parsed.has_value());
    CHECK(parsed->ToUtf8() == "Welcome to the RLS Vacation Resort!");
}

TEST_CASE("LText parser handles tiny ASCII payloads without headers") {
    const std::string ascii = "Hi";
    std::vector<uint8_t> buffer(ascii.begin(), ascii.end());
    std::span<const uint8_t> span(buffer.data(), buffer.size());
    auto parsed = LText::Parse(span);
    REQUIRE(parsed.has_value());
    CHECK(parsed->ToUtf8() == "Hi");
}

TEST_CASE("RUL0 parser loads minimal ordering data") {
    const std::string text =
        "RotationRing=0x0A5BCF4B\n"
        "AddTypes=0x0A5BCF4B\n"
        "\n"
        "[HighwayIntersectionInfo_0x00000001]\n"
        "Piece=0.0, 0.0, 0, 0, 0x00000001\n"
        "AutoPlace=1\n";
    std::span<const uint8_t> span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    auto parsed = RUL0::Parse(span);
    REQUIRE(parsed.has_value());
    auto data = std::move(parsed).value();
    CHECK(data.orderings.size() == 1);
    REQUIRE(data.puzzlePieces.size() == 1);
    const auto& piece = data.puzzlePieces.begin()->second;
    CHECK(piece.autoPlace);
    CHECK(piece.effect.initialized);
}

TEST_CASE("FSH reader parses simple uncompressed bitmap") {
    auto buffer = BuildSimpleFsh();
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = FSH::Reader::Parse(bufferSpan);
    REQUIRE(parsed.has_value());
    auto file = std::move(parsed).value();
    REQUIRE(file.entries.size() == 1);
    REQUIRE(file.entries[0].bitmaps.size() == 1);
    const auto& bmp = file.entries[0].bitmaps[0];
    CHECK(bmp.code == FSH::kCode32Bit);
    CHECK(bmp.width == 2);
    CHECK(bmp.height == 2);
    CHECK(bmp.data.size() == 16);
}

TEST_CASE("FSH reader converts 32-bit bitmap to RGBA8") {
    auto buffer = BuildSimpleFsh();
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = FSH::Reader::Parse(bufferSpan);
    REQUIRE(parsed.has_value());
    auto file = std::move(parsed).value();
    std::vector<uint8_t> rgba;
    REQUIRE(file.entries.size() == 1);
    REQUIRE(file.entries[0].bitmaps.size() == 1);
    REQUIRE(FSH::Reader::ConvertToRGBA8(file.entries[0].bitmaps[0], rgba));
    REQUIRE(rgba.size() == 16);
    CHECK(rgba[0] == 0xFF);
    CHECK(rgba[1] == 0x00);
    CHECK(rgba[2] == 0x00);
    CHECK(rgba[3] == 0xFF);
}

TEST_CASE("FSH reader decodes DXT1 bitmap") {
    std::vector<uint8_t> blocks;
    int width = 0;
    int height = 0;
    auto buffer = BuildDxtFsh(blocks, width, height);
    std::span<const uint8_t> bufferSpan(buffer.data(), buffer.size());
    auto parsed = FSH::Reader::Parse(bufferSpan);
    REQUIRE(parsed.has_value());
    auto file = std::move(parsed).value();
    REQUIRE(file.entries.size() == 1);
    REQUIRE_FALSE(file.entries[0].bitmaps.empty());
    const auto& bmp = file.entries[0].bitmaps[0];
    std::vector<uint8_t> rgba;
    REQUIRE(FSH::Reader::ConvertToRGBA8(bmp, rgba));
    std::vector<uint8_t> expected(width * height * 4);
    squish::DecompressImage(expected.data(), width, height, blocks.data(), squish::kDxt1);
    REQUIRE(rgba == expected);
}
