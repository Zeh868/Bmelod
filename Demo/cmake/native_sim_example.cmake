set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "Path to the bmelod-baremetal framework")

include(${CMAKE_CURRENT_LIST_DIR}/demo_example_common.cmake)

function(bm_demo_apply_native_sim_example_flags)
    if(BM_DEMO_UNIFIED_BUILD)
        set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BM_BUILD_ALL_COMPONENTS ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_MODULE ON CACHE BOOL "" FORCE)
        # 统一构建所有 Demo 共享一份框架配置：bus_servo 链接 bm_shell，
        # 置 ON 让框架多编一个 bm_shell 库，不需要的 Demo 不链即可
        #（与 qemu_example.cmake 统一分支行为一致）
        set(BM_ENABLE_SHELL ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_WDG ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_TT_SCHEDULE ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_TICKER ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_EXEC ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_SYNC ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_STREAM ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_PIPELINE ON CACHE BOOL "" FORCE)
        set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
        set(BM_SYNC_HAL_NATIVE ON CACHE BOOL "" FORCE)
        set(BM_SYNC_HAL_QEMU OFF CACHE BOOL "" FORCE)
        set(BM_BACKEND "native_sim" CACHE STRING "" FORCE)
        return()
    endif()

    set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BM_ENABLE_MODULE ${EXAMPLE_ENABLE_MODULE} CACHE BOOL "" FORCE)
    # 与 qemu_example.cmake 同构：尊重示例声明的 EXAMPLE_ENABLE_SHELL
    # （bus_servo 链接 bm_shell；未声明的示例该变量为空 → OFF）
    set(BM_ENABLE_SHELL ${EXAMPLE_ENABLE_SHELL} CACHE BOOL "" FORCE)
    set(BM_ENABLE_WDG ${EXAMPLE_ENABLE_WDG} CACHE BOOL "" FORCE)
    set(BM_ENABLE_HRT ${EXAMPLE_ENABLE_HRT} CACHE BOOL "" FORCE)
    set(BM_ENABLE_TICKER ${EXAMPLE_ENABLE_TICKER} CACHE BOOL "" FORCE)
    set(BM_ENABLE_EXEC ${EXAMPLE_ENABLE_EXEC} CACHE BOOL "" FORCE)
    set(BM_ENABLE_SYNC ${EXAMPLE_ENABLE_SYNC} CACHE BOOL "" FORCE)
    if(EXAMPLE_ENABLE_SYNC)
        set(BM_SYNC_HAL_NATIVE ON CACHE BOOL "" FORCE)
    else()
        set(BM_SYNC_HAL_NATIVE OFF CACHE BOOL "" FORCE)
    endif()
    set(BM_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h" CACHE FILEPATH "" FORCE)
    set(BM_BACKEND "native_sim" CACHE STRING "" FORCE)
endfunction()

function(bm_demo_ensure_native_sim)
    # 去重守卫只在单次 configure 内生效：GLOBAL 属性随 configure 结束即弃。
    # 此前用 INTERNAL 缓存（BM_DEMO_NATIVE_INIT）会跨 configure 残留，
    # 导致 standalone Demo 目录在框架 CMakeLists 变更后的增量 reconfigure
    # 静默跳过 add_subdirectory，build.ninja 退化为裸 -lbm_* 链接。
    get_property(_bm_demo_native_init GLOBAL PROPERTY BM_DEMO_NATIVE_INIT_DONE)
    if(_bm_demo_native_init)
        return()
    endif()
    set_property(GLOBAL PROPERTY BM_DEMO_NATIVE_INIT_DONE ON)
    bm_demo_apply_native_sim_example_flags()
    if(BM_DEMO_UNIFIED_BUILD)
        set(BM_CONFIG_FILE "" CACHE FILEPATH "" FORCE)
    endif()
    add_subdirectory("${BMELOD_ROOT}" bmelod EXCLUDE_FROM_ALL)
endfunction()

function(bm_add_native_sim_example TARGET)
    set(options)
    set(one_value_args)
    set(multi_value_args FRAMEWORK_LIBS EXTRA_SOURCES)
    cmake_parse_arguments(EX "${options}" "${one_value_args}"
        "${multi_value_args}" ${ARGN})

    add_executable(${TARGET}
        main.c
        "${BMELOD_ROOT}/Demo/common/example_support.c"
        "${BMELOD_ROOT}/Demo/common/hybrid_print.c"
        ${EX_EXTRA_SOURCES}
    )
    target_compile_definitions(${TARGET} PRIVATE
        NATIVE_SIM
        BM_DRV_HAS_BACKEND)
    target_compile_options(${TARGET} PRIVATE
        "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
    )

    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${BMELOD_ROOT}/Demo/common"
        "${BMELOD_ROOT}/include"
        "${BMELOD_ROOT}/include/bm/common"
        "${BMELOD_ROOT}/include/bm/core"
        "${BMELOD_ROOT}/include/bm/hybrid"
        "${BMELOD_ROOT}/include/hal"
        "${BMELOD_ROOT}/include/drv"
        "${BMELOD_ROOT}/portable/sim/native"
    )
    target_link_libraries(${TARGET} PRIVATE
        bm_config
        ${EX_FRAMEWORK_LIBS}
        bm_hal
        bm_hal_native
    )
    if(TARGET bm_port_arch_host)
        target_link_libraries(${TARGET} PRIVATE
            -Wl,--whole-archive bm_port_arch_host -Wl,--no-whole-archive)
    endif()
    if(BM_DEMO_UNIFIED_BUILD)
        bm_demo_apply_example_config(${TARGET})
    endif()
endfunction()

if(NOT BM_DEMO_UNIFIED_BUILD)
    bm_demo_ensure_native_sim()
endif()
