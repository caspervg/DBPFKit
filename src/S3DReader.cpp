#include "S3DReader.h"

#include <algorithm>
#include <cstring>

namespace S3D {

    bool Reader::Parse(std::span<const uint8_t> buffer, Model& outModel) {
        if (buffer.size() < 12) {
            return false;
        }

        const uint8_t* ptr = buffer.data();
        const uint8_t* end = buffer.data() + buffer.size();

        if (!CheckMagic(ptr, end, "3DMD")) {
            return false;
        }

        uint32_t totalLength = 0;
        if (!ReadValue(ptr, end, totalLength)) {
            return false;
        }
        (void)totalLength;

        if (!ParseHEAD(ptr, end, outModel))
            return false;
        if (!ParseVERT(ptr, end, outModel))
            return false;
        if (!ParseINDX(ptr, end, outModel))
            return false;
        if (!ParsePRIM(ptr, end, outModel))
            return false;
        if (!ParseMATS(ptr, end, outModel))
            return false;
        if (!ParseANIM(ptr, end, outModel))
            return false;

        if (!outModel.vertexBuffers.empty()) {
            outModel.bbMin = outModel.vertexBuffers[0].bbMin;
            outModel.bbMax = outModel.vertexBuffers[0].bbMax;

            for (size_t i = 1; i < outModel.vertexBuffers.size(); ++i) {
                const auto& vb = outModel.vertexBuffers[i];
                outModel.bbMin.x = std::min(outModel.bbMin.x, vb.bbMin.x);
                outModel.bbMin.y = std::min(outModel.bbMin.y, vb.bbMin.y);
                outModel.bbMin.z = std::min(outModel.bbMin.z, vb.bbMin.z);
                outModel.bbMax.x = std::max(outModel.bbMax.x, vb.bbMax.x);
                outModel.bbMax.y = std::max(outModel.bbMax.y, vb.bbMax.y);
                outModel.bbMax.z = std::max(outModel.bbMax.z, vb.bbMax.z);
            }
        }

        return true;
    }

    bool Reader::ParseHEAD(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "HEAD")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        if (!ReadValue(ptr, end, model.majorVersion))
            return false;
        if (!ReadValue(ptr, end, model.minorVersion))
            return false;

        if (model.majorVersion != 1 || model.minorVersion < 1 || model.minorVersion > 5) {
            return false;
        }

