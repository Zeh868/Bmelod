# BM_BACKEND=qemu_cortex_m0：arch/armv6m + sim/qemu_cm0 + sim/native 外设源

set(_BM_SIM_NATIVE ${BM_FRAMEWORK_ROOT}/portable/sim/native)
set(_BM_QEMU_CM0 ${BM_FRAMEWORK_ROOT}/portable/sim/qemu_cm0)

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/armv6m _bm_arch_armv6m_qemu_pack)
add_subdirectory(${_BM_QEMU_CM0} _bm_sim_qemu_cm0_pack)

add_library(bm_backend_qemu_cortex_m0 STATIC
    ${_BM_SIM_NATIVE}/bm_drv_pwm_native.c
    ${_BM_SIM_NATIVE}/bm_drv_adc_native.c
    ${_BM_SIM_NATIVE}/bm_drv_comp_native.c
    ${_BM_SIM_NATIVE}/bm_drv_encoder_native.c
    ${_BM_SIM_NATIVE}/bm_drv_dma_stream_native.c
    ${_BM_QEMU_CM0}/bm_sync_hal_qemu.c
)
target_include_directories(bm_backend_qemu_cortex_m0 PUBLIC
    ${_BM_SIM_NATIVE}
    ${_BM_QEMU_CM0})
target_compile_definitions(bm_backend_qemu_cortex_m0 PUBLIC BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_cortex_m0 PUBLIC
    bm_config
    bm_port_arch_armv6m
    bm_sim_qemu_cm0)
bm_target_internal_includes(bm_backend_qemu_cortex_m0)
