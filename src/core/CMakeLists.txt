set(CMAKE_AUTOMOC ON)
if(MSVC)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(CORE_SOURCES
    loader.cpp
    thumbnail.cpp
    color.cpp
    hdr_detect.cpp
    clipboard.cpp
    decode_api.cpp
    # ── qlens_decode（解码 + 插件系统，从 quickview 并入）──
    ${CMAKE_SOURCE_DIR}/src/quickview/decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/quickview/plugins.cpp
    ${CMAKE_SOURCE_DIR}/src/quickview/i18n.cpp
)

add_library(qlens_core SHARED ${CORE_SOURCES})

target_link_libraries(qlens_core PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql
    ${LibRaw_r_LIBRARIES}
    lcms::lcms
    windowscodecs shlwapi
)
target_include_directories(qlens_core PRIVATE ${LibRaw_INCLUDE_DIR})
target_include_directories(qlens_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# qlens_decode 头文件（decoder.h/plugins.h）
target_include_directories(qlens_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src/quickview
)

if(WIN32)
    target_sources(qlens_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/win/hdr_win.cpp
    )
elseif(APPLE)
    target_sources(qlens_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/mac/hdr_mac.mm
    )
else()
    target_sources(qlens_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/linux/hdr_linux.cpp
    )
endif()
