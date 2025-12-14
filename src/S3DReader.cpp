#include "S3DReader.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>

#include "SafeSpanReader.h"

// Helper macro to simplify reading and error checking for integral types
#define READ_VALUE(reader, var) \
    do { \
        using ValueType = std::remove_reference_t<decltype(var)>; \
        auto _tmp = reader.ReadLE<ValueType>(); \
        if (!_tmp) return false; \
        var = *_tmp; \
    } while(0)

// Helper macro for reading float types
#define READ_FLOAT(reader, var) \
    do { \
        auto _tmp = reader.Read<float>(); \
        if (!_tmp) return false; \
        var = *_tmp; \
    } while(0)

namespace S3D {

    constexpr auto kMagic = "3DMD";
    constexpr auto kVersion = "1.5";
    constexpr auto kMagicHead = "HEAD";
    constexpr auto kMagicVert = "VERT";
    constexpr auto kMagicIndx = "INDX";
    constexpr auto kMagicPrim = "PRIM";
    constexpr auto kMagicMats = "MATS";
    constexpr auto kMagicAnim = "ANIM";

    ParseExpected<Record> Reader::Parse(std::span<const uint8_t> buffer) {
        if (buffer.size() < 12) {
            return Fail("S3D buffer too small");
        }

        DBPF::SafeSpanReader reader(buffer);

        if (!CheckMagic(reader, kMagic)) {
            return Fail("Missing 3DMD magic");
        }

        auto totalLength = reader.ReadLE<uint32_t>();
        if (!totalLength) {
            return Fail("Failed to read S3D length");
        }
        (void)totalLength;

        Record model;
        if (!ParseHEAD(reader, model))
            return Fail("Failed to parse HEAD chunk");
        if (!ParseVERT(reader, model))
            return Fail("Failed to parse VERT chunk");
        if (!ParseINDX(reader, model))
            return Fail("Failed to parse INDX chunk");
        if (!ParsePRIM(reader, model))
            return Fail("Failed to parse PRIM chunk");
        if (!ParseMATS(reader, model))
            return Fail("Failed to parse MATS chunk");
        if (!ParseANIM(reader, model))
            return Fail("Failed to parse ANIM chunk");

        if (!model.vertexBuffers.empty()) {
            model.bbMin = model.vertexBuffers[0].bbMin;
            model.bbMax = model.vertexBuffers[0].bbMax;

            for (size_t i = 1; i < model.vertexBuffers.size(); ++i) {
                const auto& vb = model.vertexBuffers[i];
                model.bbMin.x = std::min(model.bbMin.x, vb.bbMin.x);
                model.bbMin.y = std::min(model.bbMin.y, vb.bbMin.y);
                model.bbMin.z = std::min(model.bbMin.z, vb.bbMin.z);
                model.bbMax.x = std::max(model.bbMax.x, vb.bbMax.x);
                model.bbMax.y = std::max(model.bbMax.y, vb.bbMax.y);
                model.bbMax.z = std::max(model.bbMax.z, vb.bbMax.z);
            }
        }

        return model;
    }

    bool Reader::ParseHEAD(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicHead)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        READ_VALUE(reader, model.majorVersion);
        READ_VALUE(reader, model.minorVersion);

        if (model.majorVersion != 1 || model.minorVersion < 1 || model.minorVersion > 5) {
            return false;
        }

