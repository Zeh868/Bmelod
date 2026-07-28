# vendor/stm32g4 — STM32G474xB（NUCLEO-G474RE 参考板）裸机外设层
#
# 基于 STM32 LL（Low-Layer）库的后端：LL 头文件与所用 .c 经
# bm_sdk_stm32g4_apply()（cmake/bm_sdk_stm32g4.cmake）从 Cube 包注入，
# 刻意不定义 USE_HAL_DRIVER（LL 独立可用）。
# 经 BM_BACKEND=sdk_stm32g4 由 portable/packs/sdk_stm32g4 组合
# arch/armv7em + 本目录一起引入。
#
# include 目录说明：
#   include/drv       — 驱动 API 表（bm_drv_timer.h 等），Zephyr 同构 vtable
#   include/bm/common — 公共框架类型（bm_types.h 等）
#   include/hal       — HAL 契约（bm_hal_pwm.h 等）与 uptime 后端接口

# arch 根目录：供 #include "armv7em/bm_arch_isr_fpu.h" 使用，与 arch 层既有的
# "<archid>/文件名" 引用惯例一致（esp32 vendor 同款写法）。
set(_BM_ARCH_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../arch)

add_library(bm_vendor_stm32g4 STATIC
    bm_vendor_singleton_stm32g4.c
    bm_vendor_pwm_stm32g4.c
    bm_vendor_adc_stm32g4.c
    bm_vendor_encoder_stm32g4.c
    bm_vendor_comp_stm32g4.c
    bm_vendor_gpio_stm32g4.c
    bm_vendor_spi_stm32g4.c
    bm_vendor_uart_dev_stm32g4.c
    bm_vendor_dma_usart2_rx_stm32g4.c
    bm_vendor_hrtimer_stm32g4.c    # 接口批 1：高精度 Timer STM32G4 后端
    bm_hal_cpu_freq_stm32g4.c
)

# 公开 include 目录（框架根通过 BM_FRAMEWORK_ROOT / bm_config 传入）
target_include_directories(bm_vendor_stm32g4 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_BM_ARCH_ROOT_DIR})

# Bmelod 内部 include 路径（bm_include_dirs.cmake 的 bm_target_internal_includes）
bm_target_internal_includes(bm_vendor_stm32g4)
target_link_libraries(bm_vendor_stm32g4 PUBLIC bm_config)

# BM_DRV_HAS_BACKEND：声明本后端提供 bm_drv_*_api 单例符号
# （timer/uart/wdg），令 Source/hal 分发层走后端转发而非桩。
# BM_PORT_ARCH_ID 由 pack 链接的 bm_port_arch_armv7em 目标 PUBLIC 提供，不重复定义。
target_compile_definitions(bm_vendor_stm32g4 PUBLIC
    BM_DRV_HAS_BACKEND)

# BM_HAL_CPU_HAS_PORT_FREQ 的去重接线不在此处：bm_hal_cpu_stub.c 属 bm_hal
# 目标，bm_hal 只链 bm_config、不依赖本 vendor，vendor 侧 PUBLIC 定义到不了
# 桩所在编译单元（机制见 docs/03-移植与IDE集成/01-HAL契约与移植要点.md
# 133-148 行）。该宏由 pack（portable/packs/sdk_stm32g4/CMakeLists.txt）
# 在 bm_hal 目标存在后直接 target_compile_definitions(bm_hal PRIVATE ...)
# 注入，见该文件注释。
