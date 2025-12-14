#if defined(_WIN32)
#define NOGDI             // All GDI defines and routines
#define NOUSER            // All USER defines and routines
#endif

#include "raylib.h"
#include "rlImGui.h"

#if defined(_WIN32)           // raylib uses these names as function parameters
#undef near
#undef far
#endif

#define NOMINMAX

#include <algorithm>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <tuple>
#include <map>
#include <cfloat>
#include "RUL0.h"
#include "imgui.h"
#include "ini.h"
#include "raymath.h"

#include "DBPFReader.h"
#include "FSHReader.h"
#include "S3DReader.h"
#include "rlgl.h"

namespace {

    constexpr auto kDefaultDbpfPath = "../examples/dat/051-non-pac_000.dat";

    struct PieceView {
        uint32_t id = 0;
        std::string shortDescription;
        std::string fullDetail;
    };

    std::vector<PieceView> BuildPieceViews(const RUL0::Record& data) {
        std::vector<PieceView> views;
        views.reserve(data.puzzlePieces.size());

        for (const auto& [id, piece] : data.puzzlePieces) {
            PieceView view;
            view.id = id;
            if (!piece.effect.name.empty()) {
                view.shortDescription = std::format("0x{:08X} - {}", id, piece.effect.name);
            }
            else {
                view.shortDescription = std::format("0x{:08X}", id);
            }
            view.fullDetail = piece.ToString();
            views.push_back(std::move(view));
        }

        std::sort(views.begin(), views.end(), [](const PieceView& a, const PieceView& b) {
            return a.id < b.id;
        });

        return views;
    }

    std::string DescribeParseError(int resultCode) {
        switch (resultCode) {
        case -1:
            return "failed to open the file";
        case -2:
            return "parsing error occurred";
        default:
            return "unknown error";
        }
    }

    bool TryLoadData(std::string_view filePath, RUL0::Record& data, std::string& errorMessage) {
        const int result = ini_parse(std::string(filePath).c_str(), RUL0::IniHandler, &data);
        if (result < 0) {
            errorMessage = DescribeParseError(result);
            return false;
        }

        RUL0::BuildNavigationIndices(data);
        return true;
    }

    struct LoadedModel {
        Model model{};
        std::vector<Texture2D> textures;
        std::vector<Shader> shaders; // any dynamic shaders we create (UV, alpha-test)
    };

    void ReleaseLoadedModel(std::optional<LoadedModel>& handle) {
        if (!handle) {
            return;
        }
        for (auto& texture : handle->textures) {
            if (texture.id != 0) {
                UnloadTexture(texture);
            }
        }
        for (auto& sh : handle->shaders) {
            if (sh.id != 0)
                UnloadShader(sh);
        }
        UnloadModel(handle->model);
        handle.reset();
    }

    std::vector<uint16_t> ExpandPrimitives(const S3D::PrimitiveBlock& primitives,
                                           std::span<const uint16_t> source) {
        std::vector<uint16_t> expanded;
        for (const auto& prim : primitives) {
            const size_t offset = prim.first;
            const size_t count = prim.length;
            if (offset >= source.size() || count == 0 || offset + count > source.size()) {
                continue;
            }

            switch (prim.type) {
            case 0: {
                // triangle list
                for (size_t i = 0; i + 2 < count; i += 3) {
                    expanded.push_back(source[offset + i + 0]);
                    expanded.push_back(source[offset + i + 1]);
                    expanded.push_back(source[offset + i + 2]);
                }
                break;
            }
            case 1: {
                // triangle strip
                for (size_t i = 0; i + 2 < count; ++i) {
                    const uint16_t a = source[offset + i + 0];
                    const uint16_t b = source[offset + i + 1];
                    const uint16_t c = source[offset + i + 2];
                    if (i % 2 == 0) {
                        expanded.insert(expanded.end(), {a, b, c});
                    }
                    else {
                        expanded.insert(expanded.end(), {a, c, b});
                    }
                }
                break;
            }
            case 2: {
                break;
            }
            default:
                break;
            }
        }
        return expanded;
    }

    struct MeshSource {
        const S3D::VertexBuffer* vertexBuffer = nullptr;
        const S3D::IndexBuffer* indexBuffer = nullptr;
        const S3D::PrimitiveBlock* primitiveBlock = nullptr;
        const S3D::Material* material = nullptr;
    };

