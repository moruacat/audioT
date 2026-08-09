# ============================================================
# CompilerFlags.cmake - 编译器标志配置 (MSVC / MinGW / GCC / Clang)
# ============================================================

# ---------- 通用设置 ----------
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Debug / Release 通用优化
if(MSVC)
    # ---------- MSVC ----------
    add_compile_options(
        /W4             # 高警告级别
        /MP             # 多处理器编译
        /utf-8          # 源文件与执行字符集均为 UTF-8
        /wd4100         # 未引用形参
        /wd4189         # 局部变量已初始化但未引用
        /wd4244         # 类型转换可能丢失数据
        /wd4267         # size_t -> int 转换
        /wd4456         # 局部变量隐藏外部同名变量
        /wd4457         # 函数参数隐藏外部同名变量
        /wd4458         # 成员变量隐藏
        /wd4459         # 全局变量隐藏
        /wd4702         # 不可达代码
        /wd4996         # 废弃 API
    )

    # MSVC runtime library configuration
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # MSVC 的 Debug/Release 标志由 CMake 自动处理 (/Od /Zi vs /O2)

elseif(MINGW)
    # ---------- MinGW / GCC ----------
    add_compile_options(
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )

    # 静态链接 libgcc / libstdc++，减少运行时 DLL 依赖
    add_link_options(-static -static-libgcc -static-libstdc++)

    # 根据构建类型设置优化标志（兼容单配置生成器如 MinGW Makefiles）
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
    else()
        add_compile_options(-O2 -g)
    endif()

else()
    # ---------- Linux / macOS: GCC / Clang ----------
    add_compile_options(
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -finput-charset=UTF-8
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
    else()
        add_compile_options(-O2 -g)
    endif()

    # macOS: 允许部署较新的 API
    if(APPLE)
        add_compile_options(-mmacosx-version-min=10.14)
    endif()
endif()

# ---------- Windows 7 兼容性（仅 Windows）----------
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0601
        WINVER=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ---------- 32 / 64 位支持 ----------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_compile_definitions(AUDIOT_64BIT=1)
    message(STATUS "AudioT: 64-bit build")
else()
    add_compile_definitions(AUDIOT_32BIT=1)
    message(STATUS "AudioT: 32-bit build")
endif()
