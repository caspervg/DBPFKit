# DBPFKit

C++23 utilities for reading SimCity 4 DBPF archives and the asset formats that live inside them. The library powers the CLI, GUI, and tests in this repo, and it is designed to be embedded into your own tooling.

## Build and Link

```
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

Targets:

- `SC4RULParserLib` - static library with all loaders and helpers.
- `SC4RULParser` - CLI that prints parsed RUL0 pieces and (optionally) DBPF contents.
- `SC4RULParserGui` - raylib/ImGui viewer for puzzle pieces.
- `SC4RULParserTests` - Catch2 suite (`ctest --test-dir cmake-build-debug`).

Embedding via CMake:

```cmake
add_subdirectory(sc4-rul-parser)
add_executable(my_tool main.cpp)
target_link_libraries(my_tool PRIVATE SC4RULParserLib)
```

Headers live in `src/` and expose everything under the `DBPF`, `Exemplar`, `LText`, `FSH`, `S3D`, and `RUL0` namespaces.

## Using the Library

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

The high-level API mirrors this pattern for every supported asset. Call `reader.LoadX(entry)` when you already have an index entry, or rely on the convenience overloads (`LoadX(tgi)`, `LoadX(label)`, `LoadX(mask)`, or `LoadRUL0()` for the canonical TGI).

CLI entry points:

- `./cmake-build-debug/SC4RULParser [RUL0 path] [DBPF path]` to parse `examples/rul0/nam_full.txt` by default and optionally dump DBPF entries.
- `./cmake-build-debug/SC4RULParserGui [examples/rul0/nam_full.txt]` to launch the puzzle viewer.

## API Quick Reference

- **DBPF::Reader**
  - Loading: `LoadFile(path)`, `LoadBuffer(data, size)`.
  - Index/query: `GetIndex()`, `FindEntry(tgi)`, `FindFirstEntry(label)`, `FindEntries(TgiMask|label)`.
  - Raw data: `ReadEntryData(entry|tgi)`, `ReadFirstMatching(mask|label)`.
  - Typed loaders (all return `ParseExpected<T>`): `LoadExemplar`, `LoadLText`, `LoadRUL0`, `LoadFSH`, `LoadS3D` with overloads for `IndexEntry`, `Tgi`, `TgiMask`, or `label` where applicable.
- **Exemplar::Record**
  - Accessors: `FindProperty(id)`, `GetScalar<T>(id)`, `Property::ToString()`.
  - Supports EQZB/CQZB binaries and EQZT/CQZT text exemplars.
- **LText::Record**
  - Stores decoded `std::u16string`; call `ToUtf8()` for logging/UI.
  - Handles canonical UTF-16 payloads and falls back to raw UTF-8/ASCII.
- **RUL0::Record**
  - Parsed via `DBPF::Reader::LoadRUL0()` or `RUL0::Parse(span)`.
  - Contains `puzzlePieces` (unordered map keyed by piece id) and `orderings` (rotation/type rings).
  - Transformation helpers mirror SC4 behavior: `CopyPuzzlePiece`, `ApplyRotation`, `ApplyTranspose`, `ApplyTranslation`, `BuildNavigationIndices`.
- **FSH::Record**
  - Textures with mipmaps/labels; convert to RGBA via `FSH::Reader::ConvertToRGBA8(mip, rgba)`.
  - Understands planar bitmaps, DXT1/3/5 (libsquish), and QFS-compressed payloads.
- **S3D::Record**
  - Mesh data (`vertices`, `indices`, LOD/material metadata); pair with FSH for rendering.
- **QFS::Decompressor**
  - Low-level helper that returns `ParseExpected<size_t>`; most callers use the loaders above rather than invoking it directly.

## Examples and Fixtures

- `examples/rul0/` - RUL0 fixtures used by the parser and tests.
- `examples/dat/` - small DAT slices with text exemplars and other assets.

## Testing

Run `cmake --build cmake-build-debug --target SC4RULParserTests` to rebuild tests, then:

```
ctest --test-dir cmake-build-debug --output-on-failure
```

## Contributing

- Keep code formatted (`clang-format -i src/*.cpp src/*.h tests/*.cpp`).
- Document new fixtures under `examples/`.
- Follow the existing naming/style conventions (see `AGENTS.md` for project-specific notes).
