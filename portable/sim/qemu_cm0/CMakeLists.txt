add_library(bm_sim_qemu_cm0 STATIC
    bm_sim_singleton_qemu_cm0.c
)
target_include_directories(bm_sim_qemu_cm0 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
bm_target_internal_includes(bm_sim_qemu_cm0)
target_link_libraries(bm_sim_qemu_cm0 PUBLIC bm_config)
