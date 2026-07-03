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
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：注册单元契约初版
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
 * @brief 注册单元的 setup 钩子：在调用 bm_tt_schedule_init() 之前，
 * 打开总线/准备好装配表所需的一切
 *
 * @return 0 成功；非 0 会中止出表并报错
 */
int bm_schedule_map_setup(void);

#endif /* BM_SCHEDULE_MAP_REG_H */
