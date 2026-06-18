add_library(bm_port_arch_host STATIC
    bm_arch_critical.c
    bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_host PUBLIC
    BM_PORT_ARCH_ID=\"host\")
target_include_directories(bm_port_arch_host PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include)
bm_target_internal_includes(bm_port_arch_host)
target_link_libraries(bm_port_arch_host PUBLIC bm_config)
