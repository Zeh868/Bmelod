add_library(bm_sim_qemu_cortexa_smp STATIC
    bm_sim_singleton_qemu_cortexa_smp.c
    bm_hal_cpu_qemu_cortexa_smp.c
)
target_include_directories(bm_sim_qemu_cortexa_smp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BMELOD_CORE_ROOT}/include
    ${BMELOD_CORE_ROOT}/include/bm/common
    ${BMELOD_CORE_ROOT}/include/bm/core
    ${BMELOD_CORE_ROOT}/include/bm/hybrid
    ${BMELOD_CORE_ROOT}/include/hal
    ${BMELOD_CORE_ROOT}/include/drv)
bm_target_internal_includes(bm_sim_qemu_cortexa_smp)
target_link_libraries(bm_sim_qemu_cortexa_smp PUBLIC bm_config)