        return true;
    }

    bool Reader::ParseVERT(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "VERT")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        uint32_t nbrBlocks = 0;
        if (!ReadValue(ptr, end, nbrBlocks))
            return false;

        constexpr uint32_t MAX_VERTEX_BUFFERS = 1000;
        if (nbrBlocks > MAX_VERTEX_BUFFERS) {
            return false;
        }

        model.vertexBuffers.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& vb = model.vertexBuffers[i];

            if (!ReadValue(ptr, end, vb.flags))
                return false;

            uint16_t count = 0;
            if (!ReadValue(ptr, end, count))
                return false;

            uint32_t format = 0;
            uint32_t stride = 0;

            if (model.minorVersion >= 4) {
                if (!ReadValue(ptr, end, format))
                    return false;

                uint8_t coordsNb = 0;
                uint8_t colorsNb = 0;
                uint8_t texsNb = 0;
                DecodeVertexFormat(format, coordsNb, colorsNb, texsNb);
                stride = 3 * 4 * coordsNb + 4 * colorsNb + 2 * 4 * texsNb;
            }
            else {
                uint16_t formatShort = 0;
                uint16_t strideShort = 0;
                if (!ReadValue(ptr, end, formatShort))
                    return false;
                if (!ReadValue(ptr, end, strideShort))
                    return false;
                format = formatShort;
                stride = strideShort;
            }

            vb.format = format;
            vb.vertices.resize(count);

            bool firstVertex = true;

            for (uint16_t v = 0; v < count; ++v) {
                if (!ReadVertex(ptr, end, format, model.minorVersion, stride, vb.vertices[v])) {
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

    bool Reader::ParseINDX(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "INDX")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        uint32_t nbrBlocks = 0;
        if (!ReadValue(ptr, end, nbrBlocks))
            return false;

        constexpr uint32_t MAX_INDEX_BUFFERS = 1000;
        if (nbrBlocks > MAX_INDEX_BUFFERS) {
            return false;
        }

        model.indexBuffers.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& ib = model.indexBuffers[i];

            if (!ReadValue(ptr, end, ib.flags))
                return false;

            uint16_t stride = 0;
            if (!ReadValue(ptr, end, stride))
                return false;

            uint16_t count = 0;
            if (!ReadValue(ptr, end, count))
                return false;

            ib.indices.resize(count);

            for (uint16_t j = 0; j < count; ++j) {
                if (!ReadValue(ptr, end, ib.indices[j])) {
                    return false;
                }
            }
        }

        return true;
    }

    bool Reader::ParsePRIM(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "PRIM")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        uint32_t nbrBlocks = 0;
        if (!ReadValue(ptr, end, nbrBlocks))
            return false;

        constexpr uint32_t MAX_PRIMITIVE_BLOCKS = 1000;
        if (nbrBlocks > MAX_PRIMITIVE_BLOCKS) {
            return false;
        }

        model.primitiveBlocks.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            uint16_t nbrPrims = 0;
            if (!ReadValue(ptr, end, nbrPrims))
                return false;

            model.primitiveBlocks[i].resize(nbrPrims);

            for (uint16_t j = 0; j < nbrPrims; ++j) {
                auto& prim = model.primitiveBlocks[i][j];
                if (!ReadValue(ptr, end, prim.type))
                    return false;
                if (!ReadValue(ptr, end, prim.first))
                    return false;
                if (!ReadValue(ptr, end, prim.length))
                    return false;
            }
        }

        return true;
    }

    bool Reader::ParseMATS(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "MATS")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        uint32_t nbrBlocks = 0;
        if (!ReadValue(ptr, end, nbrBlocks))
            return false;

        constexpr uint32_t MAX_MATERIALS = 1000;
        if (nbrBlocks > MAX_MATERIALS) {
            return false;
        }

        model.materials.resize(nbrBlocks);

        for (uint32_t i = 0; i < nbrBlocks; ++i) {
            auto& mat = model.materials[i];

            if (!ReadValue(ptr, end, mat.flags))
                return false;
            if (!ReadValue(ptr, end, mat.alphaFunc))
                return false;
            if (!ReadValue(ptr, end, mat.depthFunc))
                return false;
            if (!ReadValue(ptr, end, mat.srcBlend))
                return false;
            if (!ReadValue(ptr, end, mat.dstBlend))
                return false;

            uint16_t alphaThresholdInt = 0;
            if (!ReadValue(ptr, end, alphaThresholdInt))
                return false;
            mat.alphaThreshold = alphaThresholdInt / 65535.0f;

            if (!ReadValue(ptr, end, mat.materialClass))
                return false;

            uint8_t reserved = 0;
            if (!ReadValue(ptr, end, reserved))
                return false;

            uint8_t textureCount = 0;
            if (!ReadValue(ptr, end, textureCount))
                return false;

            mat.textures.resize(textureCount);

            for (uint8_t t = 0; t < textureCount; ++t) {
                auto& tex = mat.textures[t];

                if (!ReadValue(ptr, end, tex.textureID))
                    return false;
                if (!ReadValue(ptr, end, tex.wrapS))
                    return false;
                if (!ReadValue(ptr, end, tex.wrapT))
                    return false;

                if (model.minorVersion == 5) {
                    if (!ReadValue(ptr, end, tex.magFilter))
                        return false;
                    if (!ReadValue(ptr, end, tex.minFilter))
                        return false;
                }
                else {
                    tex.magFilter = 0;
                    tex.minFilter = 0;
                }

                if (!ReadValue(ptr, end, tex.animRate))
                    return false;
                if (!ReadValue(ptr, end, tex.animMode))
                    return false;

                uint8_t animNameLen = 0;
                if (!ReadValue(ptr, end, animNameLen))
                    return false;

                if (animNameLen > 0) {
                    tex.animName.resize(animNameLen);
                    if (!ReadBytes(ptr, end, tex.animName.data(), animNameLen))
                        return false;
                }
            }
        }

        return true;
    }

    bool Reader::ParseANIM(const uint8_t*& ptr, const uint8_t* end, Model& model) {
        if (!CheckMagic(ptr, end, "ANIM")) {
            return false;
        }

        uint32_t length = 0;
        if (!ReadValue(ptr, end, length))
            return false;

        auto& anim = model.animation;

        if (!ReadValue(ptr, end, anim.frameCount))
            return false;
        if (!ReadValue(ptr, end, anim.frameRate))
            return false;
        if (!ReadValue(ptr, end, anim.animMode))
            return false;
        if (!ReadValue(ptr, end, anim.flags))
            return false;
        if (!ReadValue(ptr, end, anim.displacement))
            return false;

        uint16_t nbrMeshes = 0;
        if (!ReadValue(ptr, end, nbrMeshes))
            return false;

        anim.animatedMeshes.resize(nbrMeshes);

        for (uint16_t m = 0; m < nbrMeshes; ++m) {
            auto& mesh = anim.animatedMeshes[m];

            uint8_t nameLen = 0;
            if (!ReadValue(ptr, end, nameLen))
                return false;

            if (!ReadValue(ptr, end, mesh.flags))
                return false;

            if (nameLen > 0) {
                mesh.name.resize(nameLen);
                if (!ReadBytes(ptr, end, mesh.name.data(), nameLen))
                    return false;
                if (!mesh.name.empty() && mesh.name.back() == '\0') {
                    mesh.name.pop_back();
                }
            }

            mesh.frames.resize(anim.frameCount);

            for (uint16_t f = 0; f < anim.frameCount; ++f) {
                auto& frame = mesh.frames[f];
                if (!ReadValue(ptr, end, frame.vertBlock))
                    return false;
                if (!ReadValue(ptr, end, frame.indexBlock))
                    return false;
                if (!ReadValue(ptr, end, frame.primBlock))
                    return false;
                if (!ReadValue(ptr, end, frame.matsBlock))
                    return false;
            }
        }

        return true;
    }

    bool Reader::ReadVertex(const uint8_t*& ptr, const uint8_t* end,
                            uint32_t format, uint16_t minorVersion,
                            uint32_t stride, Vertex& outVertex) {
        const uint8_t* vertexStart = ptr;

        uint8_t coordsNb = 0;
        uint8_t colorsNb = 0;
        uint8_t texsNb = 0;
        DecodeVertexFormat(format, coordsNb, colorsNb, texsNb);

        if (!ReadValue(ptr, end, outVertex.position.x))
            return false;
        if (!ReadValue(ptr, end, outVertex.position.y))
            return false;
        if (!ReadValue(ptr, end, outVertex.position.z))
            return false;

        if (colorsNb > 0) {
            uint8_t b = 0, g = 0, r = 0, a = 0;
            if (!ReadValue(ptr, end, b))
                return false;
            if (!ReadValue(ptr, end, g))
                return false;
            if (!ReadValue(ptr, end, r))
                return false;
            if (!ReadValue(ptr, end, a))
                return false;
            outVertex.color = Vec4{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
        }
        else {
            outVertex.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        if (texsNb > 0) {
            if (!ReadValue(ptr, end, outVertex.uv.x))
                return false;
            if (!ReadValue(ptr, end, outVertex.uv.y))
                return false;
        }
        else {
            outVertex.uv = Vec2{0.0f, 0.0f};
        }

        if (texsNb > 1) {
            if (!ReadValue(ptr, end, outVertex.uv2.x))
                return false;
            if (!ReadValue(ptr, end, outVertex.uv2.y))
                return false;
        }
        else {
            outVertex.uv2 = Vec2{0.0f, 0.0f};
        }

        size_t bytesRead = ptr - vertexStart;
        if (bytesRead < stride) {
            if (!SkipBytes(ptr, end, stride - bytesRead))
                return false;
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

    bool Reader::CheckMagic(const uint8_t*& ptr, const uint8_t* end, const char* expected) {
        size_t len = std::strlen(expected);
        if (ptr + len > end)
            return false;
        if (std::memcmp(ptr, expected, len) != 0)
            return false;
        ptr += len;
        return true;
    }

} // namespace S3D