    Vector3 CalculateModelCenter(const S3D::Record& record) {
        if (record.vertexBuffers.empty()) {
            return Vector3Zero();
        }

        const Vector3 min{
            record.bbMin.x,
            record.bbMin.y,
            record.bbMin.z,
        };
        const Vector3 max{
            record.bbMax.x,
            record.bbMax.y,
            record.bbMax.z,
        };
        return Vector3Scale(Vector3Add(min, max), 0.5f);
    }

    std::vector<MeshSource> CollectMeshSources(const S3D::Record& record) {
        std::vector<MeshSource> sources;
        sources.reserve(record.animation.animatedMeshes.size());

        for (const auto& mesh : record.animation.animatedMeshes) {
            if (mesh.frames.empty()) {
                continue;
            }

            const auto& frame = mesh.frames.front();
            if (frame.vertBlock >= record.vertexBuffers.size() ||
                frame.indexBlock >= record.indexBuffers.size() ||
                frame.primBlock >= record.primitiveBlocks.size()) {
                continue;
            }

            MeshSource source;
            source.vertexBuffer = &record.vertexBuffers[frame.vertBlock];
            source.indexBuffer = &record.indexBuffers[frame.indexBlock];
            source.primitiveBlock = &record.primitiveBlocks[frame.primBlock];
            if (frame.matsBlock < record.materials.size()) {
                source.material = &record.materials[frame.matsBlock];
            }
            sources.push_back(source);
        }

        if (sources.empty() &&
            !record.vertexBuffers.empty() &&
            !record.indexBuffers.empty() &&
            !record.primitiveBlocks.empty()) {
            MeshSource fallback;
            fallback.vertexBuffer = &record.vertexBuffers.front();
            fallback.indexBuffer = &record.indexBuffers.front();
            fallback.primitiveBlock = &record.primitiveBlocks.front();
            if (!record.materials.empty()) {
                fallback.material = &record.materials.front();
            }
            sources.push_back(fallback);
        }

        return sources;
    }

    bool BuildMeshFromSource(const MeshSource& source,
                             const Vector3& center,
                             float yLift,
                             Mesh& mesh) {
        if (!source.vertexBuffer || !source.indexBuffer || !source.primitiveBlock) {
            return false;
        }

        const auto expandedIndices = ExpandPrimitives(*source.primitiveBlock,
                                                      source.indexBuffer->indices);
        if (source.vertexBuffer->vertices.empty() || expandedIndices.size() < 3) {
            return false;
        }

        mesh = {};
        mesh.vertexCount = static_cast<int>(source.vertexBuffer->vertices.size());
        mesh.triangleCount = static_cast<int>(expandedIndices.size() / 3);
        mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
        mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(mesh.vertexCount * 4));
        mesh.indices = static_cast<unsigned short*>(MemAlloc(expandedIndices.size() *
            sizeof(unsigned short)));

        if (!mesh.vertices || !mesh.normals || !mesh.texcoords ||
            !mesh.colors || !mesh.indices) {
            UnloadMesh(mesh);
            return false;
        }

        // Keep whole model above the XZ grid (y >= 0): shift up by -(bbMin.y - center.y)
        const float yOffset = yLift;

        for (int i = 0; i < mesh.vertexCount; ++i) {
            const auto& vert = source.vertexBuffer->vertices[i];
            // Center geometry around origin in X/Z and keep base at y=0
            mesh.vertices[i * 3 + 0] = vert.position.x - center.x;
            mesh.vertices[i * 3 + 1] = vert.position.y - center.y + yOffset;
            mesh.vertices[i * 3 + 2] = vert.position.z - center.z;
            // Keep UVs as-is; S3D sprites already bake mirroring
            mesh.texcoords[i * 2 + 0] = vert.uv.x;
            mesh.texcoords[i * 2 + 1] = vert.uv.y;
            mesh.colors[i * 4 + 0] = static_cast<unsigned char>(vert.color.x * 255.f);
            mesh.colors[i * 4 + 1] = static_cast<unsigned char>(vert.color.y * 255.f);
            mesh.colors[i * 4 + 2] = static_cast<unsigned char>(vert.color.z * 255.f);
            mesh.colors[i * 4 + 3] = static_cast<unsigned char>(vert.color.w * 255.f);
        }

