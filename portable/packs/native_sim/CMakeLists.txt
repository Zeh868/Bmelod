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
)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/host
    _bm_arch_host_native_pack)
target_link_libraries(bm_backend_native_sim PUBLIC bm_port_arch_host)
target_include_directories(bm_backend_native_sim PUBLIC
    ${_BM_SIM_NATIVE})
target_compile_definitions(bm_backend_native_sim PUBLIC BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_native_sim PUBLIC bm_config)
bm_target_internal_includes(bm_backend_native_sim)

add_library(bm_hal_native ALIAS bm_backend_native_sim)
