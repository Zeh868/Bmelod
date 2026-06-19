# vendor/esp32_idf — 灯哥 V4 平衡车 ESP32-WROOM-32E 裸机外设层
#
# 该目录只保留独立 CMake 裸机静态库路径：不再作为 ESP-IDF App 组件注册，
# 也不引入 FreeRTOS、任务、队列或事件循环。上层工程通过
# `portable/packs/sdk_esp32_idf` 组合 `arch/xtensa` + 本目录即可。

set(_BM_XTENSA_ARCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../arch/xtensa)

add_library(bm_vendor_esp32_idf STATIC
    bm_vendor_singleton_esp32_idf.c
    bm_vendor_pwm_esp32_idf.c
    bm_vendor_adc_esp32_idf.c
    bm_vendor_encoder_esp32_idf.c
    bm_vendor_bmi160_esp32_idf.c
)

target_include_directories(bm_vendor_esp32_idf PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
bm_target_internal_includes(bm_vendor_esp32_idf)
target_link_libraries(bm_vendor_esp32_idf PUBLIC bm_config)
target_compile_definitions(bm_vendor_esp32_idf PUBLIC
    BM_DRV_HAS_BACKEND
    BM_ESP32_BAREMETAL=1
    BM_PORT_ARCH_ID=\"xtensa\")
