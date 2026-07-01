function(bm_add_test TEST_NAME TEST_SRC TEST_LIBRARY)
    add_executable(${TEST_NAME} ${TEST_SRC})
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${TEST_NAME} PRIVATE
            "-Wl,--start-group"
            unity ${TEST_LIBRARY} bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(${TEST_NAME} PRIVATE
            unity ${TEST_LIBRARY} bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(${TEST_NAME})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

bm_add_test(test_skeleton test_skeleton.c bm_core)
bm_add_test(test_critical test_critical.c bm_core)
bm_add_test(test_log test_log.c bm_core)
bm_add_test(test_uptime test_uptime.c bm_core)
# bm_cpu_is_owner() 统一 owner 守卫原语单测（覆盖 no-op 与 BM_CPU_ANY 恒真语义）
bm_add_test(test_cpu_owner test_cpu_owner.c bm_core)
bm_add_test(test_idle test_idle.c bm_core)

# #10 持久化 KV 存储测试
bm_add_test(test_persist test_persist.c bm_core)
target_include_directories(test_persist PRIVATE
    ${CMAKE_SOURCE_DIR}/portable/sim/native)
bm_add_test(test_ultra test_ultra.c bm_core)
target_compile_definitions(test_ultra PRIVATE BM_ENABLE_ULTRA_TEST_HOOK=1)
bm_add_test(test_mempool test_mempool.c bm_core)
bm_add_test(test_event test_event.c bm_core)
target_compile_definitions(test_event PRIVATE BM_ENABLE_EVENT_TEST_HOOK=1)
bm_add_test(test_wdg test_wdg.c bm_wdg)
target_include_directories(test_wdg PRIVATE
    ${CMAKE_SOURCE_DIR}/portable/sim/native)

if(BM_ENABLE_MODULE)
    bm_add_test(test_module test_module.c bm_module)
endif()

if(BM_ENABLE_BUS)
    if(TARGET bm_stream)
        bm_add_test(test_bus test_bus.c "bm_stream;bm_core")
    elseif(TARGET bm_exec)
        bm_add_test(test_bus test_bus.c "bm_exec;bm_core")
    else()
        bm_add_test(test_bus test_bus.c bm_core)
    endif()
    target_compile_definitions(test_bus PRIVATE BM_ENABLE_BUS_TEST_HOOK=1)
    target_compile_definitions(test_bus PRIVATE BM_BUS_ALLOW_INTERNAL=1)

    # 确定性 L1 C1：bus retry-bound（DET-01）+ 非阻塞已由 test_bus 证明，纳入 determinism 标签
    set_tests_properties(test_bus PROPERTIES LABELS "determinism")

    # ---- 显式缺口登记（非占位，各有触发条件）----
    # [C2 缺口] exec drain budget 有界：bm_exec_drain_streams(budget) 需 exec+stream 已初始化；
    #   BM_ENABLE_EXEC_TEST_HOOK 宏在代码库中未找到（grep 2026-06-30 确认）。
    #   触发条件：待 bm_exec.h 增加 BM_ENABLE_EXEC_TEST_HOOK 或等效 hook 后，
    #   在 test_det_bounded.c 新增 test_det_bounded_exec_drain_budget 用例，
    #   注入固定数量 stream 块，断言 drain_streams(N) 处理条数 <= N。
    #
    # [C5-CAS 缺口] atomic CAS 迭代次数有界（多核路径）：
    #   单核 native 不激活 CAS 环（BM_CPU_LOCAL_ENABLE_ROUTE 默认 OFF）；
    #   多核有界性通过 bm_critical.c:78 + BM_CONFIG_ATOMIC_MAX_RETRIES=8 静态保证，
    #   对应确定性审查报告 D3/D6；L1 暂无 test hook 直接观测重试次数。
    #   触发条件：BM_CONFIG_SMP=ON 时可用 QEMU -smp 在竞争窗口插入延迟观测。
    #
    # [C6 缺口] ipc 非阻塞返回：BM_CONFIG_SMP=OFF 默认，bm_mp/ipc 不可用。
    #   触发条件：BM_CONFIG_SMP=ON SMP 构建 + bm_mp_add_test 中新增 ipc 非阻塞用例。
    bm_add_test(test_bus_migration test_bus_migration.c bm_core)

    # BM_BUS_IPC 模式契约测试：native 桩后端（fifo_stub + latest_stub）覆盖 FIFO 保序/满/空、
    # LATEST 最新值覆盖、未绑定 INVALID、错误模式 bind；不依赖 bm_mp/SMP。
    add_executable(test_bus_ipc_mode
        test_bus_ipc_mode.c
        ${CMAKE_SOURCE_DIR}/tests/unit/support/ipc_stub_backend.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_bus_ipc_mode PRIVATE
            "-Wl,--start-group"
            unity bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_bus_ipc_mode PRIVATE
            unity bm_core bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_bus_ipc_mode)
    target_include_directories(test_bus_ipc_mode PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/tests/unit)
    add_test(NAME test_bus_ipc_mode COMMAND test_bus_ipc_mode)

    # mp IPC backend adapter 契约测试：matrix cmd_ring（FIFO）+ tel_channel（LATEST）
    # 不链接 bm_mp；adapter 自包含；BM_CONFIG_CPU_COUNT=2u 构造 2×2 矩阵使
    # cmd_ring[0][1] 与 tel_channel[1][0] 索引有效（SMP=OFF 默认 CPU_COUNT=1 仅有 [0][0]）
    add_executable(test_mp_ipc_backend
        test_mp_ipc_backend.c
        ${CMAKE_SOURCE_DIR}/Source/mp/bm_mp_ipc_backend.c)
    target_compile_definitions(test_mp_ipc_backend PRIVATE BM_CONFIG_CPU_COUNT=2u)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_mp_ipc_backend PRIVATE
            "-Wl,--start-group"
            unity bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_mp_ipc_backend PRIVATE
            unity bm_core bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_mp_ipc_backend)
    target_include_directories(test_mp_ipc_backend PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/include/bm/mp)
    add_test(NAME test_mp_ipc_backend COMMAND test_mp_ipc_backend)
