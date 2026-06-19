# Console 后端源文件挂接到 HAL 目标

function(bm_console_add_backends hal_target)
    if(NOT BM_FRAMEWORK_ROOT)
        get_filename_component(BM_FRAMEWORK_ROOT
            "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    endif()
    set(_console_root ${BM_FRAMEWORK_ROOT}/portable/console)
    target_sources(${hal_target} PRIVATE
        ${_console_root}/bm_console_stub.c
        ${_console_root}/bm_console_stdio.c
        ${_console_root}/bm_console_uart.c
        ${_console_root}/bm_console_rtt.c
    )
endfunction()

# 按 BM_BACKEND 注入默认 Console 通道后端（可被 bm_config.h 覆盖）
function(bm_console_apply_defaults config_target)
    set(_backend "${BM_BACKEND}")
    if(NOT _backend)
        return()
    endif()
    if(_backend STREQUAL "native_sim" OR _backend STREQUAL "native_sim_mp")
        target_compile_definitions(${config_target} INTERFACE
            BM_CONFIG_CONSOLE_LOG_BACKEND=1
            BM_CONFIG_CONSOLE_CLI_BACKEND=1)
    elseif(_backend MATCHES "^qemu_")
        target_compile_definitions(${config_target} INTERFACE
            BM_CONFIG_CONSOLE_LOG_BACKEND=2
            BM_CONFIG_CONSOLE_CLI_BACKEND=2)
    elseif(_backend STREQUAL "sdk_esp32_idf")
        target_compile_definitions(${config_target} INTERFACE
            BM_CONFIG_CONSOLE_LOG_BACKEND=3
            BM_CONFIG_CONSOLE_CLI_BACKEND=2)
    endif()
endfunction()
