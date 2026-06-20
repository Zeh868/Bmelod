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

# 按 BM_BACKEND 注入默认 Console 通道后端（可被工程 set(BM_CONSOLE_LOG_BACKEND/CLI_BACKEND) 覆盖，
# 须在 add_subdirectory 引入本框架之前 set，否则子作用域读不到覆盖值）
function(bm_console_apply_defaults config_target)
    set(_backend "${BM_BACKEND}")
    if(NOT _backend)
        return()
    endif()

    # 先按后端算出该平台的默认值
    if(_backend STREQUAL "native_sim" OR _backend STREQUAL "native_sim_mp")
        set(_log_default 1)
        set(_cli_default 1)
    elseif(_backend MATCHES "^qemu_")
        set(_log_default 2)
        set(_cli_default 2)
    elseif(_backend STREQUAL "sdk_esp32_idf")
        # esp32 实时主循环 LOG 默认走 RTT（非阻塞有界，保护 WCET 闭包）；
        # CLI 走 UART（可阻塞交互通道）。
        # 非实时冒烟工程可在 add_subdirectory 之前 set(BM_CONSOLE_LOG_BACKEND 2) 切换为 UART。
        set(_log_default 3)
        set(_cli_default 2)
    else()
        return()
    endif()

    # 若工程显式 set(BM_CONSOLE_LOG_BACKEND/CLI_BACKEND)，则以工程值为准；否则用平台默认
    if(DEFINED BM_CONSOLE_LOG_BACKEND)
        set(_log "${BM_CONSOLE_LOG_BACKEND}")
    else()
        set(_log "${_log_default}")
    endif()
    if(DEFINED BM_CONSOLE_CLI_BACKEND)
        set(_cli "${BM_CONSOLE_CLI_BACKEND}")
    else()
        set(_cli "${_cli_default}")
    endif()

    target_compile_definitions(${config_target} INTERFACE
        BM_CONFIG_CONSOLE_LOG_BACKEND=${_log}
        BM_CONFIG_CONSOLE_CLI_BACKEND=${_cli})
endfunction()
