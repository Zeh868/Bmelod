add_library(bm_port_arch_riscv64 STATIC
    bm_arch_critical.c
    bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_riscv64 PUBLIC
    BM_PORT_ARCH_ID=\"riscv64\")
target_include_directories(bm_port_arch_riscv64 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include)
bm_target_internal_includes(bm_port_arch_riscv64)
target_link_libraries(bm_port_arch_riscv64 PUBLIC bm_config)
