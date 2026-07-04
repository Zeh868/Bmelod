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
# 用法（legacy，DIR 模式，Task 6）：
#   cmake -DDIR=<出表目录> -P check_schedule_map_interference.cmake
#
# Task 4 新增通用模式：传 -DEXPECT_NAME 即切换——先跑 DUMP_EXE 把出表刷新到
# OUT_DIR（不依赖 ctest 默认串行顺序去蹭 POST_BUILD 已生成的产物），再断言
# OUT_DIR/sched_fixture_a.json 含 {EXPECT_NAME, EXPECT_PERIOD, EXPECT_WCET,
# EXPECT_TIER} 描述的干扰源条目；若给了 EXPECT_OP_HZ，额外断言
# operating_points_hz 数组含该频率点。用于 config 单源展开注入测试
# （schedule_map_dump_cfg_ok），legacy DIR 模式行为不受影响。
#
# 用法（通用模式）：
#   cmake -DDUMP_EXE=<dump 可执行> -DOUT_DIR=<出表目录>
#         -DEXPECT_NAME=<name> -DEXPECT_TIER=<hardware|scheduled>
#         -DEXPECT_PERIOD=<period_us> -DEXPECT_WCET=<wcet_us>
#         [-DEXPECT_OP_HZ=<频率点>]
#         -P check_schedule_map_interference.cmake
#
# @author zeh (china_qzh@163.com)
# @date 2026-07-04

if(DEFINED EXPECT_NAME)
    if(NOT DEFINED DUMP_EXE OR NOT DEFINED OUT_DIR)
        message(FATAL_ERROR "EXPECT_NAME 模式需要 -DDUMP_EXE=<dump 可执行> -DOUT_DIR=<出表目录>")
    endif()
    if(NOT DEFINED EXPECT_PERIOD OR NOT DEFINED EXPECT_WCET OR NOT DEFINED EXPECT_TIER)
        message(FATAL_ERROR "EXPECT_NAME 模式需同时给 -DEXPECT_PERIOD/-DEXPECT_WCET/-DEXPECT_TIER")
    endif()
    file(MAKE_DIRECTORY "${OUT_DIR}")
    execute_process(COMMAND "${DUMP_EXE}" "${OUT_DIR}" RESULT_VARIABLE _cfg_rc)
    if(NOT _cfg_rc EQUAL 0)
        message(FATAL_ERROR "dump 可执行退出非零（rc=${_cfg_rc}）：${DUMP_EXE} ${OUT_DIR}")
    endif()
    set(_cfg_json_file "${OUT_DIR}/sched_fixture_a.json")
    if(NOT EXISTS "${_cfg_json_file}")
        message(FATAL_ERROR "${_cfg_json_file} 不存在（dump 是否已跑过？）")
    endif()
    file(READ "${_cfg_json_file}" _cfg_json)
    if(NOT _cfg_json MATCHES "\\{\"name\": \"${EXPECT_NAME}\", \"period_us\": ${EXPECT_PERIOD}, \"wcet_us\": ${EXPECT_WCET}, \"tier\": \"${EXPECT_TIER}\"\\}")
        message(FATAL_ERROR "${_cfg_json_file} 缺 ${EXPECT_NAME}(${EXPECT_TIER}) 干扰源条目——config 单源展开未生效？")
    endif()
    if(DEFINED EXPECT_OP_HZ)
        # CMake 正则不支持 \b 词边界，改用捕获组取出 [] 内整段再逐元素比对，
        # 避免子串误配（例如 EXPECT_OP_HZ=0 误中其它频率点里的 "0"）。
        if(NOT _cfg_json MATCHES "\"operating_points_hz\": \\[([^]]*)\\]")
            message(FATAL_ERROR "${_cfg_json_file} 缺 operating_points_hz 数组")
        endif()
        set(_cfg_op_list "${CMAKE_MATCH_1}")
        string(REPLACE "," ";" _cfg_op_list "${_cfg_op_list}")
        string(STRIP "${_cfg_op_list}" _cfg_op_list)
        set(_cfg_op_found FALSE)
        foreach(_cfg_op IN LISTS _cfg_op_list)
            string(STRIP "${_cfg_op}" _cfg_op)
            if(_cfg_op STREQUAL "${EXPECT_OP_HZ}")
                set(_cfg_op_found TRUE)
            endif()
        endforeach()
        if(NOT _cfg_op_found)
            message(FATAL_ERROR "${_cfg_json_file} 的 operating_points_hz [${_cfg_op_list}] 缺 ${EXPECT_OP_HZ}——BM_CONFIG_CPU_DVFS_POINTS_HZ 未生效？")
        endif()
    endif()
    message(STATUS "schedule-map config 单源干扰源装配检查通过：${_cfg_json_file}")
    return()
endif()

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
