# SC4 RUL Parser Support Libraries

This repository contains a collection of reusable helpers for reading SimCity 4 data formats in C++23. The main components today are:

- `DBPFReader` / `DBPFTypes`: minimal DBPF archive support, ported from the `scdbpf` reference. Handles chunked records, directory metadata, and exposes helper indexes for TGIs.
- `QFSDecompressor`: a byte-for-byte decoder that follows wouanagaine’s SC4Mapper 2013 implementation.
- `S3DReader`: a parser for the `3DMD` mesh format used by SimCity 4 models.
- `FSHReader`: decodes SC4 texture containers (SHPI/G26x) including DXT1/3/5 bitmaps and mipmaps via libsquish.
- `Exemplar` helpers: binary exemplar/cohort parser with property introspection and the catalogued TGI labels from scdbpf.

The sections below outline how to build the project and how to consume each API from your own tools.

## Building & Tests

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target SC4RULParserTests
ctest --test-dir cmake-build-debug --output-on-failure
```

The `SC4RULParserLib` static library exposes the public headers under `src/`. Include the directory when adding the target to another project:

```cmake
add_subdirectory(path/to/sc4-rul-parser)
target_link_libraries(my_tool PRIVATE SC4RULParserLib)
target_include_directories(my_tool PRIVATE path/to/sc4-rul-parser/src)
```

## DBPF Reader & Types

**Key headers:** `DBPFReader.h`, `DBPFTypes.h`

```cpp
#include "DBPFReader.h"

DBPF::Reader reader;
if (!reader.LoadFile("examples/dat/SM2 Mega Prop Pack Vol1.dat")) {
    throw std::runtime_error("failed to load DAT");
}

for (const auto& entry : reader.GetIndex()) {
    auto payload = reader.ReadEntryData(entry);
    if (!payload) {
        continue; // skip truncated entries
    }
    // entry.tgi gives type/group/instance
    std::span<const uint8_t> bytes(payload->data(), payload->size());
    // bytes contains either raw or auto-decompressed bytes
}
```

Usage notes:

- `LoadFile(path)` maps the entire DAT into memory and verifies the 0x60-byte header (version `1.0`, index type `7`).
- `LoadBuffer(data, size)` accepts an in-memory image (useful in tests).
- `GetIndex()` returns the parsed index entries (`DBPF::IndexEntry`), each with a `Tgi`, file offset, size, and optional `decompressedSize`.
- `ReadEntryData(entry)` copies the referenced bytes and automatically runs them through the QFS decompressor when needed. If you need raw bytes, test `QFS::Decompressor::IsQFSCompressed` yourself before calling `Decompress`.
- Directory metadata is applied automatically when an entry with `DBPF::kDirectoryTgi` exists; the `decompressedSize` field is filled for matching entries.

### Convenience lookups

`DBPF::Reader` exposes helpers that sit on top of the catalog in `TGI.cpp`, so you can map straight from human-friendly labels to entries:

```cpp
DBPF::TgiMask fshMask;
fshMask.type = 0x7AB50E44; // any FSH

for (const DBPF::IndexEntry* entry : reader.FindEntries(fshMask)) {
    auto bytes = reader.ReadEntryData(*entry);
    // ...
}

auto overlayTextures = reader.FindEntries("FSH (Base/Overlay Texture)");
auto firstExemplar = reader.ReadFirstMatching("Exemplar");
auto exact = reader.ReadEntryData(DBPF::Tgi{0x6534284A, 0x2821ED93, 0x12345678});

if (auto exemplar = reader.LoadExemplar("Exemplar (Road)")) {
    // exemplar->properties ...
}

auto s3d = reader.LoadS3D(DBPF::Tgi{0x5AD0E817, 0xBADB57F1, 0x00000001});
if (!s3d) {
    std::println("S3D failed: {}", s3d.error().message);
}
```

Use whichever path matches your workflow: exact TGIs, `TgiMask` filters, or the catalog strings shown by `DBPF::Describe`.

- `LoadFSH/LoadS3D/LoadExemplar` wrap the lookup + parse sequence and return `std::expected<...>` results so you can jump straight to the decoded data (see snippet above).

### TGI helpers & labeling

If you need friendlier names or quick lookups for specific type/group/instance combos, the `TGI.h` helpers wrap everything in one place:

```cpp
#include "TGI.h"

DBPF::Tgi tgi{0x5AD0E817, 0xBADB57F1, 0x00000000};

