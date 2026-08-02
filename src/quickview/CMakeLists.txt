cmake_minimum_required(VERSION 3.22)
# 原生 QuickView —— Win32 + WIC + D3D11（弃 Qt）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(qlens_quickview WIN32
    main.cpp
    window.cpp
    decoder.cpp
    renderer.cpp
    thumbstrip.cpp
    thumbstrip.h
    plugins.cpp
    plugins.h
    hdr.cpp
    hdr.h
)
target_link_libraries(qlens_quickview PRIVATE d3d11 dxgi dwmapi windowscodecs shlwapi d3dcompiler)
target_compile_options(qlens_quickview PRIVATE /utf-8)

# 最小窗口测试（验证环境 Win32 窗口支持）
add_executable(qlens_minimal WIN32 minimal.cpp)
target_link_libraries(qlens_minimal PRIVATE user32)
