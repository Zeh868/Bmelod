# ARMv8-M Mainline (Cortex-M33/M55): reuse armv7em critical implementation

add_library(bm_port_arch_armv8m_main STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../armv7em/bm_arch_critical.c
    ${CMAKE_CURRENT_LIST_DIR}/../armv7em/bm_arch_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/../common/bm_arch_drv_bundle.c
)
target_compile_definitions(bm_port_arch_armv8m_main PUBLIC
    BM_PORT_ARCH_ID=\"armv8m_main\"
    BM_HAL_HAS_PRIORITY_MASK=1)
target_include_directories(bm_port_arch_armv8m_main PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${BM_FRAMEWORK_ROOT}/include)
bm_target_internal_includes(bm_port_arch_armv8m_main)
target_link_libraries(bm_port_arch_armv8m_main PUBLIC bm_config)
