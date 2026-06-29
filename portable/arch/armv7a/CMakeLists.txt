add_library(bm_port_arch_armv7a STATIC
    bm_arch_critical.c
    bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_armv7a PUBLIC
    BM_PORT_ARCH_ID=\"armv7a\")
target_include_directories(bm_port_arch_armv7a PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include
    ${BM_FRAMEWORK_ROOT}/include/drv
    ${BM_FRAMEWORK_ROOT}/include/hal)
bm_target_internal_includes(bm_port_arch_armv7a)
target_link_libraries(bm_port_arch_armv7a PUBLIC bm_config)
