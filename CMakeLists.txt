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

find_package(raylib QUIET)
if (NOT raylib_FOUND)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_SHALLOW 1
    )
    FetchContent_MakeAvailable(raylib)
endif()

set(IMGUI_VERSION 1.92.4-docking)
set(IMGUI_TAG e7d2d636affeeac1fc7175fb13472ca02d1bc0d1)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG ${IMGUI_TAG}
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui INTERFACE ${imgui_SOURCE_DIR})

set (RLIMGUI_TAG 4d8a61842903978bc42adf3347cd34f4e6524efc)
FetchContent_Declare(
        rlimgui
        GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
        GIT_TAG ${RLIMGUI_TAG}
)
FetchContent_MakeAvailable(rlimgui)
add_library(rlimgui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR} PRIVATE ${raylib_SOURCE_DIR} ${imgui_SOURCE_DIR})
target_link_libraries(rlimgui PUBLIC imgui raylib)

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

# Enable testing
enable_testing()
add_test(NAME AllTests COMMAND DBPFKitTests)

add_executable(S3DModelViewer apps/s3d_model_viewer/main.cpp)
target_link_libraries(S3DModelViewer PRIVATE
    DBPFKitLib
    raylib
    imgui
    rlimgui
)
