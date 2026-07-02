# tt_schedule_balance：bm_tt_schedule 平衡车场景 demo（ISR 域力矩环 + MAINLOOP 域遥测聚合）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(tt_schedule_balance_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(tt_schedule_balance
        FRAMEWORK_LIBS bm_core bm_tt_schedule
    )
    if(NOT MSVC)
        target_link_libraries(tt_schedule_balance PRIVATE m)
    endif()
else()
    # 本 demo 只跑在 native 主机上（main.c 用手动 tick 循环模拟节拍，无真实
    # 中断/HRT 硬件依赖），不提供 QEMU 后端，避免默认 QEMU 模式下 umbrella
    # 配置因缺 BM_ENABLE_TT_SCHEDULE 而报错。
    message(STATUS "tt_schedule_balance: 仅提供 native_sim 后端（无 QEMU/HRT 硬件依赖，main.c 用手动 tick 循环模拟）")
endif()
