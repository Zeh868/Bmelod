add_library(bm_sim_qemu_riscv64 STATIC
    bm_sim_singleton_qemu_riscv64.c
)
target_include_directories(bm_sim_qemu_riscv64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
bm_target_internal_includes(bm_sim_qemu_riscv64)
target_link_libraries(bm_sim_qemu_riscv64 PUBLIC bm_config)
