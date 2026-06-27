# BM_BACKEND=qemu_cortexa_smp：arch/armv7a + sim/qemu_cortexa_smp

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/armv7a _bm_arch_armv7a_qemu_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_cortexa_smp _bm_sim_qemu_cortexa_smp_pack)

add_library(bm_backend_qemu_cortexa_smp INTERFACE)
target_compile_definitions(bm_backend_qemu_cortexa_smp INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_cortexa_smp INTERFACE
    bm_config
    bm_port_arch_armv7a
    bm_sim_qemu_cortexa_smp)

add_library(bm_hal_qemu_cortexa_smp ALIAS bm_backend_qemu_cortexa_smp)
