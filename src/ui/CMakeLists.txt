# ============================================================
# src/ui/CMakeLists.txt - Qt UI 库
# ============================================================

file(GLOB_RECURSE UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE UI_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE UI_FORMS   "${CMAKE_CURRENT_SOURCE_DIR}/*.ui")
# 注意：*.qrc 不在此处编译。资源文件由 app 目标统一编译链接，
# 避免同一 qrc 在静态库和 exe 中重复注册导致堆损坏。

add_library(audiot_ui STATIC
    ${UI_SOURCES}
    ${UI_HEADERS}
    ${UI_FORMS}
)

target_include_directories(audiot_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(audiot_ui
    PUBLIC
        audiot_core
        audiot_audio
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Multimedia
)
