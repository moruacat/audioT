# ============================================================
# FetchDependencies.cmake - 第三方依赖管理
# ============================================================
include(FetchContent)

# ---------- KissFFT (float 类型) ----------
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)

# KissFFT 需要 KISSFFT_DATATYPE=float 来使用 float 精度
set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)

# 禁用 KissFFT 自带的 tools 和 test（需要 pkg-config，Windows 上通常没有）
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(kissfft)

# KissFFT 的 CMake 目标名通常为 kissfft（或 kissfft-float）
# 创建一个别名方便统一引用
if(NOT TARGET kissfft::kissfft)
    if(TARGET kissfft)
        add_library(kissfft::kissfft ALIAS kissfft)
    elseif(TARGET kissfft-float)
        add_library(kissfft::kissfft ALIAS kissfft-float)
    endif()
endif()

# KissFFT 是纯 C 库，禁用 Qt AUTOMOC 避免警告
if(TARGET kissfft)
    set_target_properties(kissfft PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
elseif(TARGET kissfft-float)
    set_target_properties(kissfft-float PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()

message(STATUS "AudioT: KissFFT fetched (datatype=float)")
