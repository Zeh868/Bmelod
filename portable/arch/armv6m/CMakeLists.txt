add_library(bm_port_arch_armv6m STATIC
    bm_arch_critical.c
    bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_armv6m PUBLIC
    BM_PORT_ARCH_ID=\"armv6m\")
target_include_directories(bm_port_arch_armv6m PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include)
bm_target_internal_includes(bm_port_arch_armv6m)
target_link_libraries(bm_port_arch_armv6m PUBLIC bm_config)
