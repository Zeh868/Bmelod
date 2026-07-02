function(bm_demo_apply_example_config TARGET)
    set(_cfg "${CMAKE_CURRENT_SOURCE_DIR}/bm_config_app.h")
    if(NOT EXISTS "${_cfg}")
        message(FATAL_ERROR "bm_config_app.h not found: ${_cfg}")
    endif()
    get_filename_component(_cfg "${_cfg}" ABSOLUTE)
    target_compile_options(${TARGET} PRIVATE
        "$<$<C_COMPILER_ID:MSVC>:/FI${_cfg}>"
        "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-include${_cfg}>"
    )
    if(DEFINED EXAMPLE_ENABLE_MODULE)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_MODULE=$<BOOL:${EXAMPLE_ENABLE_MODULE}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_SHELL)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_SHELL=$<BOOL:${EXAMPLE_ENABLE_SHELL}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_WDG)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_WDG=$<BOOL:${EXAMPLE_ENABLE_WDG}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_HRT)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_HRT=$<BOOL:${EXAMPLE_ENABLE_HRT}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_TICKER)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_TICKER=$<BOOL:${EXAMPLE_ENABLE_TICKER}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_EXEC)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_EXEC=$<BOOL:${EXAMPLE_ENABLE_EXEC}>)
    endif()
    if(DEFINED EXAMPLE_ENABLE_SYNC)
        target_compile_definitions(${TARGET} PRIVATE
            BM_CONFIG_ENABLE_SYNC=$<BOOL:${EXAMPLE_ENABLE_SYNC}>)
    endif()
endfunction()
