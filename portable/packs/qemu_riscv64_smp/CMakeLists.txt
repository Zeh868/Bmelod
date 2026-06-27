# BM_BACKEND=qemu_riscv64_smp：arch/riscv64 + sim/qemu_riscv64_smp

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/riscv64 _bm_arch_riscv64_qemu_smp_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/sim/qemu_riscv64_smp _bm_sim_qemu_riscv64_smp_pack)

if(TARGET bm_port_arch_riscv64)
    target_include_directories(bm_port_arch_riscv64 PUBLIC
        ${BM_FRAMEWORK_ROOT}/include/drv)
endif()

add_library(bm_backend_qemu_riscv64_smp INTERFACE)
target_compile_definitions(bm_backend_qemu_riscv64_smp INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_qemu_riscv64_smp INTERFACE
    bm_config
    bm_port_arch_riscv64
    bm_sim_qemu_riscv64_smp)

add_library(bm_hal_qemu_riscv64_smp ALIAS bm_backend_qemu_riscv64_smp)
