add_library(bm_port_arch_stub STATIC
    bm_arch_critical.c
    bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_stub PUBLIC
    BM_PORT_ARCH_ID=\"stub\")
target_include_directories(bm_port_arch_stub PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include)
bm_target_internal_includes(bm_port_arch_stub)
target_link_libraries(bm_port_arch_stub PUBLIC bm_config)
