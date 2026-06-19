# SEGGER RTT CMake 集成

set(BM_SEGGER_RTT_ROOT "${BM_FRAMEWORK_ROOT}/third_party/segger_rtt")

function(bm_segger_rtt_add_library)
    if(TARGET bm_segger_rtt)
        return()
    endif()
    if(NOT EXISTS "${BM_SEGGER_RTT_ROOT}/RTT/SEGGER_RTT.c")
        message(FATAL_ERROR
            "SEGGER RTT sources missing at ${BM_SEGGER_RTT_ROOT}/RTT")
    endif()
    add_library(bm_segger_rtt STATIC
        ${BM_SEGGER_RTT_ROOT}/RTT/SEGGER_RTT.c
    )
    target_include_directories(bm_segger_rtt PUBLIC
        ${BM_SEGGER_RTT_ROOT}/RTT
        ${BM_SEGGER_RTT_ROOT}/Config
    )
    target_link_libraries(bm_segger_rtt PUBLIC bm_config)
    if(COMMAND bm_target_internal_includes)
        bm_target_internal_includes(bm_segger_rtt)
    endif()
endfunction()

function(bm_segger_rtt_link_to_hal hal_target)
    if(NOT BM_ENABLE_SEGGER_RTT)
        return()
    endif()
    bm_segger_rtt_add_library()
    target_link_libraries(${hal_target} PUBLIC bm_segger_rtt)
    target_compile_definitions(${hal_target} PUBLIC BM_CONSOLE_HAS_SEGGER_RTT)
    message(STATUS "SEGGER RTT: linked into ${hal_target}")
endfunction()
