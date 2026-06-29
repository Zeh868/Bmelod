# bus_servo：native_sim / QEMU bm_bus 三 mode 伺服示例
# 移植 closed_loop_servo 算法，通讯层替换为 bm_bus QUEUE/LATEST/SIGNAL
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

# bm_bus 编译进 bm_core（BM_ENABLE_BUS 已默认 ON），此处显式确保算法可用
set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)
set(BM_ENABLE_BUS ON CACHE BOOL "" FORCE)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(bus_servo_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SHELL ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB BUS_SERVO_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(bus_servo
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm bm_shell
        EXTRA_SOURCES ${BUS_SERVO_MODULES} shell_cmds.c
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(bus_servo C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    set(EXAMPLE_ENABLE_SHELL ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB BUS_SERVO_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(bus_servo
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm bm_shell
        EXTRA_SOURCES ${BUS_SERVO_MODULES} shell_cmds.c
    )
endif()
