# BM_BACKEND=qemu_riscv32_smp：arch/riscv32 + sim/qemu_riscv32_smp

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/riscv32 _bm_arch_riscv32_qemu_smp_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_riscv32_smp _bm_sim_qemu_riscv32_smp_pack)

add_library(bm_backend_qemu_riscv32_smp INTERFACE)
target_compile_definitions(bm_backend_qemu_riscv32_smp INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_riscv32_smp INTERFACE
    bm_config
    bm_port_arch_riscv32
    bm_sim_qemu_riscv32_smp)

add_library(bm_hal_qemu_riscv32_smp ALIAS bm_backend_qemu_riscv32_smp)
