/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_schedule_map_reg.h
 * @brief Schedule-map 注册单元契约：由 CMake 生成或手写，
 * 见 spec 2.2 节（编译期 schedule-map 导出）
 *
 * @details "注册单元"是一个小型翻译单元（由 CMake 里的
 * `bm_add_schedule_map()` 生成，或为 IDE 二/三档手写），列出应用想要
 * 导出的真实 `bm_tt_schedule_t` 表，外加供 JSON 导出用的可选参考时钟/
 * 工作点元数据（见 `bm_tt_schedule_json_meta_t`）。通用出表程序
 * `bm_schedule_map_main.c` 只链接恰好一个注册单元，遍历
 * `g_bm_schedule_map_entries[]` 为每张表产出 `.txt`/`.json`。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：注册单元契约初版
 * 2026-07-04       1.1            zeh            Task 6：新增
 *                                                 `bm_schedule_map_interference_t`
 *                                                 干扰源声明数组契约（{src, cpu}
 *                                                 嵌套形状），供 main 按当前表
 *                                                 cpu 过滤装配进
 *                                                 `bm_tt_schedule_json_meta_t`
 *
 */
#ifndef BM_SCHEDULE_MAP_REG_H
#define BM_SCHEDULE_MAP_REG_H

#include "bm_tt_schedule.h"

/** @brief 单条调度表条目：表实例 + 所属 CPU */
typedef struct {
    bm_tt_schedule_t *sched; /**< 表实例（由应用装配单元 extern 引用） */
    uint8_t            cpu;   /**< 该表所属的 CPU */
} bm_schedule_map_entry_t;

/** @brief 本注册单元导出的表条目（数组，长度 = g_bm_schedule_map_entry_count） */
extern const bm_schedule_map_entry_t g_bm_schedule_map_entries[];
/** @brief g_bm_schedule_map_entries[] 的条目数 */
extern const uint32_t g_bm_schedule_map_entry_count;
/** @brief 本注册单元所有表共用的 wcet 参考时钟（Hz）；0=未声明 */
extern const uint32_t g_bm_schedule_map_ref_clk_hz;
/** @brief 工作点频率数组（即使 count 为 0 也至少有 1 个占位元素） */
extern const uint32_t g_bm_schedule_map_op_points_hz[];
/** @brief g_bm_schedule_map_op_points_hz[] 的实际元素个数（可为 0） */
extern const uint32_t g_bm_schedule_map_op_point_count;

/**
 * @brief 单条 HRT 抢占干扰源声明：干扰源描述（见 `bm_tt_sched_intf_src_t`）
 * + 其所属 CPU
 *
 * @details 本注册单元声明的干扰源横跨全部 CPU；通用出表程序
 * （`bm_schedule_map_main.c`）在装配每张表的 `bm_tt_schedule_json_meta_t`
 * 时，按该表自身的 cpu 过滤出属于同一 CPU 的干扰源子集再挂上 meta——
 * 因此同一 CPU 下的多张表会共用该 CPU 的干扰源集合，跨 CPU 互不可见。
 */
typedef struct {
    bm_tt_sched_intf_src_t src; /**< 干扰源描述（name/period_us/wcet_us/tier） */
    uint8_t                cpu;  /**< 该干扰源所属的 CPU */
} bm_schedule_map_interference_t;

/** @brief 本注册单元声明的全部干扰源（数组，长度 =
 *  g_bm_schedule_map_interference_count；未声明干扰源时可为仅含占位元素的
 *  空数组，count 填 0） */
extern const bm_schedule_map_interference_t g_bm_schedule_map_interference[];
/** @brief g_bm_schedule_map_interference[] 的实际元素个数（可为 0） */
extern const uint32_t g_bm_schedule_map_interference_count;

/**
 * @brief 注册单元的 setup 钩子：在调用 bm_tt_schedule_init() 之前，
 * 打开总线/准备好装配表所需的一切
 *
 * @return 0 成功；非 0 会中止出表并报错
 */
int bm_schedule_map_setup(void);

#endif /* BM_SCHEDULE_MAP_REG_H */
