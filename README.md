# DBPFKit

C++23 utilities for reading SimCity 4 DBPF archives and the asset formats inside them (RUL0, FSH, S3D, LText, exemplars, QFS). Some tests and examples also live in this repo.

## Build and Test

```
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
```

Targets:

- `DBPFKitLib` - static library with all parsers/helpers (public includes exported).
- `DBPFKitTests` - Catch2 suite.

Dependencies are fetched automatically via `FetchContent` (libsquish for DXT, mio for memory-mapped files, Catch2 for tests).

## Using DBPFKit from another CMake project

### Option A: FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  DBPFKit
  GIT_REPOSITORY https://github.com/your-org/DBPFKit.git
  GIT_TAG        main   # pin to a tag/commit
)
FetchContent_MakeAvailable(DBPFKit)

add_executable(my_tool main.cpp)
target_link_libraries(my_tool PRIVATE DBPFKitLib)
```

### Option B: vendored subdirectory

```cmake
add_subdirectory(external/DBPFKit)
add_executable(my_tool main.cpp)
target_link_libraries(my_tool PRIVATE DBPFKitLib)
```

`DBPFKitLib` publishes its include paths; you do not need to set them manually.

## Usage Snippet

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

    if (auto name = exemplar->GetScalar<uint32_t>(0x00000020)) {
        std::println("Name property: 0x{:08X}", *name);
    }
}
```

High-level loaders follow the same pattern: `LoadRUL0()`, `LoadFSH(...)`, `LoadS3D(...)`, `LoadLText(...)`, and `ReadEntryData(...)` when you need raw bytes.

## Repository Layout

- `src/` - library sources/headers.
- `tests/` - Catch2 runner.
- `examples/` - small fixtures for RUL0, exemplars, and DAT slices.
