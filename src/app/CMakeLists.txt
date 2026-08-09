# ============================================================
# src/app/CMakeLists.txt - 可执行程序
# ============================================================

file(GLOB_RECURSE APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if(NOT APP_SOURCES)
    message(FATAL_ERROR "AudioT app: 未找到源文件，至少需要 main.cpp")
endif()

# 收集可选资源文件（如 resources.qrc）
file(GLOB APP_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")

# Windows 资源文件（应用图标）直接加入源列表，MinGW 会自动用 windres 编译
if(WIN32)
    list(APPEND APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

# UI 资源（logo 等）需要编入可执行目标，
# 否则静态库中的 qrc 目标代码不会被链接器拉出
if(AUDIOT_USE_QT)
    list(APPEND APP_RESOURCES "${CMAKE_SOURCE_DIR}/src/ui/resources.qrc")
endif()

add_executable(AudioT ${APP_SOURCES} ${APP_RESOURCES})

target_include_directories(AudioT
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(AUDIOT_USE_QT)
    target_link_libraries(AudioT
        PRIVATE
            audiot_core
            audiot_audio
            audiot_ui
            Qt5::Core
            Qt5::Gui
            Qt5::Widgets
            Qt5::Multimedia
    )
    target_compile_definitions(AudioT PRIVATE AUDIOT_HAS_QT=1)
else()
    target_link_libraries(AudioT
        PRIVATE
            audiot_core
            audiot_audio
    )
endif()

# Windows 子系统设置
# Qt GUI 模式使用 WIN32 子系统（无控制台窗口）
# CLI 功能仍可通过在终端中运行来使用
# 应用图标通过 app.rc（源列表中的资源文件）嵌入
if(WIN32)
    if(AUDIOT_USE_QT)
        set_target_properties(AudioT PROPERTIES WIN32_EXECUTABLE TRUE)
    else()
        set_target_properties(AudioT PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
endif()

# Phase 4: Windows + Qt 环境下构建后自动运行 windeployqt 打包
include(deploy)
