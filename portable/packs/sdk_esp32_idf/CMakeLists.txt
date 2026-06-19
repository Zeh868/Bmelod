# BM_BACKEND=sdk_esp32_idf：arch/xtensa + vendor/esp32_idf
#
# Phase 3 说明：portable/boot/esp32_idf/ 已删除，IDF 路线通过 idf.py 构建系统
# 提供启动/链接，不需要仓库内裸机 boot 骨架。

include(${BM_FRAMEWORK_ROOT}/cmake/bm_sdk_esp32_idf.cmake)

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/xtensa _bm_arch_xtensa_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/vendor/esp32_idf _bm_vendor_esp32_idf_pack)

add_library(bm_backend_sdk_esp32_idf INTERFACE)
target_compile_definitions(bm_backend_sdk_esp32_idf INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_sdk_esp32_idf INTERFACE
    bm_config
    bm_port_arch_xtensa
    bm_vendor_esp32_idf)
bm_sdk_esp32_idf_apply(bm_vendor_esp32_idf)

add_library(bm_hal_esp32wroom32e ALIAS bm_backend_sdk_esp32_idf)