std::string_view label = DBPF::Describe(tgi);          // => "S3D (Maxis)"
auto mask = DBPF::MaskForLabel("Exemplar (T21)");      // optional TgiMask describing that family
bool isExemplar = mask && mask->Matches(tgi);
```

- `Tgi` now exposes `ToString()` plus hashing/comparison helpers so you can drop it into `unordered_map` instances for fast T/G/I lookups.
- `Describe(tgi)` consults a built-in catalog (ported from `scdbpf`) so you can present human-readable labels in UIs or logs.
- `MaskForLabel(label)` retrieves the `TgiMask` used for that label, which is handy when filtering indexes (e.g., “find all entries whose type=FSH and group=0x0986135e”).

These helpers are self-contained; add more catalog entries by editing `src/TGI.cpp`.

## QFS Decompressor

**Header:** `QFSDecompressor.h`

```cpp
#include "QFSDecompressor.h"

std::vector<uint8_t> decompressed;
std::span<const uint8_t> rawSpan(raw.data(), raw.size());
if (QFS::Decompressor::Decompress(rawSpan, decompressed)) {
    // decompressed now holds the inflated bytes
}
```

API summary:

- `IsQFSCompressed(std::span<const uint8_t>)` checks the 0x10FB signature and minimum header length.
- `GetUncompressedSize(std::span<const uint8_t>)` returns the 24-bit size stored in the header (0 if not compressed).
- `Decompress(std::span<const uint8_t>, std::vector<uint8_t>&)` validates the header, resizes the output vector to the advertised length, and decodes the packcode stream. It supports the optional “chunk flag” header variant and the literal terminator rules described on the SC4Devotion wiki.

If you need more control (e.g., to stream into a preallocated buffer) you can call `DecompressInternal` directly, but the vector-based helper is usually sufficient.

## FSH Reader

**Headers:** `FSHReader.h`, `FSHStructures.h`

```cpp
#include "FSHReader.h"

auto payload = reader.ReadEntryData(entry);                    // DBPF entry with type 0x7AB50E44
std::span<const uint8_t> payloadSpan(payload->data(), payload->size());
if (auto fileResult = FSH::Reader::Parse(payloadSpan)) {
    const auto& file = fileResult.value();
    for (const auto& tex : file.entries) {
        for (const auto& mip : tex.bitmaps) {
            std::vector<uint8_t> rgba;
            if (FSH::Reader::ConvertToRGBA8(mip, rgba)) {
                // rgba now contains width*height*4 bytes
            }
        }
    }
} else {
    std::println("FSH parse failed: {}", fileResult.error().message);
}
```

Highlights:

- Supports SHPI/G26x/G35x headers, directory entries, labels/attachments, and the usual SimCity directory IDs (G264/GIMX/etc.).
- Automatically QFS-decompresses records before parsing.
- Handles both uncompressed formats (32-bit, 24-bit, 4444/565/1555) and DXT1/3/5 via libsquish. Each entry exposes all mip levels, not just the base texture.

### Saving textures to disk (CLI helper)

`src/main.cpp` contains a convenience path that scans a DAT, decodes the first few FSH entries, and writes them to `./fsh_output`. On Windows this uses WIC via `IWICBitmapEncoder`, so no extra libraries are required. Non-Windows builds skip the PNG export path (the decoder still works, but the sample app just logs a message).

## S3D Reader

**Headers:** `S3DReader.h`, `S3DStructures.h`

```cpp
#include "S3DReader.h"

std::vector<uint8_t> s3dData = /* load from DBPF entry */;
S3D::Model model;
std::span<const uint8_t> s3dSpan(s3dData.data(), s3dData.size());
auto parsed = S3D::Reader::Parse(s3dSpan);
if (!parsed) {
    throw std::runtime_error(parsed.error().message);
}
S3D::Model model = std::move(parsed).value();

for (const auto& vb : model.vertexBuffers) {
    // vb.vertices, vb.bbMin/bbMax, etc.
}
```

Highlights:

- `Reader::Parse(std::span<const uint8_t>) -> std::expected<Model, ParseError>` walks each chunk (`3DMD`, `HEAD`, `VERT`, `INDX`, `PRIM`, `MATS`, `ANIM`). The parser enforces the documented minor versions and vertex formats.
- `Model` aggregates vertex buffers, index buffers, materials, animations, and bounding boxes. See `S3DStructures.h` for detailed field layouts.
- The reader intentionally keeps parsing logic simple—no implicit OpenGL bindings or GPU resources—so you can adapt the in-memory model to whatever renderer or exporter you need.

### Example: Rendering S3D via raylib

The project already fetches raylib/imgui for the GUI target, so you can glue the readers together like this:

```cpp
#include <raylib.h>

#include "DBPFReader.h"
#include "S3DReader.h"

