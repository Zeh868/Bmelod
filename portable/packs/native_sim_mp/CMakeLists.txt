# BM_BACKEND=native_sim_mp：arch/host + native_mp 多核仿真层
# 所有路径均以合并后的 BM_FRAMEWORK_ROOT 为根

set(_BM_SIM_NATIVE ${BM_FRAMEWORK_ROOT}/portable/sim/native)
set(_BM_SIM_MP     ${BM_FRAMEWORK_ROOT}/portable/sim/native_mp)

add_library(bm_backend_native_sim_mp STATIC
    ${_BM_SIM_NATIVE}/bm_sim_singleton_native.c
    ${_BM_SIM_NATIVE}/bm_drv_pwm_native.c
    ${_BM_SIM_NATIVE}/bm_drv_adc_native.c
    ${_BM_SIM_NATIVE}/bm_drv_comp_native.c
    ${_BM_SIM_NATIVE}/bm_drv_encoder_native.c
    ${_BM_SIM_NATIVE}/bm_drv_dma_stream_native.c
    ${_BM_SIM_NATIVE}/bm_hal_irq_affinity_native.c
    ${_BM_SIM_NATIVE}/bm_drv_nvs_native.c    # #10 NVS 文件仿真后端（与 native_sim 保持一致）
    ${_BM_SIM_MP}/bm_sim_critical_mp_native.c
    ${_BM_SIM_MP}/bm_hal_cpu_mp_native.c
)
target_compile_definitions(bm_backend_native_sim_mp PUBLIC
    BM_NATIVE_SIM_CPU_LOCAL_CRITICAL=1
    BM_NATIVE_SIM_TIMER_CPU_LOCAL=1
    BM_DRV_HAS_BACKEND)
target_compile_options(bm_backend_native_sim_mp PUBLIC
    "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
target_include_directories(bm_backend_native_sim_mp PUBLIC
    ${_BM_SIM_NATIVE}
    ${_BM_SIM_MP}
    ${BM_FRAMEWORK_ROOT}/include
    ${BM_FRAMEWORK_ROOT}/include/hal
    ${BM_FRAMEWORK_ROOT}/include/drv
    ${BM_FRAMEWORK_ROOT}/include/bm/common
    ${BM_FRAMEWORK_ROOT}/include/bm/core
    ${BM_FRAMEWORK_ROOT}/include/port)
target_link_libraries(bm_backend_native_sim_mp PUBLIC bm_config)
target_compile_definitions(bm_config INTERFACE BM_HAL_CRITICAL_MASKS_STREAM_IRQ=1)
if(TARGET bm_port_arch_host)
    target_link_libraries(bm_backend_native_sim_mp PUBLIC bm_port_arch_host)
else()
    add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/host
        _bm_arch_host_native_mp_pack)
    if(TARGET bm_port_arch_host)
        target_include_directories(bm_port_arch_host PRIVATE
            ${BM_FRAMEWORK_ROOT}/include/drv
            ${BM_FRAMEWORK_ROOT}/include/bm/common
            ${BM_FRAMEWORK_ROOT}/include/bm/core
            ${BM_FRAMEWORK_ROOT}/include/port)
    endif()
    target_link_libraries(bm_backend_native_sim_mp PUBLIC bm_port_arch_host)
endif()
if(COMMAND bm_target_internal_includes)
    bm_target_internal_includes(bm_backend_native_sim_mp)
endif()

add_library(bm_hal_native ALIAS bm_backend_native_sim_mp)
