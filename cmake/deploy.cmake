# ============================================================
# cmake/deploy.cmake - windeployqt 打包支持 (Phase 4)
# ============================================================
# 在 Windows + Qt 环境下，构建后自动调用 windeployqt，
# 将 Qt 运行时 DLL、插件、平台主题等复制到可执行文件目录，
# 使生成的 AudioT.exe 可脱离 Qt 开发环境直接运行。
#
# 也可手动执行（等效命令）：
#   windeployqt --no-translations <build>/src/app/AudioT.exe

if(WIN32 AND AUDIOT_USE_QT AND TARGET AudioT)
    # 优先从 Qt5_DIR 推导工具目录（Qt5_DIR 形如 <qt>/lib/cmake/Qt5）
    find_program(WINDEPLOYQT windeployqt
        HINTS "${Qt5_DIR}/../../../bin"
    )

    if(WINDEPLOYQT)
        message(STATUS "AudioT deploy: windeployqt found - ${WINDEPLOYQT}")

        # 注意：不强制传 --release / --debug。
        # 部分 Qt MinGW 安装中的插件 DLL 会被 windeployqt 的 PE 分析
        # 误判为 debug 构建，强制 --release 会导致平台插件被过滤掉，
        # 报 "Unable to find the platform plugin"。默认自动模式下
        # windeployqt 会根据 exe 自动选择并部署所需运行时。
        add_custom_command(TARGET AudioT POST_BUILD
            COMMAND ${WINDEPLOYQT}
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    $<TARGET_FILE:AudioT>
            COMMENT "Running windeployqt on $<TARGET_FILE_NAME:AudioT>..."
            VERBATIM
        )
    else()
        message(STATUS "AudioT deploy: windeployqt not found - skipping Qt DLL packaging")
    endif()
endif()

# Copy KissFFT DLL to output directory
# windeployqt 只部署 Qt 自身 DLL，第三方共享库需手动拷贝，
# 否则运行时因导入表缺库报 0xC0000135 STATUS_DLL_NOT_FOUND。
if(TARGET kissfft)
    set(_KISSFFT_TARGET kissfft)
elseif(TARGET kissfft-float)
    set(_KISSFFT_TARGET kissfft-float)
endif()

if(DEFINED _KISSFFT_TARGET)
    get_target_property(_KISSFFT_TYPE ${_KISSFFT_TARGET} TYPE)
    if(_KISSFFT_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET AudioT POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${_KISSFFT_TARGET}>
                $<TARGET_FILE_DIR:AudioT>
            COMMENT "Copying KissFFT DLL..."
        )
    endif()
endif()
