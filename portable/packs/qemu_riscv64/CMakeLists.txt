# BM_BACKEND=qemu_riscv64：arch/riscv64 + sim/qemu_riscv64

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/riscv64 _bm_arch_riscv64_qemu_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_riscv64 _bm_sim_qemu_riscv64_pack)

add_library(bm_backend_qemu_riscv64 INTERFACE)
target_compile_definitions(bm_backend_qemu_riscv64 INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_riscv64 INTERFACE
    bm_config
    bm_port_arch_riscv64
    bm_sim_qemu_riscv64)

add_library(bm_hal_qemu_riscv64 ALIAS bm_backend_qemu_riscv64)
