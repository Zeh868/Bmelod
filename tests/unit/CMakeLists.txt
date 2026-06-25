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

if(BM_ENABLE_CHANNEL)
    bm_add_test(test_channel test_channel.c bm_channel)
endif()

if(BM_ENABLE_BUS)
    bm_add_test(test_bus test_bus.c bm_core)
endif()

if(BM_ENABLE_SHELL)
    bm_add_test(test_shell test_shell.c bm_shell)
endif()

if(BM_ENABLE_HRT)
    bm_add_test(test_hrt test_hrt.c bm_hrt)
    target_include_directories(test_hrt PRIVATE
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
endif()

if(BM_ENABLE_TICKER)
    bm_add_test(test_ticker test_ticker.c bm_ticker)
    target_include_directories(test_ticker PRIVATE
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
    add_executable(test_snapshot test_snapshot.c)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(test_snapshot PRIVATE
            "-Wl,--start-group"
            unity bm_core bm_hal bm_hal_native
            "-Wl,--end-group")
    else()
        target_link_libraries(test_snapshot PRIVATE
            unity bm_core bm_hal bm_hal_native)
    endif()
    bm_target_internal_includes(test_snapshot)
    target_include_directories(test_snapshot PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/unit/unity
        ${CMAKE_SOURCE_DIR}/portable/sim/native)
    add_test(NAME test_snapshot COMMAND test_snapshot)

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
