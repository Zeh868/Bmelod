# STM32G4 CMSIS 设备头（STM32Cube 固件包）集成
#
# 缓存变量：
#   BM_STM32_CUBE_PATH  — Cube 固件包根目录（含 Drivers/CMSIS/...）
#   BM_STM32_DEVICE     — 器件宏，默认 STM32G474xx（NUCLEO-G474RE 参考板）

set(BM_STM32_DEVICE "STM32G474xx" CACHE STRING
    "STM32 CMSIS device define (e.g. STM32G474xx)")

function(bm_sdk_stm32g4_apply TARGET)
    if(NOT BM_STM32_CUBE_PATH)
        message(FATAL_ERROR
            "BM_STM32_CUBE_PATH is required for sdk_stm32g4 backend.\n"
            "Point it at an STM32CubeG4 firmware package root, e.g.\n"
            "  -DBM_STM32_CUBE_PATH=/path/to/STM32CubeG4")
    endif()

    set(_cmsis_device "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Include")
    set(_cmsis_core "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Include")

    if(NOT EXISTS "${_cmsis_device}/stm32g4xx.h")
        message(FATAL_ERROR
            "STM32G4 CMSIS device headers not found under:\n  ${_cmsis_device}")
    endif()
    if(NOT EXISTS "${_cmsis_core}/core_cm4.h")
        message(FATAL_ERROR
            "ARM CMSIS core headers not found under:\n  ${_cmsis_core}")
    endif()

    target_include_directories(${TARGET} PRIVATE
        "${_cmsis_device}"
        "${_cmsis_core}"
    )
    target_compile_definitions(${TARGET} PRIVATE
        ${BM_STM32_DEVICE}
        USE_HAL_DRIVER=0
    )
endfunction()
