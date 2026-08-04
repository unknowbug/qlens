cmake_minimum_required(VERSION 3.22)
project(qlens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 依赖查找 ──────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql LinguistTools Concurrent)
find_package(libraw REQUIRED)
find_package(lcms REQUIRED)

# ── 子项目 ────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 应用图标资源（configure 注入绝对路径，供两个 exe 共用）
configure_file(${CMAKE_SOURCE_DIR}/qlens.rc ${CMAKE_BINARY_DIR}/qlens.rc @ONLY)
set(QLENS_RC ${CMAKE_BINARY_DIR}/qlens.rc)

add_subdirectory(src/core)
add_subdirectory(src/manager)
