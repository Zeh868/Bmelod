# BM_BACKEND=qemu_aarch64_smp：arch/aarch64 + sim/qemu_aarch64_smp

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/aarch64 _bm_arch_aarch64_qemu_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_aarch64_smp _bm_sim_qemu_aarch64_smp_pack)

add_library(bm_backend_qemu_aarch64_smp INTERFACE)
target_compile_definitions(bm_backend_qemu_aarch64_smp INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_aarch64_smp INTERFACE
    bm_config
    bm_port_arch_aarch64
    bm_sim_qemu_aarch64_smp)

add_library(bm_hal_qemu_aarch64_smp ALIAS bm_backend_qemu_aarch64_smp)
