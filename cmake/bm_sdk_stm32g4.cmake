# STM32G4 CMSIS 设备头 + LL（Low-Layer）驱动（STM32Cube 固件包）集成
#
# 缓存变量：
#   BM_STM32_CUBE_PATH  — Cube 固件包根目录（含 Drivers/CMSIS/... 与
#                         Drivers/STM32G4xx_HAL_Driver/...）
#   BM_STM32_DEVICE     — 器件宏，默认 STM32G474xx（NUCLEO-G474RE 参考板）
#   BM_STM32G4_STARTUP  — 启动汇编（Cube gcc 模板），供应用工程 target_sources 引用
#   BM_STM32G4_LD       — gcc 链接脚本（Cube CubeMX 模板），供应用工程
#                         target_link_options(-T ...) 引用
#
# 启动/链接文件不复制进仓库（第三方许可），由应用工程经上述变量从 Cube 包
# 引用；SystemInit/SystemCoreClockUpdate 由应用工程从 Cube 引入
# system_stm32g4xx.c（vendor 不接管时钟树）。用法示例：
#   target_sources(app PRIVATE ${BM_STM32G4_STARTUP}
#       ${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Source/Templates/system_stm32g4xx.c)
#   target_link_options(app PRIVATE -T${BM_STM32G4_LD})
#
# 实现路线：vendor（portable/vendor/stm32g4）用 STM32 LL 库实现外设驱动。
# LL 以 __STATIC_INLINE 头文件为主；仅下列非内联函数所在的 LL .c 编入
# 使用方目标（BM_STM32G4_LL_SOURCES，目前 vendor 只用到
# LL_RCC_GetSystemClocksFreq()，位于 stm32g4xx_ll_rcc.c，该文件整体受
# USE_FULL_LL_DRIVER 门控，故必须定义此宏；不定义 USE_FULL_ASSERT，
# assert_param 即空操作、无需提供 assert_failed）。刻意不定义
# USE_HAL_DRIVER（LL 独立可用；stm32g4xx.h 以 `#if defined(USE_HAL_DRIVER)`
# 判定包含 stm32g4xx_hal.h，定义成 0 也会触发包含）。

set(BM_STM32_DEVICE "STM32G474xx" CACHE STRING
    "STM32 CMSIS device define (e.g. STM32G474xx)")

set(BM_STM32G4_STARTUP
    "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Source/Templates/gcc/startup_stm32g474xx.s"
    CACHE FILEPATH "STM32G4 startup assembly (Cube gcc template)")

set(BM_STM32G4_LD
    "${BM_STM32_CUBE_PATH}/Projects/NUCLEO-G474RE/Templates/STM32CubeIDE/STM32G474RETX_FLASH.ld"
    CACHE FILEPATH "STM32G4 gcc linker script (Cube CubeMX template)")

# vendor 实际用到的 LL .c（非内联函数所在文件，相对 HAL_Driver/Src 路径），
# 由 bm_sdk_stm32g4_apply 拼上 Cube 路径后编入目标；新增对其它 LL .c
# 非内联 API 的调用时在此追加。
set(BM_STM32G4_LL_SOURCES
    "stm32g4xx_ll_rcc.c")

function(bm_sdk_stm32g4_apply TARGET)
    if(NOT BM_STM32_CUBE_PATH)
        message(FATAL_ERROR
            "BM_STM32_CUBE_PATH is required for sdk_stm32g4 backend.\n"
            "Point it at an STM32CubeG4 firmware package root, e.g.\n"
            "  -DBM_STM32_CUBE_PATH=/path/to/STM32CubeG4\n"
            "（需含 Drivers/CMSIS 与 Drivers/STM32G4xx_HAL_Driver，"
            "git 稀疏检出时注意后者是 submodule，须 submodule update）")
    endif()

    set(_cmsis_device "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Include")
    set(_cmsis_core "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Include")
    set(_ll_inc "${BM_STM32_CUBE_PATH}/Drivers/STM32G4xx_HAL_Driver/Inc")

    if(NOT EXISTS "${_cmsis_device}/stm32g4xx.h")
        message(FATAL_ERROR
            "STM32G4 CMSIS device headers not found under:\n  ${_cmsis_device}")
    endif()
    if(NOT EXISTS "${_cmsis_core}/core_cm4.h")
        message(FATAL_ERROR
            "ARM CMSIS core headers not found under:\n  ${_cmsis_core}")
    endif()
    if(NOT EXISTS "${_ll_inc}/stm32g4xx_ll_tim.h")
        message(FATAL_ERROR
            "STM32G4 LL driver headers not found under:\n  ${_ll_inc}\n"
            "Drivers/STM32G4xx_HAL_Driver 是 STM32CubeG4 的 submodule，"
            "请在该仓库执行：\n"
            "  git submodule update --init Drivers/STM32G4xx_HAL_Driver")
    endif()
    set(_ll_srcs "")
    foreach(_ll_src IN LISTS BM_STM32G4_LL_SOURCES)
        set(_ll_src_abs
            "${BM_STM32_CUBE_PATH}/Drivers/STM32G4xx_HAL_Driver/Src/${_ll_src}")
        if(NOT EXISTS "${_ll_src_abs}")
            message(FATAL_ERROR
                "STM32G4 LL driver source not found:\n  ${_ll_src_abs}")
        endif()
        list(APPEND _ll_srcs "${_ll_src_abs}")
    endforeach()

    target_include_directories(${TARGET} PRIVATE
        "${_cmsis_device}"
        "${_cmsis_core}"
        "${_ll_inc}"
    )
    target_compile_definitions(${TARGET} PRIVATE
        ${BM_STM32_DEVICE}
        USE_FULL_LL_DRIVER
    )
    target_sources(${TARGET} PRIVATE ${_ll_srcs})
endfunction()
