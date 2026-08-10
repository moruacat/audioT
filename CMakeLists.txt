cmake_minimum_required(VERSION 3.16)
project(AudioT VERSION 1.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 自动工具（稍后在找到 Qt 后再设置）

# 包含自定义 CMake 模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(FetchDependencies)

# 选项
option(AUDIOT_BUILD_TESTS "Build unit tests" ON)
option(AUDIOT_USE_QT "Enable Qt UI (requires Qt5)" ON)

# 自动检测 Qt 路径（如果未通过 CMAKE_PREFIX_PATH 指定）
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        file(GLOB QT_PATHS "D:/Qt/5.15.2/*")
        foreach(path ${QT_PATHS})
            if(EXISTS "${path}/lib/cmake/Qt5")
                list(APPEND CMAKE_PREFIX_PATH "${path}")
            endif()
        endforeach()
    endif()
endif()

# 查找 Qt5（QUIET 模式，找不到则禁用 UI）
if(AUDIOT_USE_QT)
    find_package(Qt5 5.15 COMPONENTS Core Gui Widgets Multimedia QUIET)
    if(NOT Qt5_FOUND)
        message(STATUS "Qt5 not found - UI will be disabled. Set CMAKE_PREFIX_PATH to Qt install path to enable.")
        set(AUDIOT_USE_QT OFF)
    else()
        message(STATUS "Qt5 found - UI will be built (version: ${Qt5_VERSION})")
        # 找到 Qt 后才启用自动工具
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)
    endif()
endif()

# 子项目
add_subdirectory(src/core)
add_subdirectory(src/audio)

if(AUDIOT_USE_QT)
    add_subdirectory(src/ui)
endif()

add_subdirectory(src/app)

if(AUDIOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