        return true;
    }

    bool Reader::ParseVERT(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicVert)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        uint32_t nbrBlocks = 0;
        READ_VALUE(reader, nbrBlocks);

        constexpr uint32_t MAX_VERTEX_BUFFERS = 1000;
        if (nbrBlocks > MAX_VERTEX_BUFFERS) {
            return false;
        }

        model.vertexBuffers.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& vb = model.vertexBuffers[i];

            READ_VALUE(reader, vb.flags);

            uint16_t count = 0;
            READ_VALUE(reader, count);

            uint32_t format = 0;
            uint32_t stride = 0;

            if (model.minorVersion >= 4) {
                READ_VALUE(reader, format);

                uint8_t coordsNb = 0;
                uint8_t colorsNb = 0;
                uint8_t texsNb = 0;
                DecodeVertexFormat(format, coordsNb, colorsNb, texsNb);
                stride = 3 * 4 * coordsNb + 4 * colorsNb + 2 * 4 * texsNb;
            }
            else {
                uint16_t formatShort = 0;
                uint16_t strideShort = 0;
                READ_VALUE(reader, formatShort);
                READ_VALUE(reader, strideShort);
                format = formatShort;
                stride = strideShort;
            }

            vb.format = format;
            vb.vertices.resize(count);

            bool firstVertex = true;

            for (uint16_t v = 0; v < count; ++v) {
                if (!ReadVertex(reader, format, model.minorVersion, stride, vb.vertices[v])) {
                    return false;
                }

                const auto& pos = vb.vertices[v].position;
                if (firstVertex) {
                    vb.bbMin = vb.bbMax = pos;
                    firstVertex = false;
                }
                else {
                    vb.bbMin.x = std::min(vb.bbMin.x, pos.x);
                    vb.bbMin.y = std::min(vb.bbMin.y, pos.y);
                    vb.bbMin.z = std::min(vb.bbMin.z, pos.z);
                    vb.bbMax.x = std::max(vb.bbMax.x, pos.x);
                    vb.bbMax.y = std::max(vb.bbMax.y, pos.y);
                    vb.bbMax.z = std::max(vb.bbMax.z, pos.z);
                }
            }
        }

        return true;
    }

    bool Reader::ParseINDX(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicIndx)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        uint32_t nbrBlocks = 0;
        READ_VALUE(reader, nbrBlocks);

        constexpr uint32_t MAX_INDEX_BUFFERS = 1000;
        if (nbrBlocks > MAX_INDEX_BUFFERS) {
            return false;
        }

        model.indexBuffers.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& ib = model.indexBuffers[i];

            READ_VALUE(reader, ib.flags);

            uint16_t stride = 0;
            READ_VALUE(reader, stride);

            uint16_t count = 0;
            READ_VALUE(reader, count);

            ib.indices.resize(count);

            for (uint16_t j = 0; j < count; ++j) {
                READ_VALUE(reader, ib.indices[j]);
            }
        }

        return true;
    }

    bool Reader::ParsePRIM(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicPrim)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        uint32_t nbrBlocks = 0;
        READ_VALUE(reader, nbrBlocks);

        constexpr uint32_t MAX_PRIMITIVE_BLOCKS = 1000;
        if (nbrBlocks > MAX_PRIMITIVE_BLOCKS) {
            return false;
        }

        model.primitiveBlocks.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            uint16_t nbrPrims = 0;
            READ_VALUE(reader, nbrPrims);

            model.primitiveBlocks[i].resize(nbrPrims);

            for (uint16_t j = 0; j < nbrPrims; ++j) {
                auto& prim = model.primitiveBlocks[i][j];
                READ_VALUE(reader, prim.type);
                READ_VALUE(reader, prim.first);
                READ_VALUE(reader, prim.length);
            }
        }

        return true;
    }

    bool Reader::ParseMATS(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicMats)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        uint32_t nbrBlocks = 0;
        READ_VALUE(reader, nbrBlocks);

        constexpr uint32_t MAX_MATERIALS = 1000;
        if (nbrBlocks > MAX_MATERIALS) {
            return false;
        }

        model.materials.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& mat = model.materials[i];

            READ_VALUE(reader, mat.flags);
            READ_VALUE(reader, mat.alphaFunc);
            READ_VALUE(reader, mat.depthFunc);
            READ_VALUE(reader, mat.srcBlend);
            READ_VALUE(reader, mat.dstBlend);

            uint16_t alphaThresholdInt = 0;
            READ_VALUE(reader, alphaThresholdInt);
            mat.alphaThreshold = alphaThresholdInt / 65535.0f;

            READ_VALUE(reader, mat.materialClass);

            uint8_t reserved = 0;
            READ_VALUE(reader, reserved);

            uint8_t textureCount = 0;
            READ_VALUE(reader, textureCount);

            mat.textures.resize(textureCount);

            for (uint8_t t = 0; t < textureCount; ++t) {
                auto& tex = mat.textures[t];

                READ_VALUE(reader, tex.textureID);
                READ_VALUE(reader, tex.wrapS);
                READ_VALUE(reader, tex.wrapT);

                if (model.minorVersion == 5) {
                    READ_VALUE(reader, tex.magFilter);
                    READ_VALUE(reader, tex.minFilter);
                }
                else {
                    tex.magFilter = 0;
                    tex.minFilter = 0;
                }

                READ_VALUE(reader, tex.animRate);
                READ_VALUE(reader, tex.animMode);

                uint8_t animNameLen = 0;
                READ_VALUE(reader, animNameLen);

                if (animNameLen > 0) {
                    tex.animName.resize(animNameLen);
                    auto readBytes = reader.ReadBytes(tex.animName.data(), animNameLen);
                    if (!readBytes) return false;
                }
            }
        }

        return true;
    }

    bool Reader::ParseANIM(DBPF::SafeSpanReader& reader, Record& model) {
        if (!CheckMagic(reader, kMagicAnim)) {
            return false;
        }

        uint32_t length = 0;
        READ_VALUE(reader, length);

        auto& anim = model.animation;

        READ_VALUE(reader, anim.frameCount);
        READ_VALUE(reader, anim.frameRate);
        READ_VALUE(reader, anim.animMode);
        READ_VALUE(reader, anim.flags);
        READ_FLOAT(reader, anim.displacement);

        uint16_t nbrMeshes = 0;
        READ_VALUE(reader, nbrMeshes);

        anim.animatedMeshes.resize(nbrMeshes);

        for (uint16_t m = 0; m < nbrMeshes; ++m) {
            auto& mesh = anim.animatedMeshes[m];

            uint8_t nameLen = 0;
            READ_VALUE(reader, nameLen);

            READ_VALUE(reader, mesh.flags);

            if (nameLen > 0) {
                mesh.name.resize(nameLen);
                auto readBytes = reader.ReadBytes(mesh.name.data(), nameLen);
                if (!readBytes) return false;
                if (!mesh.name.empty() && mesh.name.back() == '\0') {
                    mesh.name.pop_back();
                }
            }

            mesh.frames.resize(anim.frameCount);

            for (uint16_t f = 0; f < anim.frameCount; ++f) {
                auto& frame = mesh.frames[f];
                READ_VALUE(reader, frame.vertBlock);
                READ_VALUE(reader, frame.indexBlock);
                READ_VALUE(reader, frame.primBlock);
                READ_VALUE(reader, frame.matsBlock);
            }
        }

        return true;
    }

    bool Reader::ReadVertex(DBPF::SafeSpanReader& reader,
                            uint32_t format, uint16_t minorVersion,
                            uint32_t stride, Vertex& outVertex) {
        size_t startOffset = reader.Offset();

        uint8_t coordsNb = 0;
        uint8_t colorsNb = 0;
        uint8_t texsNb = 0;
        DecodeVertexFormat(format, coordsNb, colorsNb, texsNb);

        READ_FLOAT(reader, outVertex.position.x);
        READ_FLOAT(reader, outVertex.position.y);
        READ_FLOAT(reader, outVertex.position.z);

        if (colorsNb > 0) {
            uint8_t b = 0, g = 0, r = 0, a = 0;
            READ_VALUE(reader, b);
            READ_VALUE(reader, g);
            READ_VALUE(reader, r);
            READ_VALUE(reader, a);
            outVertex.color = Vec4{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
        }
        else {
            outVertex.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        if (texsNb > 0) {
            READ_FLOAT(reader, outVertex.uv.x);
            READ_FLOAT(reader, outVertex.uv.y);
        }
        else {
            outVertex.uv = Vec2{0.0f, 0.0f};
        }

        if (texsNb > 1) {
            READ_FLOAT(reader, outVertex.uv2.x);
            READ_FLOAT(reader, outVertex.uv2.y);
        }
        else {
            outVertex.uv2 = Vec2{0.0f, 0.0f};
        }

        size_t bytesRead = reader.Offset() - startOffset;
        if (bytesRead < stride) {
            auto skip = reader.Skip(stride - bytesRead);
            if (!skip) return false;
        }

        (void)minorVersion;
        return true;
    }

    void Reader::DecodeVertexFormat(uint32_t format, uint8_t& coordsNb,
                                    uint8_t& colorsNb, uint8_t& texsNb) {
        if (format & 0x80000000) {
            coordsNb = format & 0x3;
            colorsNb = (format >> 8) & 0x3;
            texsNb = (format >> 14) & 0x3;
        }
        else {
            switch (format) {
            case 1:
                coordsNb = 1;
                colorsNb = 1;
                texsNb = 0;
                break;
            case 2:
                coordsNb = 1;
                colorsNb = 0;
                texsNb = 1;
                break;
            case 3:
                coordsNb = 1;
                colorsNb = 0;
                texsNb = 2;
                break;
            case 10:
                coordsNb = 1;
                colorsNb = 1;
                texsNb = 1;
                break;
            case 11:
                coordsNb = 1;
                colorsNb = 1;
                texsNb = 2;
                break;
            default:
                coordsNb = 1;
                colorsNb = 0;
                texsNb = 1;
                break;
            }
        }
    }

    bool Reader::CheckMagic(DBPF::SafeSpanReader& reader, const char* expected) {
        const size_t len = std::strlen(expected);
        auto bytes = reader.PeekBytes(len);
        if (!bytes) return false;
        if (std::memcmp(bytes->data(), expected, len) != 0)
            return false;
        auto skip = reader.Skip(len);
        return skip.has_value();
    }

#undef READ_VALUE
#undef READ_FLOAT

} // namespace S3D
