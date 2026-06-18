# BM_BACKEND=arch_stub — 仅 arch 层 + HAL 烟雾（无 timer/uart 后端）

add_subdirectory(${BM_FRAMEWORK_ROOT}/portable/arch/stub _bm_arch_stub_pack)

add_library(bm_backend_arch_stub INTERFACE)
target_compile_definitions(bm_backend_arch_stub INTERFACE BM_DRV_HAS_BACKEND)
target_link_libraries(bm_backend_arch_stub INTERFACE
    bm_config
    bm_port_arch_stub)

add_library(bm_hal_arch_stub ALIAS bm_backend_arch_stub)
