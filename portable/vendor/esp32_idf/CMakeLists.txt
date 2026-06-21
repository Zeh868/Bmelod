# vendor/esp32_idf — 灯哥 V4 平衡车 ESP32-WROOM-32E 裸机外设层
#
# 该目录只保留独立 CMake 裸机静态库路径：不再作为 ESP-IDF App 组件注册，
# 也不引入 FreeRTOS、任务、队列或事件循环。上层工程通过
# `portable/packs/sdk_esp32_idf` 组合 `arch/xtensa` + 本目录即可。
#
# include 目录说明（Phase 3 正式归档）：
#   include/drv      — 驱动 API 表（bm_drv_timer.h 等），Zephyr 同构 vtable
#   include/bm/common — 公共框架类型（bm_types.h 等）
#   include/port     — 架构层契约（bm_arch_ops.h、bm_port_arch.h）

set(_BM_XTENSA_ARCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../arch/xtensa)

add_library(bm_vendor_esp32_idf STATIC
    bm_vendor_singleton_esp32_idf.c
    bm_vendor_pwm_esp32_idf.c
    bm_vendor_adc_esp32_idf.c
    bm_vendor_i2c_esp32_idf.c
    bm_vendor_encoder_esp32_idf.c
    bm_vendor_bmi160_esp32_idf.c
)

# 公开 include 目录（框架根通过 BM_FRAMEWORK_ROOT / bm_config 传入）
target_include_directories(bm_vendor_esp32_idf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

# Bmelod 内部 include 路径（由 bmelod_library.cmake 的 bm_target_internal_includes 提供）：
#   include/drv        — 驱动 API 表（bm_drv_*.h）
#   include/bm/common  — 公共框架类型（bm_types.h 等）
#   include/port       — 架构层契约（bm_arch_ops.h、bm_port_arch.h）
bm_target_internal_includes(bm_vendor_esp32_idf)
target_link_libraries(bm_vendor_esp32_idf PUBLIC bm_config)

# ESP ISR FPU(CP0) 守卫（bm_vendor_esp32_isr_fpu.h）引用 xthal_save_cp0 /
# xthal_get_cpenable 等，这些符号由 IDF xtensa 组件的预编译 libxt_hal.a 提供。
# 在 IDF 工程内构建时（ESP_PLATFORM），把该归档作为 vendor 目标的链接依赖：
# vendor 是其使用方，CMake 据依赖关系把 libxt_hal.a 排在 vendor 之后，闭合 GNU ld
# 单遍扫描下的 xthal_* 引用。独立裸机/compilecheck 构建（无 ESP_PLATFORM）不引入。
if(DEFINED ESP_PLATFORM AND DEFINED ENV{IDF_PATH})
    if(NOT DEFINED IDF_TARGET)
        set(IDF_TARGET "esp32")
    endif()
    target_link_libraries(bm_vendor_esp32_idf PUBLIC
        "$ENV{IDF_PATH}/components/xtensa/${IDF_TARGET}/libxt_hal.a")
endif()
target_compile_definitions(bm_vendor_esp32_idf PUBLIC
    BM_DRV_HAS_BACKEND
    BM_ESP32_BAREMETAL=1
    BM_PORT_ARCH_ID=\"xtensa\")