endif()

if(BM_ENABLE_SHELL)
    bm_add_test(test_shell test_shell.c bm_shell)

    if(BM_ENABLE_BUS)
        # #11 shell 业务命令集成：验证「命令字符串→业务动作生效」链路
        bm_add_test(test_shell_cmds test_shell_cmds.c bm_shell)
        target_sources(test_shell_cmds PRIVATE
            ${CMAKE_SOURCE_DIR}/Demo/bus_servo/shell_cmds.c)
        target_include_directories(test_shell_cmds PRIVATE
            ${CMAKE_SOURCE_DIR}/Demo/bus_servo)
    endif()
endif()

if(BM_ENABLE_HRT)
    bm_add_test(test_hrt test_hrt.c bm_hrt)
    target_include_directories(test_hrt PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
endif()

if(BM_ENABLE_TT_SCHEDULE)
    # Task 3：输入冻结 + seq-delta 判龄；走门面 API + 公共 bus API，
    # 不需要 BM_BUS_ALLOW_INTERNAL（该宏仅 bm_tt_schedule 库内部 PRIVATE 定义）。
    bm_add_test(test_tt_schedule test_tt_schedule.c bm_tt_schedule)
    # A5：本门面全静态分配（无 malloc/calloc/realloc/free 符号）+ 循环上界
    # 均受 init 期校验（entry_count/input_count/n_frames ≤ 编译期上界），
    # 纳入既有 determinism 回归面（见 test_tt_schedule.c 场景19注释）。
    set_tests_properties(test_tt_schedule PROPERTIES LABELS "determinism")

    # A5 补丁（Task 8 评审 Important-1）：零动态分配符号级机械检查——
    # 对 bm_tt_schedule 静态库产物的 COFF/ELF 符号表做扫描，命中
    # malloc/calloc/realloc/free 任一外部引用即 FAIL。比人工 grep 源码更
    # 权威：检查的是最终编译产物的真实符号引用，不依赖人工核对源码是否
    # 遗漏（见 tests/unit/support/check_no_alloc_symbols.cmake）。
    # MSVC 用 dumpbin /symbols；GNU 用 nm。若工具链下找不到对应工具，
    # 该检查不注册（不硬凑一个跑不了的假测试）。
    if(MSVC)
        find_program(BM_DUMPBIN_EXE dumpbin)
        if(BM_DUMPBIN_EXE)
            add_test(NAME test_tt_schedule_symbols_no_alloc
                COMMAND ${CMAKE_COMMAND}
                    -DTOOL_ID=MSVC
                    -DDUMPBIN_EXE=${BM_DUMPBIN_EXE}
                    -DLIB_PATH=$<TARGET_FILE:bm_tt_schedule>
                    -P ${CMAKE_SOURCE_DIR}/tests/unit/support/check_no_alloc_symbols.cmake)
            set_tests_properties(test_tt_schedule_symbols_no_alloc PROPERTIES
                LABELS "determinism")
        else()
            message(WARNING "dumpbin 未找到：test_tt_schedule_symbols_no_alloc 未注册"
                             "（需在 vcvars 环境下 configure 才能被 find_program 发现）")
        endif()
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        find_program(BM_NM_EXE nm)
        if(BM_NM_EXE)
            add_test(NAME test_tt_schedule_symbols_no_alloc
                COMMAND ${CMAKE_COMMAND}
                    -DTOOL_ID=GNU
                    -DNM_EXE=${BM_NM_EXE}
                    -DLIB_PATH=$<TARGET_FILE:bm_tt_schedule>
                    -P ${CMAKE_SOURCE_DIR}/tests/unit/support/check_no_alloc_symbols.cmake)
            set_tests_properties(test_tt_schedule_symbols_no_alloc PROPERTIES
                LABELS "determinism")
        else()
            message(WARNING "nm 未找到：test_tt_schedule_symbols_no_alloc 未注册")
        endif()
    endif()

    # Task 7：host dump 工具，装配示例调度表并导出 bm_tt_schedule_report
    # 到 build/schedule_map.txt，供开发者人工核对调度概览（非单元测试，
    # 随 ALL 目标构建）。
    add_executable(tt_schedule_map_dump ${CMAKE_SOURCE_DIR}/tests/tools/tt_schedule_map_dump.c)
    target_link_libraries(tt_schedule_map_dump PRIVATE bm_tt_schedule bm_core bm_hal bm_hal_native)
    bm_target_internal_includes(tt_schedule_map_dump)
    add_custom_target(schedule-map ALL
        COMMAND tt_schedule_map_dump ${CMAKE_BINARY_DIR}/schedule_map.txt
        DEPENDS tt_schedule_map_dump
        COMMENT "Generating build/schedule_map.txt")
endif()

if(BM_ENABLE_TICKER)
    bm_add_test(test_ticker test_ticker.c bm_ticker)
    target_include_directories(test_ticker PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
endif()

if(TARGET bm_health)
    bm_add_test(test_health test_health.c bm_health)
    target_include_directories(test_health PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
endif()

if(TARGET bm_resource)
    bm_add_test(test_resource_claim test_resource_claim.c bm_resource)
endif()

if(BM_ENABLE_EXEC)
    bm_add_test(test_exec test_exec.c bm_exec)
    target_include_directories(test_exec PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
endif()

if(TARGET bm_stream)
    bm_add_test(test_stream test_stream.c bm_stream)
elseif(BM_ENABLE_EXEC)
    bm_add_test(test_stream test_stream.c bm_exec)
endif()

bm_add_test(test_hal_dma_stream test_hal_dma_stream.c bm_core)
target_include_directories(test_hal_dma_stream PRIVATE
    ${CMAKE_SOURCE_DIR}/portable/sim/native)

if(BM_ENABLE_ALGORITHM)
    bm_add_test(test_algorithm test_algorithm.c bm_algorithm)
    if(NOT MSVC)
        target_link_libraries(test_algorithm PRIVATE m)
    endif()
endif()

if(TARGET bm_pipeline)
    bm_add_test(test_pipeline test_pipeline.c bm_pipeline)
endif()

if(TARGET bm_component_motor)
    bm_add_test(test_motor_foc_sensored
        test_motor_foc_sensored.c
        bm_component_motor)
    target_link_libraries(test_motor_foc_sensored PRIVATE bm_core)
    target_include_directories(test_motor_foc_sensored PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    bm_add_test(test_motor_foc_sensorless
        test_motor_foc_sensorless.c
        bm_component_motor)
    target_link_libraries(test_motor_foc_sensorless PRIVATE bm_core)
    target_include_directories(test_motor_foc_sensorless PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    bm_add_test(test_motor_current_sense test_motor_current_sense.c bm_component_motor)
    bm_add_test(test_robot_joint_control test_robot_joint_control.c bm_component_motor)
endif()

if(TARGET bm_component_bms)
    bm_add_test(test_bms_estimation test_bms_estimation.c bm_component_bms)
endif()

if(TARGET bm_component_fusion)
    bm_add_test(test_estimation_fusion
        test_estimation_fusion.c
        bm_component_fusion)
    if(NOT MSVC)
        target_link_libraries(test_estimation_fusion PRIVATE m)
    endif()
endif()

if(TARGET bm_component_power)
    bm_add_test(test_power_control test_power_control.c bm_component_power)
    bm_add_test(test_power_converter test_power_converter.c bm_component_power)
endif()

if(TARGET bm_component_process)
    bm_add_test(test_bms_supervision test_bms_supervision.c bm_component_process)
endif()

if(TARGET bm_component_process)
    bm_add_test(test_motion_profile test_motion_profile.c bm_component_process)
    bm_add_test(test_process_sequence test_process_sequence.c bm_component_process)
    bm_add_test(test_navigation_frontend test_navigation_frontend.c bm_component_process)
    bm_add_test(test_solar_control test_solar_control.c bm_component_process)
    bm_add_test(test_transport_qos_token test_transport_qos_token.c bm_component_process)
    bm_add_test(test_mobile_base_control test_mobile_base_control.c bm_component_process)
    bm_add_test(test_additive_motion test_additive_motion.c bm_component_process)
    bm_add_test(test_grid_control test_grid_control.c bm_component_process)
    if(NOT MSVC)
        target_link_libraries(test_grid_control PRIVATE m)
    endif()
    bm_add_test(test_process_control test_process_control.c bm_component_process)
    bm_add_test(test_motion_coordination test_motion_coordination.c bm_component_process)
    if(NOT MSVC)
        target_link_libraries(test_motion_coordination PRIVATE m)
    endif()
endif()

if(TARGET bm_component_diagnostics AND (BM_ENABLE_COMPONENT_STREAM OR BM_BUILD_TESTS))
    set(_bm_stream_frontend_test_libs bm_component_diagnostics)
    if(TARGET bm_stream)
        list(APPEND _bm_stream_frontend_test_libs bm_stream)
    elseif(TARGET bm_exec)
        list(APPEND _bm_stream_frontend_test_libs bm_exec)
    endif()
    bm_add_test(test_stream_frontend test_stream_frontend.c
        "${_bm_stream_frontend_test_libs}")
    bm_add_test(test_audio_array_frontend test_audio_array_frontend.c bm_component_diagnostics)
    bm_add_test(test_radar_frontend test_radar_frontend.c bm_component_diagnostics)
    if(NOT MSVC)
        target_link_libraries(test_radar_frontend PRIVATE m)
    endif()
    bm_add_test(test_daq_frontend test_daq_frontend.c bm_component_diagnostics)
    if(NOT MSVC)
        target_link_libraries(test_daq_frontend PRIVATE m)
    endif()
    bm_add_test(test_fault_derating test_fault_derating.c bm_component_diagnostics)
    bm_add_test(test_sensor_quality test_sensor_quality.c bm_component_diagnostics)
    bm_add_test(test_spectral_diagnostics test_spectral_diagnostics.c bm_component_diagnostics)
    if(NOT MSVC)
        target_link_libraries(test_spectral_diagnostics PRIVATE m)
    endif()
endif()

if(TARGET bm_component_tinyml)
    bm_add_test(test_tinyml_adapter test_tinyml_adapter.c bm_component_tinyml)
endif()

# C3+C4+C5：有界+非阻塞多路径
bm_add_test(test_det_bounded test_det_bounded.c bm_core)
target_compile_definitions(test_det_bounded PRIVATE BM_ENABLE_EVENT_TEST_HOOK=1)
set_tests_properties(test_det_bounded PROPERTIES LABELS "determinism")

if(TARGET bm_component_control_loop)
    bm_add_test(test_control_loop test_control_loop.c bm_component_control_loop)
    if(NOT MSVC)
        target_link_libraries(test_control_loop PRIVATE m)
    endif()
endif()

if(BM_ENABLE_SYNC)
    add_executable(test_sync test_sync.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_sync PRIVATE
            "-Wl,--start-group"
            unity bm_exec bm_hrt bm_resource bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_sync PRIVATE
            unity bm_exec bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_sync)
    target_include_directories(test_sync PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    add_test(NAME test_sync COMMAND test_sync)
endif()

if(BM_BUILD_TESTS)
    add_executable(test_recorder test_recorder.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_recorder PRIVATE
            "-Wl,--start-group"
            unity bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_recorder PRIVATE
            unity bm_core bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_recorder)
    target_include_directories(test_recorder PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    add_test(NAME test_recorder COMMAND test_recorder)

    add_executable(test_priority_mask
        test_priority_mask.c
        fakes/bm_hal_priority_mask_fake.c
    )
    target_compile_definitions(test_priority_mask PRIVATE
        BM_CONFIG_ENABLE_PRIORITY_MASK=1
        BM_HAL_HAS_PRIORITY_MASK=1
    )
    target_link_libraries(test_priority_mask PRIVATE unity bm_config)
    bm_target_internal_includes(test_priority_mask)
    target_include_directories(test_priority_mask PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/tests/unit/fakes)
    add_test(NAME test_priority_mask COMMAND test_priority_mask)

    if(NOT TARGET bm_port_arch_stub)
        add_subdirectory(${CMAKE_SOURCE_DIR}/portable/arch/stub
            _bm_unit_arch_stub EXCLUDE_FROM_ALL)
    endif()
    add_executable(test_arch_port test_arch_port.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_arch_port PRIVATE
            "-Wl,--start-group"
            unity bm_port_arch_stub
            "-Wl,--end-group")
    else()
        target_link_libraries(test_arch_port PRIVATE
            unity bm_port_arch_stub)
    endif()
    bm_target_internal_includes(test_arch_port)
    target_include_directories(test_arch_port PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/portable/arch)
    add_test(NAME test_arch_port COMMAND test_arch_port)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME check_phase3_layout
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/cmake/check_phase3_layout.py)
        add_test(NAME check_esp32_backend_configure
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/cmake/check_esp32_backend_configure.py)
    endif()
endif()

# =============================================================
# SMP 多核单元测试（仅 BM_CONFIG_SMP=ON 时构建）
# =============================================================
if(BM_CONFIG_SMP AND TARGET bm_mp)
    # 通用辅助函数：链接 bm_mp + bm_core + bm_hal_native
    function(bm_mp_add_test TEST_NAME TEST_SRC)
        add_executable(${TEST_NAME} ${TEST_SRC})
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_link_libraries(${TEST_NAME} PRIVATE
                "-Wl,--start-group"
                unity bm_mp bm_core bm_hal bm_hal_native
                "-Wl,--end-group")
        else()
            target_link_libraries(${TEST_NAME} PRIVATE
                unity bm_mp bm_hal bm_hal_native)
        endif()
        bm_target_internal_includes(${TEST_NAME})
        target_include_directories(${TEST_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/tests/unit/unity
            ${CMAKE_SOURCE_DIR}/portable/sim/native_mp
            ${CMAKE_SOURCE_DIR}/portable/sim/native)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endfunction()

    bm_mp_add_test(test_mp test_mp.c)
    if(TARGET bm_module AND TARGET bm_ticker)
        target_link_libraries(test_mp PRIVATE bm_module bm_ticker)
    endif()

    # [A4 缺口登记] 多核确定性测试（test_mp / test_mp_period 等）已有并发跑通证据：
    #   native 40/0 + qemu -smp 2 35/35 均实跑。
    # 触发条件：BM_CONFIG_SMP=ON 时将下列 SMP 测试纳入 determinism 标签。
    if(BM_CONFIG_SMP)
        set_tests_properties(test_mp PROPERTIES LABELS "determinism")
        if(TARGET test_mp_period)
            set_tests_properties(test_mp_period PROPERTIES LABELS "determinism")
        endif()
    endif()
    # [A4 未决] qemu_conc_core / qemu_bus_smp 等专项并发确定性测试（seqlock 真并发下
    #   逐位可复现性量化断言）属 L2 范围，待 P2 期 QEMU icount 构建时补入。

    bm_mp_add_test(test_mp_profile test_mp_profile.c)
    if(TARGET bm_module AND TARGET bm_ticker)
        target_link_libraries(test_mp_profile PRIVATE bm_module bm_ticker)
    endif()

    bm_mp_add_test(test_mp_period test_mp_period.c)
    if(TARGET bm_module AND TARGET bm_ticker)
        target_link_libraries(test_mp_period PRIVATE bm_module bm_ticker)
    endif()

    bm_mp_add_test(test_mp_resource_topology test_mp_resource_topology.c)

    bm_mp_add_test(test_mp_profile_epoch test_mp_profile_epoch.c)

    if(TARGET bm_stream OR BM_ENABLE_STREAM)
        bm_mp_add_test(test_stream_relay test_stream_relay.c)
    endif()

    # test_log_mp 需要额外库链接
    add_executable(test_log_mp test_log_mp.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_log_mp PRIVATE
            "-Wl,--start-group"
            unity bm_mp bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_log_mp PRIVATE
            unity bm_mp bm_core bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_log_mp)
    target_include_directories(test_log_mp PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/portable/sim/native_mp
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    add_test(NAME test_log_mp COMMAND test_log_mp)

    bm_mp_add_test(test_console test_console.c)
endif()

# ===== 确定性验证 L1（功能确定性）测试套件 =====
if(TARGET bm_algorithm)
    bm_add_test(test_det_repro test_det_repro.c bm_algorithm)
    if(NOT MSVC)
        target_link_libraries(test_det_repro PRIVATE m)
    endif()
    set_tests_properties(test_det_repro PROPERTIES LABELS "determinism")
endif()

# A3 框架 round-trip 可复现性（需要 BM_ENABLE_BUS 使 bm_bus 符号可用）
if(BM_ENABLE_BUS)
    bm_add_test(test_det_roundtrip test_det_roundtrip.c bm_core)
    set_tests_properties(test_det_roundtrip PROPERTIES LABELS "determinism")
endif()

# B 块：全框架零分配稳态练习（需 BM_ENABLE_BUS 使 bm_bus 符号可用）
if(TARGET bm_algorithm AND BM_ENABLE_BUS AND BM_ENABLE_HRT)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_executable(test_det_zeroalloc
            test_det_zeroalloc.c
            support/det_trap_alloc.c)
        target_link_libraries(test_det_zeroalloc PRIVATE
            "-Wl,--start-group"
            unity bm_algorithm bm_core bm_hrt bm_hal bm_hal_native
            "-Wl,--end-group"
            m)
        target_link_options(test_det_zeroalloc PRIVATE
            "-Wl,--wrap=malloc"
            "-Wl,--wrap=calloc"
            "-Wl,--wrap=realloc"
            "-Wl,--wrap=free")
        bm_target_internal_includes(test_det_zeroalloc)
        target_include_directories(test_det_zeroalloc PRIVATE
            ${CMAKE_SOURCE_DIR}/tests/unit/unity)
        add_test(NAME test_det_zeroalloc COMMAND test_det_zeroalloc)
    else()
        # MSVC：CRT 钩子计数（smoke 路径，权威在 GNU --wrap）
        bm_add_test(test_det_zeroalloc test_det_zeroalloc.c
            "bm_algorithm;bm_core;bm_hrt")
    endif()
    set_tests_properties(test_det_zeroalloc PROPERTIES LABELS "determinism")
endif()
