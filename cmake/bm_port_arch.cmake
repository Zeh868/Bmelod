# 架构 Port 层 CMake 辅助
# 用法：bm_port_add_arch(armv7em OUT_VAR) → 创建/链接 bm_port_arch_<id>

set(BM_PORT_ARCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../portable/arch)

function(bm_port_add_arch arch_id out_target)
    set(_tgt bm_port_arch_${arch_id})
    if(TARGET ${_tgt})
        set(${out_target} ${_tgt} PARENT_SCOPE)
        return()
    endif()
    add_subdirectory(${BM_PORT_ARCH_ROOT}/${arch_id}
        _bm_port_arch_${arch_id})
    set(${out_target} ${_tgt} PARENT_SCOPE)
endfunction()

function(bm_port_arch_common_sources out_var)
    set(${out_var}
        ${BM_PORT_ARCH_ROOT}/common/bm_arch_drv_bundle.c
        PARENT_SCOPE)
endfunction()

function(bm_port_arch_apply_includes target)
    target_include_directories(${target} PUBLIC
        ${BM_PORT_ARCH_ROOT}
        ${BM_FRAMEWORK_ROOT}/include)
    bm_target_internal_includes(${target})
endfunction()
