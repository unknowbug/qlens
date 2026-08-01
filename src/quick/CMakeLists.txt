set(QUICK_SOURCES
    main.cpp
    MainWindow.cpp
)

add_executable(qlens_quick WIN32 ${QUICK_SOURCES} ${QLENS_RC} qlens.qrc)
set_target_properties(qlens_quick PROPERTIES AUTOMOC ON)

target_link_libraries(qlens_quick PRIVATE
    qlens_core
    Qt6::Core Qt6::Gui Qt6::Widgets
)
target_include_directories(qlens_quick PRIVATE ${CMAKE_SOURCE_DIR}/src/core)

# 国际化
qt_add_translations(qlens_quick TS_FILES qlens_zh_CN.ts)

# 部署翻译文件到输出目录
add_custom_command(TARGET qlens_quick POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:qlens_quick>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/qlens_zh_CN.qm"
        "$<TARGET_FILE_DIR:qlens_quick>/translations/"
)

# 注册为默认看图器（Windows 安装时）
if(WIN32 AND CMAKE_INSTALL_PREFIX)
    set_target_properties(qlens_quick PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# 部署 Qt DLL（windeployqt）
find_program(WINDEPLOYQT windeployqt
    HINTS ${CMAKE_PREFIX_PATH}/../../bin
    PATHS D:/qt/6.11.1/msvc2022_64/bin
)
if(NOT WINDEPLOYQT)
    message(WARNING "windeployqt not found. Run manually:\n"
        "  \"D:\\qt\\6.11.1\\msvc2022_64\\bin\\windeployqt\" build\\bin\\Debug")
else()
    add_custom_command(TARGET qlens_quick POST_BUILD
        COMMAND ${WINDEPLOYQT} --no-translations --no-opengl-sw
            "$<TARGET_FILE_DIR:qlens_quick>"
        COMMENT "Deploying Qt DLLs..."
    )
endif()
