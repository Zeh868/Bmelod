add_library(bm_sim_qemu_riscv64_smp STATIC
    bm_sim_singleton_qemu_rv64_smp.c
    bm_hal_cpu_qemu_rv64_smp.c
)
# BM_PORT_ARCH_ROOT（portable/arch，cmake/bm_include_dirs.cmake 定义）：供
# #include "riscv64/bm_arch_isr_fpu.h" 解析（tick ISR FPU 守卫接线）。
target_include_directories(bm_sim_qemu_riscv64_smp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BM_PORT_ARCH_ROOT}
    ${BMELOD_CORE_ROOT}/include
    ${BMELOD_CORE_ROOT}/include/bm/common
    ${BMELOD_CORE_ROOT}/include/bm/core
    ${BMELOD_CORE_ROOT}/include/bm/hybrid
    ${BMELOD_CORE_ROOT}/include/hal
    ${BMELOD_CORE_ROOT}/include/drv)
bm_target_internal_includes(bm_sim_qemu_riscv64_smp)
target_link_libraries(bm_sim_qemu_riscv64_smp PUBLIC bm_config)
