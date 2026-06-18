# ARMv7-M（Cortex-M3）：复用 armv6m 实现，独立 Arch ID

if(NOT TARGET bm_port_arch_armv6m)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../armv6m
        _bm_port_arch_armv6m_for_v7m)
endif()
add_library(bm_port_arch_armv7m INTERFACE)
target_link_libraries(bm_port_arch_armv7m INTERFACE bm_port_arch_armv6m)
target_compile_definitions(bm_port_arch_armv7m INTERFACE
    BM_PORT_ARCH_ID=\"armv7m\")
