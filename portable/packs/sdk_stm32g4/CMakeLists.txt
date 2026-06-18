# BM_BACKEND=sdk_stm32g4：arch/armv7em + vendor/stm32g4

include(${BM_FRAMEWORK_ROOT}/cmake/bm_sdk_stm32g4.cmake)

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/armv7em _bm_arch_armv7em_pack)
add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/vendor/stm32g4 _bm_vendor_stm32g4_pack)

add_library(bm_backend_sdk_stm32g4 INTERFACE)
target_compile_definitions(bm_backend_sdk_stm32g4 INTERFACE
    BM_DRV_HAS_BACKEND
    BM_HAL_HAS_PRIORITY_MASK=1)
target_link_libraries(bm_backend_sdk_stm32g4 INTERFACE
    bm_config
    bm_port_arch_armv7em
    bm_vendor_stm32g4)
bm_sdk_stm32g4_apply(bm_vendor_stm32g4)

add_library(bm_hal_stm32g4 ALIAS bm_backend_sdk_stm32g4)
