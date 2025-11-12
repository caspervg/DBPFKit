#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace S3D {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Vec4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct Vertex {
        Vec3 position;
        Vec4 color;
        Vec2 uv;
        Vec2 uv2;
    };

    struct VertexBuffer {
        std::vector<Vertex> vertices;
        uint16_t flags = 0;
        uint32_t format = 0;
        Vec3 bbMin;
        Vec3 bbMax;
    };

    struct IndexBuffer {
        std::vector<uint16_t> indices;
        uint16_t flags = 0;
    };

    struct Primitive {
        uint32_t type = 0;
        uint32_t first = 0;
        uint32_t length = 0;
    };

    using PrimitiveBlock = std::vector<Primitive>;

    struct MaterialTexture {
        uint32_t textureID = 0;
        uint8_t wrapS = 0;
        uint8_t wrapT = 0;
        uint8_t magFilter = 0;
        uint8_t minFilter = 0;
        uint16_t animRate = 0;
        uint16_t animMode = 0;
        std::string animName;
    };

    struct Material {
        uint32_t flags = 0;
        uint8_t alphaFunc = 0;
        uint8_t depthFunc = 0;
        uint8_t srcBlend = 0;
        uint8_t dstBlend = 0;
        float alphaThreshold = 0.0f;
        uint32_t materialClass = 0;
        std::vector<MaterialTexture> textures;
    };

    constexpr uint32_t MAT_ALPHA_TEST = 0x01;
    constexpr uint32_t MAT_DEPTH_TEST = 0x02;
    constexpr uint32_t MAT_BACKFACE_CULLING = 0x08;
    constexpr uint32_t MAT_BLEND = 0x10;
    constexpr uint32_t MAT_TEXTURE = 0x20;
    constexpr uint32_t MAT_COLOR_WRITES = 0x40;
    constexpr uint32_t MAT_DEPTH_WRITES = 0x80;

    struct Frame {
        uint16_t vertBlock = 0;
        uint16_t indexBlock = 0;
        uint16_t primBlock = 0;
        uint16_t matsBlock = 0;
    };

    struct AnimatedMesh {
        std::string name;
        uint8_t flags = 0;
        std::vector<Frame> frames;
    };

    struct Animation {
        uint16_t frameCount = 0;
        uint16_t frameRate = 0;
        uint16_t animMode = 0;
        uint32_t flags = 0;
        float displacement = 0.0f;
        std::vector<AnimatedMesh> animatedMeshes;
    };

    struct Record {
        uint16_t majorVersion = 0;
        uint16_t minorVersion = 0;
        std::vector<VertexBuffer> vertexBuffers;
        std::vector<IndexBuffer> indexBuffers;
        std::vector<PrimitiveBlock> primitiveBlocks;
        std::vector<Material> materials;
        Animation animation;
        Vec3 bbMin;
        Vec3 bbMax;
    };

} // namespace S3D
