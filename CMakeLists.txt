cmake_minimum_required(VERSION 3.22)
project(qlens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 依赖查找 ──────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql LinguistTools)
find_package(libraw REQUIRED)
find_package(lcms REQUIRED)

# ── 子项目 ────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_subdirectory(src/core)
add_subdirectory(src/quick)
add_subdirectory(src/manager)