        std::memcpy(mesh.indices, expandedIndices.data(),
                    expandedIndices.size() * sizeof(unsigned short));

        std::vector<Vector3> normalAccum(mesh.vertexCount, Vector3Zero());
        for (size_t i = 0; i + 2 < expandedIndices.size(); i += 3) {
            const uint16_t i0 = expandedIndices[i + 0];
            const uint16_t i1 = expandedIndices[i + 1];
            const uint16_t i2 = expandedIndices[i + 2];
            if (i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
                continue;
            }

            const Vector3 v0{
                mesh.vertices[i0 * 3 + 0],
                mesh.vertices[i0 * 3 + 1],
                mesh.vertices[i0 * 3 + 2],
            };
            const Vector3 v1{
                mesh.vertices[i1 * 3 + 0],
                mesh.vertices[i1 * 3 + 1],
                mesh.vertices[i1 * 3 + 2],
            };
            const Vector3 v2{
                mesh.vertices[i2 * 3 + 0],
                mesh.vertices[i2 * 3 + 1],
                mesh.vertices[i2 * 3 + 2],
            };

            const Vector3 edge1 = Vector3Subtract(v1, v0);
            const Vector3 edge2 = Vector3Subtract(v2, v0);
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
            if (Vector3Length(normal) == 0.0f) {
                continue;
            }
            normalAccum[i0] = Vector3Add(normalAccum[i0], normal);
            normalAccum[i1] = Vector3Add(normalAccum[i1], normal);
            normalAccum[i2] = Vector3Add(normalAccum[i2], normal);
        }

        for (int i = 0; i < mesh.vertexCount; ++i) {
            Vector3 normal = Vector3Normalize(normalAccum[i]);
            mesh.normals[i * 3 + 0] = normal.x;
            mesh.normals[i * 3 + 1] = normal.y;
            mesh.normals[i * 3 + 2] = normal.z;
        }

