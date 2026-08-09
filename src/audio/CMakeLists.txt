# ============================================================
# src/audio/CMakeLists.txt - 音频处理库
# ============================================================

file(GLOB_RECURSE AUDIO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

if(NOT AUDIO_SOURCES)
    message(STATUS "AudioT audio: 无源文件，创建占位目标")
    add_library(audiot_audio STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/audio_placeholder.cpp"
    )
else()
    add_library(audiot_audio STATIC ${AUDIO_SOURCES})
endif()

target_include_directories(audiot_audio
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(audiot_audio
    PUBLIC audiot_core
)

# ---------- 平台音频后端条件编译 ----------
if(WIN32)
    # Windows: 使用 QtMultimedia（qt_audio_stream.cpp，需 Qt）
    # ALSA 源文件以空实现桩参与编译，保持跨平台可构建
    target_compile_definitions(audiot_audio PUBLIC AUDIOT_AUDIO_BACKEND_QT=1)
elseif(UNIX AND NOT APPLE)
    # Linux: 可选 ALSA 后端（找到 asound 则启用原生实现）
    find_package(ALSA QUIET)
    if(ALSA_FOUND)
        target_compile_definitions(audiot_audio PUBLIC AUDIOT_AUDIO_BACKEND_ALSA=1)
        target_include_directories(audiot_audio PUBLIC ${ALSA_INCLUDE_DIRS})
        target_link_libraries(audiot_audio PUBLIC ${ALSA_LIBRARIES})
        message(STATUS "AudioT audio: ALSA found - native Linux audio backend enabled")
    else()
        message(STATUS "AudioT audio: ALSA not found - alsa backend compiled as stub (WAV/Qt fallback)")
    endif()
endif()

# Phase 2: Link Qt Multimedia when Qt is available
if(AUDIOT_USE_QT)
    target_link_libraries(audiot_audio
        PUBLIC Qt5::Core Qt5::Multimedia
    )
    target_compile_definitions(audiot_audio PUBLIC AUDIOT_HAS_QT=1)
endif()
