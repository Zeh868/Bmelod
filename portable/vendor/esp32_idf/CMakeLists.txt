# vendor/esp32_idf — 灯哥平衡车主控板 ESP32-WROOM-32E 外设层
#
# ESP-IDF 组件构建：idf_component_register 注册 arch/xtensa + 本目录源文件。
# Bmelod 独立 CMake：由 packs/sdk_esp32_idf 链接 bm_port_arch_xtensa + 本库。

set(_BM_XTENSA_ARCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../arch/xtensa)

if(DEFINED ESP_PLATFORM)
    idf_component_register(
        SRCS
            "bm_vendor_singleton_esp32_idf.c"
            "${_BM_XTENSA_ARCH_DIR}/bm_arch_critical.c"
            "${_BM_XTENSA_ARCH_DIR}/bm_arch_memory.c"
            "${_BM_XTENSA_ARCH_DIR}/../common/bm_arch_drv_bundle.c"
        INCLUDE_DIRS
            "."
            "${_BM_XTENSA_ARCH_DIR}"
            "${_BM_XTENSA_ARCH_DIR}/.."
            "${CMAKE_CURRENT_LIST_DIR}/../../../include"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/bm/common"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/bm/core"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/bm/hybrid"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/drv"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/hal"
            "${CMAKE_CURRENT_LIST_DIR}/../../../include/port"
        REQUIRES driver esp_timer esp_hw_support esp_system freertos
    )
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        BM_DRV_HAS_BACKEND
        BM_PORT_ARCH_ID=\"xtensa\")
else()
    add_library(bm_vendor_esp32_idf STATIC
        bm_vendor_singleton_esp32_idf.c
    )
    target_include_directories(bm_vendor_esp32_idf PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})
    bm_target_internal_includes(bm_vendor_esp32_idf)
    target_link_libraries(bm_vendor_esp32_idf PUBLIC bm_config)
endif()
