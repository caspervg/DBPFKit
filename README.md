# dbpf++

C++23 utilities for reading SimCity 4 DBPF archives and the asset formats that live inside them. The library powers the CLI, GUI, and tests in this repo, but it is also designed to be embedded into your own tooling.

## Highlights

- **DBPF index + I/O** – works from memory buffers or memory-mapped files, understands directory metadata, and resolves catalog labels/TGI masks.
- **Transparent decompression** – QFS blocks are inflated automatically via `ParseExpected` so callers get real error messages instead of booleans.
- **Typed loaders** – helpers for Exemplar (binary + text), LText, FSH, S3D, and RUL0 Intersection Ordering records.
- **Sample fixtures** – `examples/` contains RUL0 snippets, exemplar exports, and DATs for experimentation.
- **Catch2 coverage** – unit tests exercise every parser; use them as reference implementations when extending the codebase.

## Building

```
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

Targets:

- `SC4RULParserLib` – static library with all loaders.
- `SC4RULParser` – CLI sample that dumps FSH/LText data.
- `SC4RULParserGui` – raylib/ImGui front-end (requires desktop deps).
- `SC4RULParserTests` – Catch2 suite (`ctest --test-dir cmake-build-debug`).

## Quick Start

```cpp
#include "DBPFReader.h"
#include "ExemplarReader.h"

DBPF::Reader reader;
if (!reader.LoadFile("NAM.dat")) {
    throw std::runtime_error("Failed to open DAT");
}

for (const auto& entry : reader.GetIndex()) {
    if (entry.tgi.type != 0x6534284A) continue; // exemplar

    auto exemplar = reader.LoadExemplar(entry);
    if (!exemplar) {
        std::println("Exemplar {} failed: {}", entry.tgi.ToString(), exemplar.error().message);
        continue;
    }

    const auto& record = exemplar.value();
    std::println("Parent: {}", record.parent.ToString());
    if (auto name = record.GetScalar<uint32_t>(0x00000020)) {
        std::println("Name property: 0x{:08X}", *name);
    }
}
```

The high-level API mirrors this pattern for every supported asset. Call `reader.LoadX(entry)` when you already have an index entry, or rely on the convenience overloads (`LoadX(tgi)` / `LoadX(label)` / `LoadX()`) where they make sense.

## Typed Asset Reference

### Exemplar

```cpp
auto exemplar = reader.LoadExemplar("Exemplar (Road)");
if (!exemplar) {
    std::println("Exemplar error: {}", exemplar.error().message);
    return;
}
const auto& record = exemplar.value();
```

- Supports EQZB/CQZB binaries and EQZT/CQZT text exemplars.
- Text parser understands the scdbpf grammar, hex literals, signed numbers, bool aliases, and string arrays.
- Every `Exemplar::Record` exposes `FindProperty`, `GetScalar<T>`, and a `Property::ToString()` helper for logging.

### LText

```cpp
auto text = reader.LoadLText("LText");
if (!text) {
    std::println("LText error: {}", text.error().message);
} else {
    std::println("{}", text->ToUtf8());
}
```

Handles canonical UTF‑16 payloads (two-byte length + 0x1000 marker) and falls back to interpreting the raw bytes as UTF‑8/ASCII when modders ship bare strings. The decoded text is stored as `std::u16string`; use `ToUtf8()` for UI/logging.

### RUL0 (Intersection Ordering)

```cpp
auto rul = reader.LoadRUL0(); // shortcut for the canonical TGI
if (!rul) {
    std::println("RUL0 error: {}", rul.error().message);
} else {
    std::println("Pieces: {}", rul->puzzlePieces.size());
}
```

`IntersectionOrdering::Parse` and `reader.LoadRUL0()` feed the existing INI handler, apply `BuildNavigationIndices`, and return the same `Data` structure used by the CLI/GUI.

### FSH

```cpp
auto fsh = reader.LoadFSH(entry);
if (!fsh) {
    std::println("FSH error: {}", fsh.error().message);
} else {
    for (const auto& texture : fsh->entries) {
        for (const auto& mip : texture.bitmaps) {
            std::vector<uint8_t> rgba;
            if (FSH::Reader::ConvertToRGBA8(mip, rgba)) {
                // use rgba.data()
            }
        }
    }
}
```

The loader understands planar bitmaps, DXT1/3/5 (via libsquish), directory labels, mip stacks, and chunked QFS payloads.

### S3D

```cpp
auto model = reader.LoadS3D(entry);
if (!model) {
    std::println("S3D error: {}", model.error().message);
} else {
    const auto& mesh = model.value();
    // use mesh.vertices / mesh.indices
}
```

Returns an `S3D::Model` with decoded vertex/index buffers plus metadata for LODs and materials. Pair it with FSH to build meshes or feed the provided GUI.

## Examples & Tests

- `examples/rul0/` – RUL0 fixtures used by the parser and tests.
- `examples/dat/` – small DAT slices with text exemplars and other assets.
- `tests/tests.cpp` – Catch2 suite covering DBPF I/O, QFS, Exemplar, LText, RUL0, FSH, and S3D.

Run `ctest --test-dir cmake-build-debug --output-on-failure` after making changes. The tests are fast and provide good guardrails when touching parsing logic.

## Contributing

- Keep code formatted (`clang-format -i src/*.cpp src/*.h tests/*.cpp`).
- Document new fixtures under `examples/`.
- Follow the existing naming/style conventions (see `AGENTS.md` for project-specific notes).
