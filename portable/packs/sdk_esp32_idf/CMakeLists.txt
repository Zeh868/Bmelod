# BM_BACKEND=sdk_esp32_idf：arch/xtensa + vendor/esp32_idf

include(${BM_FRAMEWORK_ROOT}/cmake/bm_sdk_esp32_idf.cmake)

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/xtensa _bm_arch_xtensa_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/boot/esp32_idf _bm_boot_esp32_idf_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/vendor/esp32_idf _bm_vendor_esp32_idf_pack)

add_library(bm_backend_sdk_esp32_idf INTERFACE)
target_compile_definitions(bm_backend_sdk_esp32_idf INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_sdk_esp32_idf INTERFACE
    bm_config
    bm_port_arch_xtensa
    bm_boot_esp32_idf
    bm_vendor_esp32_idf)
bm_sdk_esp32_idf_apply(bm_vendor_esp32_idf)

add_library(bm_hal_esp32wroom32e ALIAS bm_backend_sdk_esp32_idf)
