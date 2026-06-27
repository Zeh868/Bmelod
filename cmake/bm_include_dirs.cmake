# Bmelod Baremetal 头文件路径
# 应用只需 include/（对外 API 在根目录）；库编译额外搜索子目录

# 框架根目录：以 cmake/ 的上级为准，子项目 add_subdirectory 时仍指向 bmelod 根
get_filename_component(BM_FRAMEWORK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BM_INCLUDE_ROOT ${BM_FRAMEWORK_ROOT}/include)

set(BM_INCLUDE_PUBLIC_DIRS ${BM_INCLUDE_ROOT})

set(BM_INCLUDE_INTERNAL_DIRS
    ${BM_INCLUDE_ROOT}/bm/common
    ${BM_INCLUDE_ROOT}/bm/core
    ${BM_INCLUDE_ROOT}/bm/hybrid
    ${BM_INCLUDE_ROOT}/bm/algorithm
    ${BM_INCLUDE_ROOT}/hal
    ${BM_INCLUDE_ROOT}/drv
    ${BM_INCLUDE_ROOT}/port
)

# SMP 多核头文件路径（仅 BM_CONFIG_SMP=ON 时追加）
if(BM_CONFIG_SMP)
    list(APPEND BM_INCLUDE_INTERNAL_DIRS ${BM_INCLUDE_ROOT}/bm/mp)
endif()

if(BM_EXTENSION_INCLUDE_ROOT)
    get_filename_component(BM_EXTENSION_INCLUDE_ROOT
        "${BM_EXTENSION_INCLUDE_ROOT}" ABSOLUTE)
    if(EXISTS "${BM_EXTENSION_INCLUDE_ROOT}/include")
        list(PREPEND BM_INCLUDE_PUBLIC_DIRS
            ${BM_EXTENSION_INCLUDE_ROOT}/include)
        list(PREPEND BM_INCLUDE_INTERNAL_DIRS
            ${BM_EXTENSION_INCLUDE_ROOT}/include)
    endif()
endif()

set(BM_PORT_ARCH_ROOT ${BM_FRAMEWORK_ROOT}/portable/arch)

set(BM_INCLUDE_APP_DIRS ${BM_INCLUDE_PUBLIC_DIRS})
set(BM_INCLUDE_BACKEND_DIRS ${BM_INCLUDE_PUBLIC_DIRS})

function(bm_target_internal_includes target)
    target_include_directories(${target} PRIVATE ${BM_INCLUDE_INTERNAL_DIRS})
endfunction()

# QEMU Cortex-M0 引导（启动汇编 / crt0 / 链接脚本）
set(BM_BOOT_QEMU_CM0_DIR ${CMAKE_CURRENT_LIST_DIR}/../portable/boot/qemu_cortex_m0)
# QEMU RISC-V64 virt 引导
set(BM_BOOT_QEMU_RV64_DIR ${CMAKE_CURRENT_LIST_DIR}/../portable/boot/riscv64)
