/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_comp_sim.h
 * @brief 原生仿真比较器实例与测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 *
 * @par ????:
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */

#ifndef BM_HAL_COMP_SIM_H
#define BM_HAL_COMP_SIM_H

#include "bm_hal_comp.h"

extern const bm_hal_comp_t BM_HAL_COMP_SIM0;

/**
 * @brief 设置比较器仿真阈值。
 * @param comp 比较器设备实例。
 * @param threshold 比较器触发阈值。
 */
void bm_hal_comp_sim_set_threshold(const bm_hal_comp_t *comp, uint16_t threshold);
/**
 * @brief 向比较器注入样本并更新锁存状态。
 * @param comp 比较器设备实例。
 * @param sample 待注入的比较器样本值。
 */
void bm_hal_comp_sim_trip(const bm_hal_comp_t *comp, uint16_t sample);
/**
 * @brief 查询比较器仿真触发是否已锁存。
 * @param comp 比较器设备实例。
 * @return 条件成立时返回非 0，否则返回 0。
 */
int bm_hal_comp_sim_is_latched(const bm_hal_comp_t *comp);

#endif /* BM_HAL_COMP_SIM_H */
