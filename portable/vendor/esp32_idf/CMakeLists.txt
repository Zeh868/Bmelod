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

# arch 根目录：供 #include "xtensa/bm_arch_isr_fpu.h" 使用，与 arch 层既有的
# "<archid>/文件名" 引用惯例一致（见 portable/arch/aarch64/bm_arch_portmacro.h 等）。
set(_BM_ARCH_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../arch)

add_library(bm_vendor_esp32_idf STATIC
    bm_vendor_singleton_esp32_idf.c
    bm_vendor_pwm_esp32_idf.c
    bm_vendor_adc_esp32_idf.c
    bm_vendor_i2c_esp32_idf.c
    bm_vendor_encoder_esp32_idf.c
    bm_vendor_bmi160_esp32_idf.c
    bm_hal_cpu_freq_esp32.c
)

# 公开 include 目录（框架根通过 BM_FRAMEWORK_ROOT / bm_config 传入）
target_include_directories(bm_vendor_esp32_idf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_BM_ARCH_ROOT_DIR})

# Bmelod 内部 include 路径（由 bmelod_library.cmake 的 bm_target_internal_includes 提供）：
#   include/drv        — 驱动 API 表（bm_drv_*.h）
#   include/bm/common  — 公共框架类型（bm_types.h 等）
#   include/port       — 架构层契约（bm_arch_ops.h、bm_port_arch.h）
bm_target_internal_includes(bm_vendor_esp32_idf)
target_link_libraries(bm_vendor_esp32_idf PUBLIC bm_config)

# ESP ISR FPU(CP0) 守卫（portable/arch/xtensa/bm_arch_isr_fpu.h，Phase 3 后下沉
# 为 arch 层原语，原 vendor 私有头 bm_vendor_esp32_isr_fpu.h 已退役）引用
# xthal_save_cp0 / xthal_get_cpenable 等，这些符号由 IDF xtensa 组件的预编译
# libxt_hal.a 提供。在 IDF 工程内构建时（ESP_PLATFORM），把该归档作为 vendor
# 目标的链接依赖：vendor 是其使用方，CMake 据依赖关系把 libxt_hal.a 排在
# vendor 之后，闭合 GNU ld 单遍扫描下的 xthal_* 引用。
#
# BM_ARCH_XTENSA_HAS_XTHAL：显式声明「本目标编译环境有 xthal 头与库」，打开
# bm_arch_isr_fpu.h 的真实 CP0 守卫实现。本 vendor 无条件经 IDF 头编译
# （hal/timer_ll.h 等本就依赖 IDF include 注入，见 bm_sdk_esp32_idf_apply），
# 故此宏无条件 PRIVATE 定义——与旧头无条件 include xtensa/hal.h 的行为等价。
# 【教训（2026-07-11 真机回归）】不得用 ESP_PLATFORM 这类环境宏做门控：该
# C 宏只注入 idf.py 组件目标，本 vendor 静态库 TU 里并不定义，曾导致守卫被
# 预处理成 no-op、真机 tick/PWM ISR 失去 CP0 保护（Coprocessor exception）。
# 门控宏必须由知道 xthal 可用性的构建目标（即本文件）显式给出。
# 注意：_compilecheck/ 有独立 harness 目标（bm_vendor_esp32_check），不定义
# 此宏，走 no-op 分支，勿在彼处添加。
target_compile_definitions(bm_vendor_esp32_idf PRIVATE
    BM_ARCH_XTENSA_HAS_XTHAL=1)
if(DEFINED ESP_PLATFORM AND DEFINED ENV{IDF_PATH})
    if(NOT DEFINED IDF_TARGET)
        set(IDF_TARGET "esp32")
    endif()
    target_link_libraries(bm_vendor_esp32_idf PUBLIC
        "$ENV{IDF_PATH}/components/xtensa/${IDF_TARGET}/libxt_hal.a")
endif()

# BM_HAL_CPU_HAS_PORT_FREQ：声明本 vendor 已提供 CPU 主频三接口
# （bm_hal_cpu_freq_esp32.c 中的 freq_hz/freq_points/freq_set），令桩
# `Source/hal/bm_hal_cpu_stub.c` 让出同名符号、避免重复定义。
# 【机制说明，非充分条件】此处的 PUBLIC 定义只对本 vendor 目标的**消费方**
# （即 target_link_libraries 链接了 bm_vendor_esp32_idf 的目标）生效；桩
# `bm_hal_cpu_stub.c` 属 `bm_hal` 目标，而 `bm_hal` 只链 `bm_config`、并不
# 依赖本 vendor（依赖方向是 vendor→bm_config，不是 bm_hal→vendor），因此
# 这条 PUBLIC 定义**到不了**桩所在的编译单元。真正让桩让出符号的途径是：
# 消费方 ESP-IDF 工程**全局**定义 `BM_HAL_CPU_HAS_PORT_FREQ`（idf.py 组件级
# `-D`，或经 `bm_config.h` 的 include 链，如在其 `bm_config_app.h` 里
# `#define`），使其在编译 `bm_hal_cpu_stub.c` 时可见。本行 PUBLIC 定义予以
# 保留，仅作为“消费方若确实令 `bm_hal` 依赖本 vendor 目标”这一非常规接线
# 下的兜底，不能单独依赖它去重；若两侧都未生效，链接期会出现桩与本 vendor
# 的 freq 三函数重复定义。
target_compile_definitions(bm_vendor_esp32_idf PUBLIC
    BM_DRV_HAS_BACKEND
    BM_ESP32_BAREMETAL=1
    BM_HAL_CPU_HAS_PORT_FREQ
    BM_PORT_ARCH_ID=\"xtensa\")

# Plan B 缺口②：仅当上层关闭单核 CPU stub 时才编入真实 CPU-ID 实现，
# 避免与 Source/hal/bm_hal_cpu_stub.c 重复定义 bm_hal_cpu_id()。
if(NOT BM_HAL_USE_CPU_STUB)
    target_sources(bm_vendor_esp32_idf PRIVATE bm_hal_cpu_esp32_idf.c)
endif()
