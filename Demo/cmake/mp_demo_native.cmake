# MP 闭源 Demo — native_sim_mp 公共构建
include_guard(GLOBAL)

get_filename_component(_MP_DEMO_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(_MP_DEMO_ROOT "${_MP_DEMO_CMAKE_DIR}/.." ABSOLUTE)
get_filename_component(_MP_REPO_ROOT "${_MP_DEMO_ROOT}/.." ABSOLUTE)

function(mp_demo_resolve_roots)
    set(BMEFLOD_MP_ROOT "${_MP_REPO_ROOT}" CACHE PATH "Bmeflod MP extension root" FORCE)
    if(EXISTS "${BMEFLOD_MP_ROOT}/framework/Bmelod/CMakeLists.txt")
        set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/framework/Bmelod")
    elseif(EXISTS "${BMEFLOD_MP_ROOT}/../Bmelod/CMakeLists.txt")
        set(BMELOD_CORE_ROOT "${BMEFLOD_MP_ROOT}/../Bmelod")
    else()
        message(FATAL_ERROR "mp_demo_native: cannot find Bmelod core")
    endif()
    get_filename_component(BMELOD_CORE_ROOT "${BMELOD_CORE_ROOT}" ABSOLUTE)
    set(BMELOD_CORE_ROOT "${BMELOD_CORE_ROOT}" CACHE PATH "" FORCE)
endfunction()

function(mp_configure_native_demo_roots)
    mp_demo_resolve_roots()
    list(APPEND CMAKE_MODULE_PATH "${BMEFLOD_MP_ROOT}/cmake")
    if(NOT COMMAND bmeflod_mp_init)
        include(bmeflod_mp)
    endif()
endfunction()

function(mp_ensure_native_framework)
    if(TARGET bm_mp)
        return()
    endif()
    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_ENABLE_MODULE ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_WDG ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_TICKER ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_EXEC ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)
    set(BM_ENABLE_IPC ON CACHE BOOL "" FORCE)
    set(BM_BACKEND "native_sim_mp" CACHE STRING "" FORCE)
    bmeflod_mp_init(CORE_ROOT "${BMELOD_CORE_ROOT}" MP_ROOT "${BMEFLOD_MP_ROOT}")

    # NATIVE_SIM 必须覆盖 bm_mp 库本身：bm_mp_enforce_main_loop_period 据此在
    # native 上退化为 no-op。此前仅加在 demo 可执行目标（见 mp_add_native_demo），
    # bm_mp.c 编进独立库却拿不到该宏，导致 native demo 主循环误判周期超限、
    # 持续刷 "main loop period wait exceeded"。与 QEMU 侧给 bm_mp 加
    # BM_EXAMPLE_QEMU_SMP 的做法对称（bm_mp.c 是唯一引用裸 NATIVE_SIM 的 TU）。
    if(TARGET bm_mp)
        target_compile_definitions(bm_mp PRIVATE NATIVE_SIM)
    endif()
endfunction()

function(mp_add_native_demo TARGET)
    set(options LINK_ALGO)
    set(one_value_args CONFIG_FILE)
    cmake_parse_arguments(MP "${options}" "${one_value_args}" "" ${ARGN})

    mp_configure_native_demo_roots()

    if(MP_LINK_ALGO)
        set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
    endif()
    mp_ensure_native_framework()

    if(MP_CONFIG_FILE)
        set(_cfg "${MP_CONFIG_FILE}")
    else()
        set(_cfg "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h")
    endif()
    if(NOT EXISTS "${_cfg}")
        message(FATAL_ERROR "mp_add_native_demo: bm_config missing: ${_cfg}")
    endif()
    get_filename_component(_cfg "${_cfg}" ABSOLUTE)

    if(NOT TARGET bm_core)
        set(BM_CONFIG_FILE "${_cfg}" CACHE FILEPATH "" FORCE)
    endif()

    if(MP_LINK_ALGO AND NOT TARGET bmp_algo)
        bmeflod_mp_create_algo_library()
    endif()

    set(_demo_sources
        ${MP_UNPARSED_ARGUMENTS}
        "${BMELOD_CORE_ROOT}/Demo/common/example_support.c"
        "${BMELOD_CORE_ROOT}/Demo/common/hybrid_print.c"
    )

    add_executable(${TARGET} ${_demo_sources})
    target_compile_definitions(${TARGET} PRIVATE NATIVE_SIM)
    target_compile_options(${TARGET} PRIVATE
        "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
        "$<$<C_COMPILER_ID:MSVC>:/FI${_cfg}>"
        "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-include${_cfg}>"
    )
    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_MP_DEMO_ROOT}/common"
        "${BMELOD_CORE_ROOT}/Demo/common"
        "${BMELOD_CORE_ROOT}/include"
        "${BMELOD_CORE_ROOT}/include/bm/common"
        "${BMELOD_CORE_ROOT}/include/bm/core"
        "${BMELOD_CORE_ROOT}/include/bm/hybrid"
        "${BMEFLOD_MP_ROOT}/include"
        "${BMEFLOD_MP_ROOT}/include/bm/mp"
        "${BMELOD_CORE_ROOT}/include/hal"
        "${BMELOD_CORE_ROOT}/portable/sim/native"
        "${BMEFLOD_MP_ROOT}/portable/sim/native_mp"
    )

    set(_fw_libs
        bm_config bm_core bm_module bm_hrt bm_ticker bm_exec
        bm_ipc bm_mp bm_wdg bm_hal bm_hal_native
    )
    if(MP_LINK_ALGO)
        list(APPEND _fw_libs bmp_algo)
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${TARGET} PRIVATE
            "-Wl,--start-group" ${_fw_libs} "-Wl,--end-group")
    else()
        target_link_libraries(${TARGET} PRIVATE ${_fw_libs})
    endif()
    if(NOT MSVC)
        target_link_libraries(${TARGET} PRIVATE m)
    endif()
endfunction()
