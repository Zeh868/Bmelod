# Bmelod Baremetal — CMake 便捷入口（库源码集成，方式 A′）
#
# 集成模型（见 docs/04-移植与IDE集成/02-挂库到现有工程.md）：
#   ① 移植：portable/template/bm_port.c 复制到应用工程
#   ② 库：bmelod_configure + bmelod_link 拉入 Source/ 源码目标
#
#   bmelod_configure(ROOT ... PROFILE event BACKEND external CONFIG bm_config.h)
#   target_sources(app PRIVATE Core/Src/bm_port.c)
#   bmelod_link(app)
#
# 静态库集成（方式 B）见 cmake/static-lib/

include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/bmelod_profiles.cmake)

set(_BMELOD_BACKEND_ALIAS_native_sim bm_hal_native)
set(_BMELOD_BACKEND_ALIAS_sdk_stm32g4 bm_hal_stm32g4)
set(_BMELOD_BACKEND_ALIAS_sdk_esp32_idf bm_hal_esp32wroom32e)
set(_BMELOD_BACKEND_ALIAS_register_ch32v003 bm_hal_ch32v003)
set(_BMELOD_BACKEND_ALIAS_qemu_cortex_m0 bm_backend_qemu_cortex_m0)
set(_BMELOD_BACKEND_ALIAS_qemu_riscv64 bm_hal_qemu_riscv64)

function(bmelod_configure)
    set(options)
    set(one_value_args ROOT PROFILE BACKEND CONFIG)
    set(multi_value_args)
    cmake_parse_arguments(ZP "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ZP_ROOT)
        set(ZP_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
    endif()
    get_filename_component(ZP_ROOT "${ZP_ROOT}" ABSOLUTE)
    set(BMELOD_ROOT "${ZP_ROOT}" CACHE PATH "Bmelod Baremetal root" FORCE)

    if(NOT ZP_PROFILE)
        set(ZP_PROFILE "event")
    endif()
    _bmelod_apply_profile(${ZP_PROFILE})

    if(NOT ZP_BACKEND)
        set(ZP_BACKEND "external")
    endif()

    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_BACKEND "${ZP_BACKEND}" CACHE STRING "" FORCE)

    if(ZP_CONFIG)
        get_filename_component(ZP_CONFIG "${ZP_CONFIG}" ABSOLUTE)
        set(BM_CONFIG_FILE "${ZP_CONFIG}" CACHE FILEPATH "" FORCE)
    endif()

    if(NOT TARGET bm_core)
        add_subdirectory("${ZP_ROOT}" _bmelod_baremetal EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET bmelod::framework)
        add_library(bmelod_framework INTERFACE)
        target_link_libraries(bmelod_framework INTERFACE bm_framework)

        if(ZP_BACKEND STREQUAL "external")
            target_compile_definitions(bm_hal PRIVATE BM_DRV_HAS_BACKEND)
        else()
            set(_alias_var _BMELOD_BACKEND_ALIAS_${ZP_BACKEND})
            if(DEFINED ${_alias_var})
                set(_backend_target ${${_alias_var}})
                if(TARGET ${_backend_target})
                    target_link_libraries(bmelod_framework INTERFACE ${_backend_target})
                else()
                    message(FATAL_ERROR "Backend target not found: ${_backend_target}")
                endif()
            else()
                message(FATAL_ERROR "Unknown BACKEND '${ZP_BACKEND}'. "
                    "Use external + bm_port.c, or a name from portable/")
            endif()
        endif()

        add_library(bmelod::framework ALIAS bmelod_framework)
    endif()

    set(BMELOD_CONFIGURED ON CACHE INTERNAL "bmelod_configure completed")
endfunction()

function(bmelod_link TARGET)
    if(NOT BMELOD_CONFIGURED)
        message(FATAL_ERROR "Call bmelod_configure() before bmelod_link()")
    endif()
    if(NOT TARGET bmelod::framework)
        message(FATAL_ERROR "bmelod::framework target missing")
    endif()
    target_link_libraries(${TARGET} PRIVATE bmelod::framework)
endfunction()

function(bmelod_include_dirs OUT_VAR)
    if(NOT BMELOD_ROOT)
        set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
    endif()
    include(${BMELOD_ROOT}/cmake/bm_include_dirs.cmake)
    set(${OUT_VAR} ${BM_INCLUDE_APP_DIRS} PARENT_SCOPE)
endfunction()

function(bmelod_port_include_dirs OUT_VAR)
    if(NOT BMELOD_ROOT)
        set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
    endif()
    include(${BMELOD_ROOT}/cmake/bm_include_dirs.cmake)
    set(${OUT_VAR} ${BM_INCLUDE_PUBLIC_DIRS} ${BM_INCLUDE_INTERNAL_DIRS} PARENT_SCOPE)
endfunction()