std::optional<Mesh> BuildMesh(const S3D::Model& model) {
    if (model.vertexBuffers.empty() || model.indexBuffers.empty()) return std::nullopt;
    const auto& vb = model.vertexBuffers.front();
    const auto& ib = model.indexBuffers.front();

    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(vb.vertices.size());
    mesh.triangleCount = static_cast<int>(ib.indices.size() / 3);
    mesh.vertices = MemAlloc(sizeof(float) * 3 * mesh.vertexCount);
    mesh.colors = MemAlloc(sizeof(unsigned char) * 4 * mesh.vertexCount);
    mesh.texcoords = MemAlloc(sizeof(float) * 2 * mesh.vertexCount);
    mesh.indices = MemAlloc(sizeof(unsigned short) * ib.indices.size());

    for (int i = 0; i < mesh.vertexCount; ++i) {
        const auto& v = vb.vertices[i];
        float* pos = reinterpret_cast<float*>(mesh.vertices);
        pos[3 * i + 0] = v.position.x;
        pos[3 * i + 1] = v.position.y;
        pos[3 * i + 2] = v.position.z;

        auto* clr = reinterpret_cast<unsigned char*>(mesh.colors);
        clr[4 * i + 0] = static_cast<unsigned char>(v.color.x * 255.0f);
        clr[4 * i + 1] = static_cast<unsigned char>(v.color.y * 255.0f);
        clr[4 * i + 2] = static_cast<unsigned char>(v.color.z * 255.0f);
        clr[4 * i + 3] = static_cast<unsigned char>(v.color.w * 255.0f);

        float* uv = reinterpret_cast<float*>(mesh.texcoords);
        uv[2 * i + 0] = v.uv.x;
        uv[2 * i + 1] = v.uv.y;
    }

    auto* idx = reinterpret_cast<unsigned short*>(mesh.indices);
    for (size_t i = 0; i < ib.indices.size(); ++i) {
        idx[i] = static_cast<unsigned short>(ib.indices[i]);
    }

    UploadMesh(&mesh, false);
    return mesh;
}

int main() {
    InitWindow(1280, 720, "S3D viewer");
    Camera3D cam{ {5, 5, 5}, {0, 0, 0}, {0, 1, 0}, 45.0f, CAMERA_PERSPECTIVE };

    DBPF::Reader dbpf;
    dbpf.LoadFile("examples/dat/SM2 Mega Prop Pack Vol1.dat");
    const auto& entry = dbpf.GetIndex().front(); // pick your S3D entry
    auto bytes = dbpf.ReadEntryData(entry);

    S3D::Model model;
    std::span<const uint8_t> bytesSpan(bytes->data(), bytes->size());
    auto parsedModel = S3D::Reader::Parse(bytesSpan);
    if (!parsedModel) {
        throw std::runtime_error(parsedModel.error().message);
    }
    model = std::move(parsedModel).value();

    auto mesh = BuildMesh(model);
    Model rayModel = LoadModelFromMesh(*mesh);

    while (!WindowShouldClose()) {
        UpdateCamera(&cam, CAMERA_ORBITAL);
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(cam);
        DrawGrid(20, 1.0f);
        DrawModel(rayModel, Vector3Zero(), 1.0f, WHITE);
        EndMode3D();
        EndDrawing();
    }

    UnloadModel(rayModel);
    CloseWindow();
}
```

From here you can extend the renderer with:

1. Texture lookups (decode the referenced FSH/PNG entries and assign them to `model.materials[i].maps[MATERIAL_MAP_DIFFUSE]`).
2. Multiple LOD support: iterate through every vertex/index buffer pair in the S3D.
3. Placement transforms sourced from exemplar entries (apply to `model.transform`).
4. UI via ImGui to select TGIs, toggle wireframe, and inspect metadata.

## Exemplar Parser

**Headers:** `ExemplarReader.h`, `ExemplarStructures.h`, `TGI.h`

```cpp
#include "ExemplarReader.h"

auto payload = reader.ReadEntryData(entry);                // entry.tgi.type == 0x6534284A
std::span<const uint8_t> payloadSpan(payload->data(), payload->size());
auto result = Exemplar::Parse(payloadSpan);
if (!result) {
    throw std::runtime_error(result.error().message);
}
const auto& exemplar = result.value();
std::println("Parent: {}", exemplar.parent.ToString());
for (const auto& prop : exemplar.properties) {
    std::println("  {}", prop.ToString());
}
```

The parser mirrors scdbpf’s implementation: it handles binary EQZB/CQZB headers, property IDs/types, lists vs. scalars, and exposes a human-friendly `Property::ToString()` (hex + decimal for numeric fields). Text exemplars (`EQZT/CQZT`) follow the same grammar (see `examples/dat/file_dec*.eqz`) and are parsed transparently alongside the binary form.
All exemplar/FSH/S3D readers return `std::expected<..., ParseError>` so you can bubble the message up or transform it into UI feedback.

## Extending / Integrating

- Add new tests by extending `tests/tests.cpp`; the file already includes helpers for constructing synthetic DBPF archives and verifying QFS + S3D flows.
- When adding new parsing features, keep sample fixtures under `examples/` and mention them in the README so they can be re-used for manual verification.
- Follow the formatting and naming conventions noted in `AGENTS.md` to stay consistent with the rest of the codebase.
