# BM_BACKEND=qemu_esp32_smp：arch/xtensa + sim/qemu_esp32_smp

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/xtensa _bm_arch_xtensa_qemu_esp32_smp_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_esp32_smp _bm_sim_qemu_esp32_smp_pack)

add_library(bm_backend_qemu_esp32_smp INTERFACE)
target_compile_definitions(bm_backend_qemu_esp32_smp INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_esp32_smp INTERFACE
    bm_config
    bm_port_arch_xtensa
    bm_sim_qemu_esp32_smp)

add_library(bm_hal_qemu_esp32_smp ALIAS bm_backend_qemu_esp32_smp)
