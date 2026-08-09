# ============================================================
# src/core/CMakeLists.txt - 核心库 (零 Qt 依赖)
# ============================================================

# 收集所有源文件
file(GLOB_RECURSE CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# 如果 GLOB 结果为空（初始阶段），创建占位目标
if(NOT CORE_SOURCES)
    message(STATUS "AudioT core: 无源文件，创建占位目标")
    add_library(audiot_core STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/core_placeholder.cpp"
    )
else()
    add_library(audiot_core STATIC ${CORE_SOURCES})
endif()

target_include_directories(audiot_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

# 链接 KissFFT
if(TARGET kissfft::kissfft)
    target_link_libraries(audiot_core PUBLIC kissfft::kissfft)
elseif(TARGET kissfft)
    target_link_libraries(audiot_core PUBLIC kissfft)
endif()

# 确保 core 层不链接 Qt
# (不在此处调用 find_package(Qt5))
