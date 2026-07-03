# schedule-map 宿主子构建：交叉编译主构建/IDE 工程借宿主默认工具链（自动探测本机 C
# 编译器，与外层交叉工具链无关）在构建期出调度表。由 bm_add_schedule_map() 的
# CMAKE_CROSSCOMPILING 分支（cmake/bm_schedule_map.cmake）或二档脚本
# tools/board/build_schedule_map.ps1 以独立 `cmake -S/-B` 子进程配起，与外层配置
# 完全隔离（不继承外层 CMAKE_TOOLCHAIN_FILE/CMAKE_C_COMPILER）。
#
# 传参（全部 -D 注入；分号表参数调用方须用双引号整体传入，防止命令行二次分词）：
#   BM_SM_BMELOD_ROOT   框架根（含根 CMakeLists.txt / cmake/bmelod.cmake）
#   BM_SM_CONFIG_FILE   应用 bm_config_app.h 绝对路径（可留空：不少 fixture/最小装配
#                       本就不需要自定义容量配置）；保证 host 出表与 target 编译同配置
#   BM_SM_REG           注册单元 .c（bm_add_schedule_map() 生成的，或手写等价文件）
#   BM_SM_SOURCES       分号表：应用装配/bus 源
#   BM_SM_INCLUDE_DIRS  分号表：应用 include 目录
#
# 最小开关集依据（读 cmake/bmelod.cmake + cmake/bmelod_profiles.cmake + 根 CMakeLists.txt，
# 详见 schedule-map v2 Task 8 施工记录）：
#   - BM_ENABLE_TT_SCHEDULE：bmelod_profiles.cmake 六档 profile 都不会打开这个开关，
#     必须在 bmelod_configure() 之前显式 CACHE 置 ON（bm_tt_schedule 目标才会被创建）
#   - BM_ENABLE_HRT：根 CMakeLists.txt 里 BM_ENABLE_TT_SCHEDULE 的硬依赖（缺失即
#     FATAL_ERROR）；bmelod_profiles.cmake 的“增量启用”语义只认 configure 前已置 ON
#     的变量，所以也必须显式 CACHE 置 ON，不能指望某个 profile 顺带打开
#   - BM_ENABLE_BUS：根 CMakeLists.txt 中 option(BM_ENABLE_BUS ... ON) 默认已开，
#     这里显式写出仅为自文档化，不写也不影响出表
#   - BACKEND native_sim：bmelod_configure() 不传 BACKEND 时默认 "external"，
#     cmake/bm_port_resolve.cmake 的 bm_port_apply_backend("external") 直接 return，
#     不会创建 bm_hal_native 目标；而 bm_add_schedule_map() 的默认 SM_LINK_LIBS 硬编码
#     了 bm_hal_native —— 必须显式传 BACKEND native_sim，否则宿主子构建链接期报目标缺失。
#     native_sim 后端只用 portable/arch/host 的纯 C 实现，不需要交叉工具链
#   - bmelod_configure() 自身会把 BM_BUILD_TESTS 强制 CACHE 为 OFF，宿主子构建不会
#     拖起 tests/unit 整个单测套件（无需装 Unity）
#
# 出表目标固定名 schedule_map_dump：每个 bm_add_schedule_map(NAME) 调用都在独立的
# ${NAME}_host 子目录里配置这个工程，互不冲突。
cmake_minimum_required(VERSION 3.20)
project(bm_schedule_map_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BM_ENABLE_TT_SCHEDULE ON CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT ON CACHE BOOL "" FORCE)
set(BM_ENABLE_BUS ON CACHE BOOL "" FORCE)
if(BM_SM_CONFIG_FILE)
    set(BM_CONFIG_FILE ${BM_SM_CONFIG_FILE} CACHE FILEPATH "" FORCE)
endif()

include(${BM_SM_BMELOD_ROOT}/cmake/bmelod.cmake)
include(${BM_SM_BMELOD_ROOT}/cmake/bm_include_dirs.cmake)
bmelod_configure(ROOT ${BM_SM_BMELOD_ROOT} BACKEND native_sim)

add_executable(schedule_map_dump
    ${BM_SM_BMELOD_ROOT}/tools/schedule_map/bm_schedule_map_main.c
    ${BM_SM_REG}
    ${BM_SM_SOURCES})
target_include_directories(schedule_map_dump PRIVATE
    ${BM_SM_BMELOD_ROOT}/tools/schedule_map ${BM_SM_INCLUDE_DIRS})
# bm_tt_schedule.h / bm_bus.h 等在 include/bm/hybrid、include/bm/core 下（"内部"路径，
# 不在 bmelod::framework 的 PUBLIC 用法要求里），装配源码需要它们，故显式追加；
# 与 Demo/tt_schedule_balance/CMakeLists.txt、tests/unit/CMakeLists.txt 里
# bm_target_internal_includes(<dump 目标>) 的手法一致——这里没有外层调用方能补，
# 只能在模板内部自己补齐。
bm_target_internal_includes(schedule_map_dump)
bmelod_link(schedule_map_dump)
