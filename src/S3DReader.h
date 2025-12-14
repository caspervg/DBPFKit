#pragma once

#include "S3DStructures.h"
#include "ParseTypes.h"

#include <cstdint>
#include <cstring>
#include <span>

// Forward declare SafeSpanReader
namespace DBPF {
    class SafeSpanReader;
}

namespace S3D {

    class Reader {
    public:
        static ParseExpected<Record> Parse(std::span<const uint8_t> buffer);

    private:
        static bool ParseHEAD(DBPF::SafeSpanReader& reader, Record& model);
        static bool ParseVERT(DBPF::SafeSpanReader& reader, Record& model);
        static bool ParseINDX(DBPF::SafeSpanReader& reader, Record& model);
        static bool ParsePRIM(DBPF::SafeSpanReader& reader, Record& model);
        static bool ParseMATS(DBPF::SafeSpanReader& reader, Record& model);
        static bool ParseANIM(DBPF::SafeSpanReader& reader, Record& model);

        static bool ReadVertex(DBPF::SafeSpanReader& reader,
                               uint32_t format, uint16_t minorVersion,
                               uint32_t stride, Vertex& outVertex);

        static void DecodeVertexFormat(uint32_t format, uint8_t& coordsNb,
                                       uint8_t& colorsNb, uint8_t& texsNb);

        static bool CheckMagic(DBPF::SafeSpanReader& reader, const char* expected);
    };

} // namespace S3D
