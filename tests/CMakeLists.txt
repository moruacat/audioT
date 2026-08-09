# ============================================================
# tests/CMakeLists.txt - 单元测试 (Catch2 v2 header-only)
# ============================================================

include(FetchContent)

# ---------- Catch2 v2.x (header-only) ----------
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.13.10
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(catch2)

# Catch2 v2 目标名为 Catch2，创建别名方便引用
if(NOT TARGET Catch2::Catch2)
    add_library(Catch2::Catch2 ALIAS Catch2)
endif()

# 收集所有 test_*.cpp 文件
file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

if(NOT TEST_SOURCES)
    message(STATUS "AudioT tests: 无测试文件，跳过")
    return()
endif()

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name "${test_src}" NAME_WE)

    add_executable(${test_name} "${test_src}")

    target_link_libraries(${test_name}
        PRIVATE
            audiot_core
            audiot_audio
            Catch2::Catch2
    )

    target_include_directories(${test_name}
        PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
    )

    # 注册到 CTest
    add_test(NAME ${test_name} COMMAND ${test_name})

    # 设置 PATH 以便测试能找到 kissfft DLL
    if(WIN32)
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/_deps/kissfft-build;$ENV{PATH}"
        )
    endif()
endforeach()

message(STATUS "AudioT tests: 已注册测试")
