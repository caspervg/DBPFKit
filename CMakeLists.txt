cmake_minimum_required(VERSION 3.20)
project(SC4RULParser)

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
add_library(SC4RULParserLib STATIC
    vendor/inih/ini.c
    src/RUL0.cpp
    src/FSHReader.cpp
    src/QFSDecompressor.cpp
    src/Exemplar.cpp
    src/DBPFReader.cpp
    src/S3DReader.cpp
    src/TGI.cpp
)
target_include_directories(SC4RULParserLib PUBLIC
    src
    vendor/inih
)
target_link_libraries(SC4RULParserLib PRIVATE libsquish::Squish)

# Main executable (if you have a main.cpp later)
if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
    add_executable(SC4RULParser src/main.cpp
            src/RUL0.cpp
            src/RUL0.h
            vendor/inih/ini.c
    )
    target_link_libraries(SC4RULParser PRIVATE SC4RULParserLib)
    target_include_directories(SC4RULParser PRIVATE vendor/inih)
endif()

# Test executable
add_executable(SC4RULParserTests
    tests/tests.cpp
)
target_link_libraries(SC4RULParserTests PRIVATE
    SC4RULParserLib
    libsquish::Squish
    Catch2::Catch2WithMain
)

# Enable testing
enable_testing()
add_test(NAME AllTests COMMAND SC4RULParserTests)

add_executable(SC4RULParserGui src/gui_main.cpp)
target_link_libraries(SC4RULParserGui PRIVATE
    SC4RULParserLib
    raylib
    imgui
    rlimgui
)
