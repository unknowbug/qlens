set(CMAKE_AUTOMOC ON)

add_executable(qlens_manager
    main.cpp
    ManagerWindow.cpp
    ThumbnailGrid.cpp
    FolderPanel.cpp
    TagPanel.cpp
    TagStore.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ViewerWidget.cpp
    ${QLENS_RC}
    qlens.qrc
)

target_link_libraries(qlens_manager PRIVATE qlens_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql)
target_include_directories(qlens_manager PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
