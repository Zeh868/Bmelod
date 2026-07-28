# BM_BACKEND=sdk_stm32g4：arch/armv7em + vendor/stm32g4

include(${BM_FRAMEWORK_ROOT}/cmake/bm_sdk_stm32g4.cmake)

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/armv7em _bm_arch_armv7em_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/vendor/stm32g4 _bm_vendor_stm32g4_pack)

add_library(bm_backend_sdk_stm32g4 INTERFACE)
target_compile_definitions(bm_backend_sdk_stm32g4 INTERFACE
    BM_DRV_HAS_BACKEND
    BM_HAL_HAS_PRIORITY_MASK=1)
target_link_libraries(bm_backend_sdk_stm32g4 INTERFACE
    bm_config
    bm_port_arch_armv7em
    bm_vendor_stm32g4)
bm_sdk_stm32g4_apply(bm_vendor_stm32g4)

# BM_HAL_CPU_HAS_PORT_FREQ：vendor/stm32g4 自带 bm_hal_cpu_freq_stm32g4.c
# （freq_hz/freq_points/freq_set 真机实现），须令桩 Source/hal/bm_hal_cpu_stub.c
# 让出同名符号。该宏必须到达桩所在编译单元（bm_hal 目标）；bm_hal 不依赖
# vendor，vendor 侧 PUBLIC 定义传不过来（机制见
# docs/03-移植与IDE集成/01-HAL契约与移植要点.md 133-148 行）。
# 此处 bm_hal 已由框架根 CMakeLists 在本 pack add_subdirectory 之前创建，
# 直接对其加 PRIVATE 定义即可（CMake 路径下的可靠途径）。
if(TARGET bm_hal)
    target_compile_definitions(bm_hal PRIVATE BM_HAL_CPU_HAS_PORT_FREQ)
endif()

# 当前 G4 pack 未编入 bm_hal_nvs_load/save 实现，故不得向 bm_hal 或消费者
# 定义 BM_DRV_HAS_NVS_BACKEND；bm_persist 保持 RAM KV，commit 返回
# BM_ERR_NOT_SUPPORTED（非成功 no-op）。

add_library(bm_hal_stm32g4 ALIAS bm_backend_sdk_stm32g4)
