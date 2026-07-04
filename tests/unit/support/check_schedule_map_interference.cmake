# SPDX-License-Identifier: GPL-3.0-or-later
#
# @file check_schedule_map_interference.cmake
# @brief schedule-map Task 6 内容级机械检查：读出表产物 JSON，断言干扰源
#        按 cpu 正确装配。
#
# @details 独立 cmake -P 脚本，供 add_test(... COMMAND ${CMAKE_COMMAND} -P ...)
# 调用；DIR 参数是出表目录（bm_schedule_map_dump 系列可执行文件的输出目录）。
# 断言 sched_fixture_a.json（cpu0）的 interference_sources 含 2 条声明干扰源
# （spi_isr/hardware、wifi_task/scheduled，tier 字符串与 period_us/wcet_us
# 数值均须逐字匹配），sched_fixture_b.json（cpu1）的 interference_sources
# 为空数组——未声明该 CPU 的干扰源，main 按 cpu 过滤后不得窜入。同一份脚本
# 对 CMake 生成路径（INTERFERENCE_SRC）与手写 reg 路径
# （schedule_map_fixture_reg.c）两个输出目录各跑一遍，验证两条契约实现
# 路径产出的干扰源 JSON 片段逐字节一致。
#
# 用法：cmake -DDIR=<出表目录> -P check_schedule_map_interference.cmake
#
# @author zeh (china_qzh@163.com)
# @date 2026-07-04

if(NOT DEFINED DIR)
    message(FATAL_ERROR "check_schedule_map_interference.cmake 需要 -DDIR=<出表目录>")
endif()

if(NOT EXISTS "${DIR}/sched_fixture_a.json" OR NOT EXISTS "${DIR}/sched_fixture_b.json")
    message(FATAL_ERROR "${DIR} 下缺 sched_fixture_a.json / sched_fixture_b.json（dump 是否已跑过？）")
endif()

file(READ "${DIR}/sched_fixture_a.json" _a_json)
file(READ "${DIR}/sched_fixture_b.json" _b_json)

if(NOT _a_json MATCHES "\"interference_sources\": \\[")
    message(FATAL_ERROR "${DIR}/sched_fixture_a.json 缺 interference_sources 数组")
endif()
if(NOT _a_json MATCHES "\\{\"name\": \"spi_isr\", \"period_us\": 1000, \"wcet_us\": 20, \"tier\": \"hardware\"\\}")
    message(FATAL_ERROR "${DIR}/sched_fixture_a.json 缺 spi_isr(hardware) 干扰源条目")
endif()
if(NOT _a_json MATCHES "\\{\"name\": \"wifi_task\", \"period_us\": 5000, \"wcet_us\": 300, \"tier\": \"scheduled\"\\}")
    message(FATAL_ERROR "${DIR}/sched_fixture_a.json 缺 wifi_task(scheduled) 干扰源条目")
endif()
if(NOT _b_json MATCHES "\"interference_sources\": \\[\\]")
    message(FATAL_ERROR "${DIR}/sched_fixture_b.json 的 interference_sources 应为空数组（cpu1 未声明干扰源，main 应已按 cpu 过滤掉 cpu0 的声明）")
endif()

message(STATUS "schedule-map 干扰源装配检查通过：${DIR}")
