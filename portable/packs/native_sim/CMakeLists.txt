# BM_BACKEND=native_sim：arch/host（单核）+ sim/native 源

set(_BM_SIM_NATIVE ${BM_FRAMEWORK_ROOT}/portable/sim/native)

add_library(bm_backend_native_sim STATIC
    ${_BM_SIM_NATIVE}/bm_sim_singleton_native.c
    ${_BM_SIM_NATIVE}/bm_drv_pwm_native.c
    ${_BM_SIM_NATIVE}/bm_drv_adc_native.c
    ${_BM_SIM_NATIVE}/bm_drv_comp_native.c
    ${_BM_SIM_NATIVE}/bm_drv_encoder_native.c
    ${_BM_SIM_NATIVE}/bm_drv_dma_stream_native.c
    ${_BM_SIM_NATIVE}/bm_hal_irq_affinity_native.c
    ${_BM_SIM_NATIVE}/bm_drv_nvs_native.c    # #10 NVS 文件仿真后端
    ${_BM_SIM_NATIVE}/bm_drv_gpio_native.c   # native_sim GPIO + EXTI 后端
    ${_BM_SIM_NATIVE}/bm_drv_hrtimer_native.c # native_sim 高精度 Timer 后端
    ${_BM_SIM_NATIVE}/bm_drv_uart_native.c   # native_sim 多实例 UART 后端
    ${_BM_SIM_NATIVE}/bm_drv_can_native.c    # native_sim 多实例 CAN 后端
)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/host
    _bm_arch_host_native_pack)
target_link_libraries(bm_backend_native_sim PUBLIC bm_port_arch_host)
target_include_directories(bm_backend_native_sim PUBLIC
    ${_BM_SIM_NATIVE})
target_compile_definitions(bm_backend_native_sim PUBLIC
    BM_DRV_HAS_BACKEND
    BM_DRV_HAS_NVS_BACKEND
    BM_HAL_DEVICES_HEADER="bm_hal_devices_native.h")
target_link_libraries(bm_backend_native_sim PUBLIC bm_config)
# bm_core：bf9086b 起 bm_drv_hrtimer_native 调 bm_hrt_isr_enter/exit（在 bm_core）
target_link_libraries(bm_backend_native_sim PUBLIC bm_core)
bm_target_internal_includes(bm_backend_native_sim)

# bm_persist.c 属父目录已创建的 bm_hal，backend 的 PUBLIC 定义不会逆向
# 传播到该编译单元；在实际编入 native NVS 源时显式注入 capability。
if(TARGET bm_hal)
    target_compile_definitions(bm_hal PRIVATE BM_DRV_HAS_NVS_BACKEND)
endif()

add_library(bm_hal_native ALIAS bm_backend_native_sim)
