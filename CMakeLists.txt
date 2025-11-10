cmake_minimum_required(VERSION 3.20)
project(SC4RULParser)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Catch2
add_subdirectory(vendor/catch2)

# Parser library (shared between main executable and tests)
add_library(SC4RULParserLib STATIC
    vendor/inih/ini.c
    src/RUL0.cpp
)
target_include_directories(SC4RULParserLib PUBLIC src)
target_include_directories(SC4RULParserLib PRIVATE vendor/inih)

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
    Catch2::Catch2WithMain
)

# Enable testing
enable_testing()
add_test(NAME AllTests COMMAND SC4RULParserTests)
