# ARMv8-M Baseline?Cortex-M23??? basepri??? armv6m

if(NOT TARGET bm_port_arch_armv6m)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../armv6m
        _bm_port_arch_armv6m_for_v8m_base)
endif()
add_library(bm_port_arch_armv8m_base INTERFACE)
target_link_libraries(bm_port_arch_armv8m_base INTERFACE bm_port_arch_armv6m)
target_compile_definitions(bm_port_arch_armv8m_base INTERFACE
    BM_PORT_ARCH_ID=\"armv8m_base\")
