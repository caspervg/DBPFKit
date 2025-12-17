cmake_minimum_required(VERSION 3.20)
project(DBPFKit)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Raylib + ImGui dependencies for GUI target
include(FetchContent)

FetchContent_Declare(
        libsquish
        GIT_REPOSITORY https://github.com/oblivioncth/libsquish.git
        GIT_TAG v1.15.1.3
)
FetchContent_MakeAvailable(libsquish)
if(APPLE)
    # Upstream forces an old universal set of arches (i386;ppc); override to the
    # current toolchain architecture(s) so clang on Apple Silicon stops failing.
    set(_squish_arches "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _squish_arches)
        set(_squish_arches "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(TARGET libsquish_squish)
        set_target_properties(libsquish_squish PROPERTIES OSX_ARCHITECTURES "${_squish_arches}")
    endif()
endif()

set(MIO_COMMIT 8b6b7d878c89e81614d05edca7936de41ccdd2da)
FetchContent_Declare(
        mio
        GIT_REPOSITORY https://github.com/vimpunk/mio.git
        GIT_TAG ${MIO_COMMIT}
        GIT_SHALLOW 1
)
FetchContent_GetProperties(mio)
if (NOT mio_POPULATED)
    FetchContent_MakeAvailable(mio)
endif()
set(MIO_INCLUDE_DIR ${mio_SOURCE_DIR}/include)

# Add Catch2
add_subdirectory(vendor/catch2)

# Parser library (shared between main executable and tests)
add_library(DBPFKitLib STATIC
    vendor/inih/ini.c
    src/RUL0.cpp
    src/FSHReader.cpp
    src/QFSDecompressor.cpp
    src/ExemplarReader.cpp
    src/ExemplarStructures.cpp
    src/LTextReader.cpp
    src/DBPFReader.cpp
    src/MappedFile.cpp
    src/S3DReader.cpp
    src/TGI.cpp
)
target_include_directories(DBPFKitLib PUBLIC
    src
    vendor/inih
    ${MIO_INCLUDE_DIR}
)
target_link_libraries(DBPFKitLib PRIVATE libsquish::Squish)

# Main executable (if you have a main.cpp later)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
    add_executable(DBPFKit src/main.cpp
            src/RUL0.cpp
            src/RUL0.h
            vendor/inih/ini.c
    )
    target_link_libraries(DBPFKit PRIVATE DBPFKitLib)
    target_include_directories(DBPFKit PRIVATE vendor/inih)
endif()

# Test executable
add_executable(DBPFKitTests
    tests/tests.cpp
)
target_link_libraries(DBPFKitTests PRIVATE
    DBPFKitLib
    libsquish::Squish
    Catch2::Catch2WithMain
)
target_compile_definitions(
        DBPFKitLib PUBLIC INI_MAX_LINE=1000
)

# Enable testing
enable_testing()
add_test(NAME AllTests COMMAND DBPFKitTests)
