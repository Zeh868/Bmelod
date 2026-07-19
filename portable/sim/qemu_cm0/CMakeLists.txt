add_library(bm_sim_qemu_cm0 STATIC
    bm_sim_singleton_qemu_cm0.c
)
# BM_PORT_ARCH_ROOT（portable/arch，cmake/bm_include_dirs.cmake 定义）：供
# #include "armv6m/bm_arch_isr_fpu.h" 解析（tick ISR FPU 守卫接线）。
target_include_directories(bm_sim_qemu_cm0 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${BM_PORT_ARCH_ROOT})
bm_target_internal_includes(bm_sim_qemu_cm0)
target_link_libraries(bm_sim_qemu_cm0 PUBLIC bm_config)
