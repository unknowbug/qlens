cmake_minimum_required(VERSION 3.22)
# 原生 QuickView —— Win32 + WIC + D3D11（弃 Qt）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 解码 + 插件独立库（QuickView / Manager 共享）──
add_library(qlens_decode STATIC
    decoder.cpp
    decoder.h
    plugins.cpp
    plugins.h
)
target_include_directories(qlens_decode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qlens_decode PUBLIC windowscodecs shlwapi)
target_compile_options(qlens_decode PRIVATE /utf-8)

add_executable(qlens_quickview WIN32
    main.cpp
    window.cpp
    renderer.cpp
    thumbstrip.cpp
    thumbstrip.h
    hdr.cpp
    hdr.h
)
target_link_libraries(qlens_quickview PRIVATE qlens_decode d3d11 dxgi dwmapi d3dcompiler comdlg32)
target_compile_options(qlens_quickview PRIVATE /utf-8)

# 最小窗口测试（验证环境 Win32 窗口支持）
add_executable(qlens_minimal WIN32 minimal.cpp)
target_link_libraries(qlens_minimal PRIVATE user32)
