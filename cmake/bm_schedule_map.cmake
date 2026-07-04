# bm_schedule_map.cmake —— 构建期调度表导出（spec: 2026-07-03-schedule-map-export-design）
# 生成注册单元 + host dump 可执行 + POST_BUILD 出表；见 docs/02-构建与工具链/01。

function(bm_add_schedule_map NAME)
    cmake_parse_arguments(SM ""
        "SETUP;REF_CLK_HZ;OUTPUT_DIR"
        "SOURCES;TABLES;INCLUDE_DIRS;OPERATING_POINTS;LINK_LIBS;INTERFERENCE_SRC" ${ARGN})
    if(NOT SM_SOURCES OR NOT SM_SETUP OR NOT SM_TABLES)
        message(FATAL_ERROR "bm_add_schedule_map(${NAME}): SOURCES/SETUP/TABLES 必填")
    endif()
    # 调用方是否显式传了 REF_CLK_HZ：须在下面的默认值兜底之前判定，否则
    # "未传" 与 "传了 0" 无法区分，会误吞掉"读 config"这条回退路径。
    if(DEFINED SM_REF_CLK_HZ)
        set(_sm_has_ref_clk_hz TRUE)
    else()
        set(_sm_has_ref_clk_hz FALSE)
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
    # 频率数据源优先级：CMake 参数 > config 宏 > 空。REF_CLK_HZ 没显式传，就在
    # 生成的 C 里直接读 BM_CONFIG_CPU_FREQ_HZ（reg.c include bm_schedule_map_reg.h
    # → bm_tt_schedule.h → bm/common/bm_types.h 链到 bm_config.h，宏可解析，
    # 不必再显式 include）；OPERATING_POINTS 同理回退读 BM_CONFIG_CPU_DVFS_POINTS_HZ
    # （应用可选定义的花括号初始化列表，框架不给缺省值）。两条路径生成互斥的定义，
    # 不会同时写出去导致重复定义。
    if(_sm_has_ref_clk_hz)
        set(_ref_clk_line "const uint32_t g_bm_schedule_map_ref_clk_hz = ${SM_REF_CLK_HZ}u;")
    else()
        set(_ref_clk_line "const uint32_t g_bm_schedule_map_ref_clk_hz = BM_CONFIG_CPU_FREQ_HZ;")
    endif()
    set(_ops "0u")
    set(_opn 0)
    if(SM_OPERATING_POINTS)
        list(LENGTH SM_OPERATING_POINTS _opn)
        # 与 tools/schedule_map_tool.py 的 MAX_OP_POINTS 上限一致：超过会撑爆
        # 生成的 JSON 行缓冲（bm_schedule_map_main.c 定长栈缓冲区），构建期防呆。
        if(_opn GREATER 8)
            message(FATAL_ERROR "bm_add_schedule_map(${NAME}): OPERATING_POINTS 最多 8 个（当前 ${_opn}），超出会撑爆 JSON 行缓冲")
        endif()
        list(JOIN SM_OPERATING_POINTS "u, " _ops)
        string(APPEND _ops "u")
        set(_op_points_block "const uint32_t g_bm_schedule_map_op_points_hz[] = { ${_ops} };
const uint32_t g_bm_schedule_map_op_point_count = ${_opn}u;
")
    else()
        set(_op_points_block "#ifdef BM_CONFIG_CPU_DVFS_POINTS_HZ
const uint32_t g_bm_schedule_map_op_points_hz[] = BM_CONFIG_CPU_DVFS_POINTS_HZ;
const uint32_t g_bm_schedule_map_op_point_count =
    (uint32_t)(sizeof(g_bm_schedule_map_op_points_hz) / sizeof(g_bm_schedule_map_op_points_hz[0]));
_Static_assert(sizeof(g_bm_schedule_map_op_points_hz)/sizeof(uint32_t) <= 8,
    \"BM_CONFIG_CPU_DVFS_POINTS_HZ 最多 8 个频率点\");
#else
const uint32_t g_bm_schedule_map_op_points_hz[] = { 0u };
const uint32_t g_bm_schedule_map_op_point_count = 0u;
#endif
")
    endif()
    # ---- 干扰源声明数组：INTERFERENCE_SRC 每条 "name:period_us:wcet_us:tier:cpu"
    # （tier∈hardware/scheduled，映射 0/1）；未提供该参数则生成空数组占位元素 +
    # count 0（与现状兼容，opt-in）。非法条目直接 FATAL_ERROR 报出原始字符串，
    # 不静默吞掉——错配的干扰源比没有更危险（会让 schedule_map_tool.py 算出
    # 错误的 ceiling 上界却看着像"已声明、已核实"）。
    set(_intf_entries "")
    set(_intf_n 0)
    foreach(_i IN LISTS SM_INTERFERENCE_SRC)
        if(_i MATCHES "^([A-Za-z_][A-Za-z0-9_]*):([0-9]+):([0-9]+):(hardware|scheduled):([0-9]+)$")
            set(_iname   ${CMAKE_MATCH_1})
            set(_iperiod ${CMAKE_MATCH_2})
            set(_iwcet   ${CMAKE_MATCH_3})
            set(_itier_s ${CMAKE_MATCH_4})
            set(_icpu    ${CMAKE_MATCH_5})
            if(_itier_s STREQUAL "hardware")
                set(_itier 0)
            else()
                set(_itier 1)
            endif()
            string(APPEND _intf_entries
                "    { { \"${_iname}\", ${_iperiod}u, ${_iwcet}u, ${_itier}u }, ${_icpu}u },\n")
            math(EXPR _intf_n "${_intf_n}+1")
        else()
            message(FATAL_ERROR "bm_add_schedule_map(${NAME}): INTERFERENCE_SRC 条目格式非法（要 name:period_us:wcet_us:tier:cpu，tier∈hardware/scheduled，均为非负整数）: \"${_i}\"")
        endif()
    endforeach()
    if(_intf_n EQUAL 0)
        # 与 g_bm_schedule_map_op_points_hz 的零元素占位同一惯例：空初始化列表
        # `{}` 在严格 C 下是非法的，占位元素规避该问题，count 仍如实填 0。
        set(_intf_entries "    { { \"\", 0u, 0u, 0u }, 0u },\n")
    endif()

    list(LENGTH SM_TABLES _tn)
    set(_reg ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_reg.c)
    file(WRITE ${_reg} "/* 自动生成：bm_add_schedule_map(${NAME})，勿手改 */
#include \"bm_schedule_map_reg.h\"
${_externs}extern int ${SM_SETUP}(void);
const bm_schedule_map_entry_t g_bm_schedule_map_entries[] = {
${_entries}};
const uint32_t g_bm_schedule_map_entry_count = ${_tn}u;
${_ref_clk_line}
${_op_points_block}const bm_schedule_map_interference_t g_bm_schedule_map_interference[] = {
${_intf_entries}};
const uint32_t g_bm_schedule_map_interference_count = ${_intf_n}u;
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