        UploadMesh(&mesh, false);
        return true;
    }

    std::optional<Texture2D> LoadTextureForMaterial(const DBPF::Reader& reader, DBPF::Tgi tgi, uint32_t textureId) {
        DBPF::TgiMask mask;
        mask.type = 0x7AB50E44; // FSH
        mask.group = tgi.group; // Hopefully
        mask.instance = textureId;

        auto fsh = reader.LoadFSH(mask);
        if (!fsh.has_value() || fsh->entries.empty() || fsh->entries[0].bitmaps.empty()) {
            mask.group = 0x1ABE787D; // Fallback
            fsh = reader.LoadFSH(mask);
            if (!fsh.has_value() || fsh->entries.empty() || fsh->entries[0].bitmaps.empty()) {
                std::println("Could not load FSH for texture ID {}", textureId);
                return std::nullopt;
            }
        }

        std::vector<uint8_t> rgba;
        if (!FSH::Reader::ConvertToRGBA8(fsh->entries[0].bitmaps[0], rgba)) {
            return std::nullopt;
        }

        Image img{
            .data = rgba.data(),
            .width = fsh->entries[0].bitmaps[0].width,
            .height = fsh->entries[0].bitmaps[0].height,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        };

        Texture2D texture = LoadTextureFromImage(img);
        if (texture.id == 0) {
            return std::nullopt;
        }
        return texture;
    }

    std::optional<LoadedModel> BuildModelFromRecord(const S3D::Record& record, const DBPF::Tgi tgi,
                                                    const DBPF::Reader& reader,
                                                    bool previewMode,
                                                    float rotationDegrees) {
        if (record.animation.animatedMeshes.empty() &&
            record.vertexBuffers.empty()) {
            return std::nullopt;
        }

        const auto meshSources = CollectMeshSources(record);
        if (meshSources.empty()) {
            return std::nullopt;
        }

        const Vector3 center = CalculateModelCenter(record);
        const auto meshCount = static_cast<int>(meshSources.size());

        Model model{};
        model.transform = MatrixRotateY(DEG2RAD * rotationDegrees);
        model.meshes = static_cast<Mesh*>(MemAlloc(sizeof(Mesh) * meshCount));
        model.materials = static_cast<Material*>(MemAlloc(sizeof(Material) * meshCount));
        model.meshMaterial = static_cast<int*>(MemAlloc(sizeof(int) * meshCount));
        if (!model.meshes || !model.materials || !model.meshMaterial) {
            if (model.meshes) {
                MemFree(model.meshes);
            }
            if (model.materials) {
                MemFree(model.materials);
            }
            if (model.meshMaterial) {
                MemFree(model.meshMaterial);
            }
            return std::nullopt;
        }

        std::memset(model.meshes, 0, sizeof(Mesh) * meshCount);
        std::memset(model.materials, 0, sizeof(Material) * meshCount);
        std::memset(model.meshMaterial, 0, sizeof(int) * meshCount);

        std::vector<Texture2D> loadedTextures;
        loadedTextures.reserve(meshSources.size());

        auto builtCount = 0;
        const auto cleanup = [&]() {
            model.meshCount = builtCount;
            model.materialCount = builtCount;
            if (builtCount > 0) {
                UnloadModel(model);
            }
            else {
                MemFree(model.meshes);
                MemFree(model.materials);
                MemFree(model.meshMaterial);
            }
            for (const auto& texture : loadedTextures) {
                if (texture.id != 0) {
                    UnloadTexture(texture);
                }
            }
        };

        for (size_t i = 0; i < meshSources.size(); ++i) {
            Mesh mesh{};
            const float yLift = center.y - record.bbMin.y;
            if (!BuildMeshFromSource(meshSources[i], center, yLift, mesh)) {
                cleanup();
                return std::nullopt;
            }

            model.meshes[builtCount] = mesh;
            model.meshMaterial[builtCount] = builtCount;

            Material material = LoadMaterialDefault();
            const auto* matInfo = meshSources[i].material;

            if (matInfo) {
                for (const auto& texInfo : matInfo->textures) {
                    if (auto texture = LoadTextureForMaterial(reader, tgi, texInfo.textureID)) {
                        // Inspired by S3DTexturesHolder: clamp + linear filtering works best
                        // for previewing sprite LODs. Respect material flags when not previewing.
                        if (previewMode) {
                            SetTextureWrap(*texture, TEXTURE_WRAP_CLAMP);
                            SetTextureFilter(*texture, TEXTURE_FILTER_BILINEAR);
                        }
                        else {
                            const int wrapMode = (texInfo.wrapS == 1 || texInfo.wrapT == 1)
                                ? TEXTURE_WRAP_CLAMP
                                : TEXTURE_WRAP_REPEAT;
                            SetTextureWrap(*texture, wrapMode);
                            const int filter = texInfo.minFilter > 0
                                ? TEXTURE_FILTER_BILINEAR
                                : TEXTURE_FILTER_POINT;
                            SetTextureFilter(*texture, filter);
                        }

                        loadedTextures.push_back(*texture);
                        Texture2D& storedTexture = loadedTextures.back();
                        SetMaterialTexture(&material,
                                           MATERIAL_MAP_DIFFUSE,
                                           storedTexture);
                        break;
                    }
                    else {
                        std::println("Could not load texture for material {}", texInfo.textureID);
                    }
                }
            }

            model.materials[builtCount] = material;
            ++builtCount;
        }

        model.meshCount = builtCount;
        model.materialCount = builtCount;

        LoadedModel loaded;
        loaded.model = model;
        loaded.textures = std::move(loadedTextures);

        // Collect any alpha shaders assigned to materials for cleanup
        for (int mi = 0; mi < model.materialCount; ++mi) {
            Shader sh = model.materials[mi].shader;
            if (sh.id != 0) {
                // Avoid pushing duplicates: simple linear dedup by id
                bool seen = false;
                for (const auto& s : loaded.shaders) {
                    if (s.id == sh.id) {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                    loaded.shaders.push_back(sh);
            }
        }
        return loaded;
    }

    // Build a Yaw/Pitch rotation with selectable multiplication order.
    // Column-major: rightmost matrix applies first.
    // swapOrder = false -> pitch then yaw   (Ry * Rx)
    // swapOrder = true  -> yaw then pitch   (Rx * Ry)
    static Matrix BuildRotation(float yawDeg, float pitchDeg, bool swapOrder)
    {
        const Matrix ry = MatrixRotateY(DEG2RAD * yawDeg);
        const Matrix rx = MatrixRotateX(DEG2RAD * pitchDeg);
        return swapOrder ? MatrixMultiply(rx, ry) : MatrixMultiply(ry, rx);
    }

} // namespace


int main(int argc, char* argv[]) {
    const std::string dbpfPath = (argc > 1 ? argv[1] : kDefaultDbpfPath);

    DBPF::Reader reader;
    std::string parseError;
    auto parseSuccess = false;
    std::vector<const DBPF::IndexEntry*> s3dEntries{};
    std::vector<const DBPF::IndexEntry*> s3dBaseEntries{};
    S3D::Record selectedRecord{};
    DBPF::Tgi selectedBaseTgi{};

    Camera camera = {0};
    camera.position = {25.f, 25.f, 25.f};
    camera.target = {0.f, 0.f, 0.f};
    camera.up = {0.f, 1.f, 0.f};
    camera.fovy = 45.f;
    camera.projection = CAMERA_ORTHOGRAPHIC;

    std::optional<LoadedModel> model = std::nullopt;
    bool modelChanged = false;
    std::string modelStatus;

    // LOD sprite selection controls (instance offset based)
    bool useInstanceOffsets = true;
    int lodZoom = 5; // 1 (farthest) .. 5 (closest)
    int lodRotation = 0; // 0..3 (cardinal)
    bool showBaseOnly = true; // Toggle between base-only list and all S3D entries
    bool disableBackfaceCulling = true; // Helpful for single-sided LOD quads (default on for S3D)
    bool previewMode = true; // Fixed framing like S3DViewer.py
    bool previewBestFit = true; // If false, scale by zoom table

    // Camera tweak toggles to help diagnose coordinate differences
    bool camInvertPitchSign = true; // Invert sign of pitch used for bounds/view (enabled by default)
    bool camSwapRotationOrder = true; // Swap rotation order (X then Y instead of Y then X)

    size_t selectedPiece = 0;
    DBPF::Tgi currentModelTgi{};
    // Helper to key S3D LOD sprite families
    using FamKey = std::tuple<uint32_t, uint32_t, uint32_t, uint8_t>;
    std::map<FamKey, size_t> familyCounts; // number of variants per family

    const auto reload = [&]() {
        s3dEntries.clear();
        s3dBaseEntries.clear();
        if (!reader.LoadFile(dbpfPath)) {
            throw std::runtime_error("Failed to load DBPF file");
            parseSuccess = false;
        }
        else {
            s3dEntries = reader.FindEntries("S3D");
            // Build base entries: rotation nibble == 0, minimal zoom byte per family.
            // Family key: (type, group, instance_high16, instance_low4)
            struct FamVal {
                const DBPF::IndexEntry* entry;
                uint32_t zoomByte;
            };
            std::map<FamKey, FamVal> bases;
            familyCounts.clear();
            for (const auto* e : s3dEntries) {
                const uint32_t inst = e->tgi.instance;
                // Only consider rotation 0 variants
                if ((inst & 0xF0u) != 0u)
                    continue;
                const FamKey key{
                    e->tgi.type,
                    e->tgi.group,
                    inst & 0xFFFF0000u,
                    static_cast<uint8_t>(inst & 0x0Fu)
                };
                const uint32_t zoomByte = inst & 0x0000FF00u;
                auto it = bases.find(key);
                if (it == bases.end() || zoomByte < it->second.zoomByte) {
                    bases[key] = FamVal{e, zoomByte};
                }
                // Track full family size (all entries sharing key but any rot/zoom)
                // Count all matching entries irrespective of rot/zoom
                familyCounts[key]++;
            }
            s3dBaseEntries.reserve(bases.size());
            for (const auto& kv : bases) {
                s3dBaseEntries.push_back(kv.second.entry);
            }
            parseSuccess = true;
        }
        selectedPiece = 0;
        modelStatus.clear();
        ReleaseLoadedModel(model);
        selectedBaseTgi = {};
        currentModelTgi = {};
    };
    reload();

    constexpr auto screenWidth = 1600;
    constexpr auto screenHeight = 900;
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(screenWidth, screenHeight, "SC4 Model Viewer");
    SetTargetFPS(144);

    rlImGuiSetup(true);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().Fonts->Clear();
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0f);
    if (font == NULL)
        font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttf", 16.0f); // Windows 7

    auto showModelsPanel = true;
    auto run = true;

    while (!WindowShouldClose() && run) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (!previewMode) {
            UpdateCamera(&camera, CAMERA_ORBITAL);
        }

        BeginMode3D(camera);
        if (previewMode && !selectedRecord.vertexBuffers.empty()) {
            // CameraPitch defaults per zoom: 30, 35, 40, 45, 45
            auto angleX = 45.0f;
            switch (lodZoom) {
            case 1:
                angleX = 30.0f;
                break;
            case 2:
                angleX = 35.0f;
                break;
            case 3:
                angleX = 40.0f;
                break;
            case 4:
            default:
                angleX = 45.0f;
                break; // 5
            }

            // With centered geometry, the target is origin
            camera.target = {0.f, 0.f, 0.f};

            // Compute an orthographic width to fit the rotated bbox
            const Vector3 bbMin = {selectedRecord.bbMin.x, selectedRecord.bbMin.y, selectedRecord.bbMin.z};
            const Vector3 bbMax = {selectedRecord.bbMax.x, selectedRecord.bbMax.y, selectedRecord.bbMax.z};
            const Vector3 center = CalculateModelCenter(selectedRecord);
            const Vector3 relMin = Vector3Subtract(bbMin, center);
            const Vector3 relMax = Vector3Subtract(bbMax, center);

            // 8 corners
            Vector3 corners[8] = {
                {relMin.x, relMin.y, relMin.z}, {relMax.x, relMin.y, relMin.z},
                {relMin.x, relMax.y, relMin.z}, {relMax.x, relMax.y, relMin.z},
                {relMin.x, relMin.y, relMax.z}, {relMax.x, relMin.y, relMax.z},
                {relMin.x, relMax.y, relMax.z}, {relMax.x, relMax.y, relMax.z},
            };
            // Bounds rotation: use same yaw/pitch as view and allow order toggle
            const uint32_t rotationNibble = std::min<uint32_t>((currentModelTgi.instance & 0xF0u) >> 4, 3);
            float yaw = 22.5f + 90.0f * static_cast<float>(rotationNibble);
            float pitch = angleX * (camInvertPitchSign ? -1.0f : 1.0f);
            Matrix rotBounds = BuildRotation(yaw, pitch, camSwapRotationOrder);
            float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
            for (auto& c : corners) {
                Vector3 r = Vector3Transform(c, rotBounds);
                minX = std::min(minX, r.x);
                maxX = std::max(maxX, r.x);
                minY = std::min(minY, r.y);
                maxY = std::max(maxY, r.y);
            }
            float width = std::max(maxX - minX, maxY - minY);
            if (!previewBestFit) {
                static constexpr float zoomScale[5] = {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f};
                width *= zoomScale[std::clamp(lodZoom, 1, 5) - 1];
            }
            // Show more context: push camera framing out to match SC4 viewer sizing
            constexpr float kBaseMargin = 2.2f;
            camera.fovy = width * kBaseMargin;

            // Set camera position along rotated forward vector
            // Choose camera distance per zoom (farther at lower zooms)
            // Keep camera within a safe clip range; orthographic size is set by camera.fovy
            static constexpr float zoomDistance[5] = {800.0f, 650.0f, 500.0f, 360.0f, 260.0f};
            const float distance = zoomDistance[std::clamp(lodZoom, 1, 5) - 1];
            // View rotation same as bounds
            Matrix rotView = BuildRotation(yaw, pitch, camSwapRotationOrder);
            Vector3 forward = Vector3Transform(Vector3{0, 0, -1}, rotView);
            // Place camera farther as distance grows (zoom 1 far, zoom 5 near)
            camera.position = Vector3Subtract(camera.target, Vector3Scale(forward, distance));
            camera.up = {0.f, 1.f, 0.f};
            camera.projection = CAMERA_ORTHOGRAPHIC;
        }
        if (modelChanged) {
            ReleaseLoadedModel(model);
            if (selectedRecord.vertexBuffers.empty()) {
                if (modelStatus.empty()) {
                    modelStatus = "Model does not contain any vertex buffers.";
                }
            }
            else {
                const float rotDegrees = useInstanceOffsets
                    ? static_cast<float>((lodRotation & 0x3) * 90)
                    : 0.0f;
                // Use checker override in preview for UV debugging if enabled via UI
                model = BuildModelFromRecord(selectedRecord, selectedBaseTgi, reader, previewMode, rotDegrees);
                if (!model && modelStatus.empty()) {
                    modelStatus = "Unable to build mesh for this S3D.";
                }
            }
            modelChanged = false;
        }

        DrawGrid(200, 1.f);

        if (model.has_value()) {
            if (disableBackfaceCulling) rlDisableBackfaceCulling();
            DrawModel(model->model, Vector3Zero(), 1.f, WHITE);
            if (disableBackfaceCulling) rlEnableBackfaceCulling();
        }

        EndMode3D();

        rlImGuiBegin();

        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Reload", nullptr, false, true)) {
                    //reload();
                }
                if (ImGui::MenuItem("Exit")) {
                    run = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Models", nullptr, &showModelsPanel);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        if (showModelsPanel) {
            ImGui::Begin("Models");
            ImGui::TextUnformatted(std::format("Parsed from: {}", dbpfPath).c_str());
            ImGui::Text("Loaded %zu S3D models (%zu base)", s3dEntries.size(), s3dBaseEntries.size());
            if (!parseSuccess) {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Unable to load file: %s", parseError.c_str());
            }

            ImGui::Separator();
            // Listing controls
            bool listChanged = ImGui::Checkbox("Show base entries only", &showBaseOnly);
            ImGui::SameLine();
            ImGui::Checkbox("Preview mode", &previewMode);
            if (previewMode) {
                ImGui::SameLine();
                ImGui::Checkbox("Best fit", &previewBestFit);
            }
            if (listChanged) {
                selectedPiece = 0;
                selectedBaseTgi = {};
                currentModelTgi = {};
                modelStatus.clear();
                ReleaseLoadedModel(model);
            }
            ImGui::Columns(2, "ModelColumns", true);

            ImGui::BeginChild("ModelList", ImVec2(0, 0), true);
            const auto& currentList = showBaseOnly ? s3dBaseEntries : s3dEntries;
            if (currentList.empty()) {
                ImGui::TextUnformatted(std::format("No models available in {}", dbpfPath).c_str());
            }
            else {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(currentList.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const bool selected = (static_cast<size_t>(row) == selectedPiece);
                        if (ImGui::Selectable(currentList[row]->tgi.ToString().c_str(), selected)) {
                            selectedPiece = static_cast<size_t>(row);
                            auto* entry = currentList[selectedPiece];
                            selectedBaseTgi = entry->tgi;
                            // Sprite-vs-True3D detection by family size
                            FamKey famKey{entry->tgi.type, entry->tgi.group,
                                          entry->tgi.instance & 0xFFFF0000u,
                                          static_cast<uint8_t>(entry->tgi.instance & 0x0Fu)};
                            bool isSpriteFamily = false;
                            if (auto it = familyCounts.find(famKey); it != familyCounts.end()) {
                                isSpriteFamily = (it->second > 1);
                            }
                            if (!isSpriteFamily) {
                                // True3D: free camera by default, no instance offsets
                                previewMode = false;
                                useInstanceOffsets = false;
                            }
                            else {
                                previewMode = true;
                            }
                            // Compute derived instance using SC4 pattern:
                            // instance = base + (zoom-1)*0x100 + rotation*0x10
                            DBPF::Tgi tgiToLoad = useInstanceOffsets ? selectedBaseTgi : entry->tgi;
                            if (useInstanceOffsets) {
                                const uint32_t zoomOffset = static_cast<uint32_t>(lodZoom - 1) * 0x100u;
                                const uint32_t rotOffset = static_cast<uint32_t>(lodRotation) * 0x10u;
                                tgiToLoad.instance = tgiToLoad.instance + zoomOffset + rotOffset;
                            }
                            auto data = reader.LoadS3D(tgiToLoad);
                            if (!data.has_value()) {
                                modelStatus = data.error().message;
                                currentModelTgi = {};
                                selectedRecord = S3D::Record{};
                            }
                            else {
                                modelStatus.clear();
                                selectedRecord = std::move(*data);
                                currentModelTgi = tgiToLoad;
                            }
                            modelChanged = true;
                        }
                    }
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();
            ImGui::BeginChild("ModelDetail", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if ((showBaseOnly ? s3dBaseEntries : s3dEntries).empty()) {
                ImGui::TextUnformatted("Select a model to show its details.");
            }
            else {
                auto selectedEntry = (showBaseOnly ? s3dBaseEntries : s3dEntries)[selectedPiece];
                ImGui::TextUnformatted(std::format("Model: {}", selectedEntry->tgi.ToString()).c_str());
                if (!modelStatus.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "%s", modelStatus.c_str());
                }
                else {
                    ImGui::Text("S3D Version: %u.%u", selectedRecord.majorVersion, selectedRecord.minorVersion);
                    ImGui::Separator();
                    ImGui::Text("Model Details:");
                    ImGui::Text(" VertexBuffers: %zu", selectedRecord.vertexBuffers.size());
                    ImGui::Text(" IndexBuffers: %zu", selectedRecord.indexBuffers.size());
                    ImGui::Text(" PrimitiveBlocks: %zu", selectedRecord.primitiveBlocks.size());
                    ImGui::Text(" Materials: %zu", selectedRecord.materials.size());

                    ImGui::Separator();
                    // Only show LOD sprite controls for families with variants
                    auto hasVariants = false;
                    if (selectedBaseTgi.type != 0) {
                        FamKey key{selectedBaseTgi.type, selectedBaseTgi.group,
                                   selectedBaseTgi.instance & 0xFFFF0000u,
                                   static_cast<uint8_t>(selectedBaseTgi.instance & 0x0Fu)};
                        if (auto it = familyCounts.find(key); it != familyCounts.end()) {
                            hasVariants = (it->second > 1);
                        }
                    }
                    if (hasVariants) {
                        ImGui::Text("LOD Sprite");
                        bool uiChanged = false;
                        uiChanged |= ImGui::Checkbox("Use instance offsets (zoom/rotation)", &useInstanceOffsets);
                        uiChanged |= ImGui::SliderInt("Zoom (1-5)", &lodZoom, 1, 5);
                        uiChanged |= ImGui::SliderInt("Rotation (0-3)", &lodRotation, 0, 3);

                        if (hasVariants && selectedBaseTgi.type != 0) {
                            const uint32_t zoomOffset = static_cast<uint32_t>(lodZoom - 1) * 0x100u;
                            const uint32_t rotOffset = static_cast<uint32_t>(lodRotation) * 0x10u;
                            const uint32_t derived = selectedBaseTgi.instance + zoomOffset + rotOffset;
                            ImGui::Text("Base I: 0x%08X  Derived I: 0x%08X", selectedBaseTgi.instance, derived);
                        }

                        if (hasVariants && uiChanged && selectedBaseTgi.type != 0) {
                            DBPF::Tgi tgi = selectedBaseTgi;
                            if (useInstanceOffsets) {
                                const uint32_t zoomOffset = static_cast<uint32_t>(lodZoom - 1) * 0x100u;
                                const uint32_t rotOffset = static_cast<uint32_t>(lodRotation) * 0x10u;
                                tgi.instance = tgi.instance + zoomOffset + rotOffset;
                            }
                            auto newData = reader.LoadS3D(tgi);
                            if (!newData.has_value()) {
                                modelStatus = newData.error().message;
                                currentModelTgi = {};
                            }
                            else {
                                modelStatus.clear();
                                selectedRecord = std::move(*newData);
                                lodRotation = std::clamp(lodRotation, 0, 3);
                                currentModelTgi = tgi;
                                modelChanged = true;
                            }
                        }
                    }
                }

                ImGui::Columns(1);
                ImGui::EndChild();
            }
            ImGui::EndChild();

            rlImGuiEnd();

            DrawFPS(10, 10);

            EndDrawing();
        }
    }

    ReleaseLoadedModel(model);
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
