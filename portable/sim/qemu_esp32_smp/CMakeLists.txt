add_library(bm_sim_qemu_esp32_smp STATIC
    bm_sim_singleton_qemu_esp32_smp.c
    bm_hal_cpu_qemu_esp32_smp.c
)
target_include_directories(bm_sim_qemu_esp32_smp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
bm_target_internal_includes(bm_sim_qemu_esp32_smp)
target_link_libraries(bm_sim_qemu_esp32_smp PUBLIC bm_config)
