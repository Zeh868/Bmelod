# 将 Bmelod 编译为静态库（库本体，不含 Port）
# 用法见 docs/03-构建与工具链/02-预编译静态库.md 与 cmake/static-lib/CMakeLists.txt

include(${CMAKE_CURRENT_LIST_DIR}/bmelod_profiles.cmake)

function(bmelod_build_libraries)
    set(options)
    set(one_value_args ROOT PROFILE CONFIG)
    set(multi_value_args)
    cmake_parse_arguments(ZL "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ZL_ROOT)
        set(ZL_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
    endif()
    get_filename_component(ZL_ROOT "${ZL_ROOT}" ABSOLUTE)

    if(NOT ZL_PROFILE)
        set(ZL_PROFILE "event")
    endif()
    _bmelod_apply_profile(${ZL_PROFILE})

    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_BACKEND "external" CACHE STRING "" FORCE)

    if(ZL_CONFIG)
        get_filename_component(ZL_CONFIG "${ZL_CONFIG}" ABSOLUTE)
        set(BM_CONFIG_FILE "${ZL_CONFIG}" CACHE FILEPATH "" FORCE)
    endif()

    if(NOT TARGET bm_core)
        add_subdirectory("${ZL_ROOT}" _bmelod_lib_build EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET bmelod::libraries)
        add_library(bmelod_libraries INTERFACE)
        target_link_libraries(bmelod_libraries INTERFACE bm_framework bm_hal)
        add_library(bmelod::libraries ALIAS bmelod_libraries)
    endif()
endfunction()

# 返回当前 PROFILE 下已启用的静态库目标名列表（供安装脚本使用）
function(bmelod_library_targets OUT_VAR)
    set(_targets bm_hal bm_core)
    if(BM_ENABLE_MODULE)
        list(APPEND _targets bm_module)
    endif()
    if(BM_ENABLE_SHELL)
        list(APPEND _targets bm_shell)
    endif()
    if(BM_ENABLE_WDG)
        list(APPEND _targets bm_wdg)
    endif()
    if(BM_ENABLE_HRT)
        list(APPEND _targets bm_hrt)
    endif()
    if(BM_ENABLE_TICKER)
        list(APPEND _targets bm_ticker)
    endif()
    if(BM_ENABLE_EXEC)
        list(APPEND _targets bm_resource bm_exec)
    endif()
    set(${OUT_VAR} ${_targets} PARENT_SCOPE)
endfunction()
