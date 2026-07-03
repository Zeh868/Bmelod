# bm_schedule_map.cmake —— 构建期调度表导出（spec: 2026-07-03-schedule-map-export-design）
# 生成注册单元 + host dump 可执行 + POST_BUILD 出表；见 docs/02-构建与工具链/01。

function(bm_add_schedule_map NAME)
    cmake_parse_arguments(SM ""
        "SETUP;REF_CLK_HZ;OUTPUT_DIR"
        "SOURCES;TABLES;INCLUDE_DIRS;OPERATING_POINTS;LINK_LIBS" ${ARGN})
    if(NOT SM_SOURCES OR NOT SM_SETUP OR NOT SM_TABLES)
        message(FATAL_ERROR "bm_add_schedule_map(${NAME}): SOURCES/SETUP/TABLES 必填")
    endif()
    if(NOT SM_REF_CLK_HZ)
        set(SM_REF_CLK_HZ 0)
    endif()
    if(NOT SM_OUTPUT_DIR)
        set(SM_OUTPUT_DIR ${CMAKE_BINARY_DIR}/schedule_map)
    endif()
    file(MAKE_DIRECTORY ${SM_OUTPUT_DIR})
    # native / CROSSCOMPILING 两分支共用：框架根路径、Python3（级 2 复合分析用）
    get_filename_component(_bmroot ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.. ABSOLUTE)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    # ---- 生成注册单元 ----
    set(_externs "")
    set(_entries "")
    foreach(_t IN LISTS SM_TABLES)
        if(_t MATCHES "^([A-Za-z_][A-Za-z0-9_]*):([0-9]+)$")
            set(_sched ${CMAKE_MATCH_1})
            set(_cpu ${CMAKE_MATCH_2})
        else()
            set(_sched ${_t})
            set(_cpu 0)
        endif()
        string(APPEND _externs "extern bm_tt_schedule_t ${_sched};\n")
        string(APPEND _entries "    { &${_sched}, ${_cpu}u },\n")
    endforeach()
    set(_ops "0u")
    set(_opn 0)
    if(SM_OPERATING_POINTS)
        list(JOIN SM_OPERATING_POINTS "u, " _ops)
        string(APPEND _ops "u")
        list(LENGTH SM_OPERATING_POINTS _opn)
    endif()
    list(LENGTH SM_TABLES _tn)
    set(_reg ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_reg.c)
    file(WRITE ${_reg} "/* 自动生成：bm_add_schedule_map(${NAME})，勿手改 */
#include \"bm_schedule_map_reg.h\"
${_externs}extern int ${SM_SETUP}(void);
const bm_schedule_map_entry_t g_bm_schedule_map_entries[] = {
${_entries}};
const uint32_t g_bm_schedule_map_entry_count = ${_tn}u;
const uint32_t g_bm_schedule_map_ref_clk_hz = ${SM_REF_CLK_HZ}u;
const uint32_t g_bm_schedule_map_op_points_hz[] = { ${_ops} };
const uint32_t g_bm_schedule_map_op_point_count = ${_opn}u;
int bm_schedule_map_setup(void) { return ${SM_SETUP}(); }
")

    # ---- 交叉编译：借宿主默认工具链的独立子构建出表（不产生真实 ${NAME} 编译目标，
    # 用 add_custom_target 代替；调用方之后不能再对 ${NAME} 做 target_include_directories
    # 之类的操作——这与 native 分支下 ${NAME} 是可执行目标不同，是当前已知的一处调用约束，
    # 留给未来接真实交叉后端时的调用方注意）----
    if(CMAKE_CROSSCOMPILING)
        if(CMAKE_HOST_WIN32)
            set(_host_exe_suffix ".exe")
        else()
            set(_host_exe_suffix "")
        endif()
        set(_host_bin ${CMAKE_BINARY_DIR}/${NAME}_host)
        set(_host_dump ${_host_bin}/schedule_map_dump${_host_exe_suffix})
        # 显式指定 Ninja：子构建需要单一配置产物目录（Visual Studio 等多配置生成器
        # 会在产物路径里插入 Debug/Release 子目录，位置不确定），宿主机需装 Ninja。
        set(_sm_configure_cmd ${CMAKE_COMMAND} -S ${_bmroot}/cmake/schedule_map_host -B ${_host_bin}
                -G Ninja
                -DBM_SM_BMELOD_ROOT=${_bmroot}
                -DBM_SM_CONFIG_FILE=${BM_CONFIG_FILE}
                -DBM_SM_REG=${_reg}
                "-DBM_SM_SOURCES=${SM_SOURCES}"
                "-DBM_SM_INCLUDE_DIRS=${SM_INCLUDE_DIRS}")
        set(_sm_build_cmd ${CMAKE_COMMAND} --build ${_host_bin})
        if(Python3_FOUND)
            add_custom_target(${NAME} ALL
                COMMAND ${_sm_configure_cmd}
                COMMAND ${_sm_build_cmd}
                COMMAND ${_host_dump} ${SM_OUTPUT_DIR}
                COMMAND ${Python3_EXECUTABLE} ${_bmroot}/tools/schedule_map_tool.py
                        --dir ${SM_OUTPUT_DIR} --out ${SM_OUTPUT_DIR}/schedule_map_all.txt
                COMMENT "schedule-map(${NAME}): 交叉编译宿主子构建出表并复合分析 → ${SM_OUTPUT_DIR}"
                VERBATIM)
        else()
            add_custom_target(${NAME} ALL
                COMMAND ${_sm_configure_cmd}
                COMMAND ${_sm_build_cmd}
                COMMAND ${_host_dump} ${SM_OUTPUT_DIR}
                COMMENT "schedule-map(${NAME}): 交叉编译宿主子构建出表 → ${SM_OUTPUT_DIR}（Python3 未找到，级 2 复合分析跳过）"
                VERBATIM)
        endif()
        return()
    endif()

    # ---- dump 可执行 ----
    add_executable(${NAME}
        ${_bmroot}/tools/schedule_map/bm_schedule_map_main.c
        ${_reg}
        ${SM_SOURCES})
    target_include_directories(${NAME} PRIVATE
        ${_bmroot}/tools/schedule_map ${SM_INCLUDE_DIRS})
    if(NOT SM_LINK_LIBS)
        set(SM_LINK_LIBS bm_tt_schedule bm_core bm_hal bm_hal_native)
    endif()
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_libraries(${NAME} PRIVATE
            "-Wl,--start-group" ${SM_LINK_LIBS}
            $<$<BOOL:${BM_ENABLE_WCET_MON}>:bm_wcet_mon> "-Wl,--end-group")
    else()
        target_link_libraries(${NAME} PRIVATE ${SM_LINK_LIBS})
    endif()

    # ---- POST_BUILD：dump → 级 2 → 表进构建输出（Python3_FOUND 已在函数前部求好） ----
    if(Python3_FOUND)
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND $<TARGET_FILE:${NAME}> ${SM_OUTPUT_DIR}
            COMMAND ${Python3_EXECUTABLE} ${_bmroot}/tools/schedule_map_tool.py
                    --dir ${SM_OUTPUT_DIR} --out ${SM_OUTPUT_DIR}/schedule_map_all.txt
            COMMENT "schedule-map: 导出并复合分析 → ${SM_OUTPUT_DIR}")
    else()
        add_custom_command(TARGET ${NAME} POST_BUILD
            COMMAND $<TARGET_FILE:${NAME}> ${SM_OUTPUT_DIR}
            COMMENT "schedule-map: 导出 → ${SM_OUTPUT_DIR}（Python3 未找到，级 2 复合分析跳过）")
    endif()
endfunction()
