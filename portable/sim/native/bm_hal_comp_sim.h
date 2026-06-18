/**
 * @file bm_hal_comp_sim.h
 * @brief 原生仿真比较器实例与测试辅助接口
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
 */

#ifndef BM_HAL_COMP_SIM_H
#define BM_HAL_COMP_SIM_H

#include "bm_hal_comp.h"

extern const bm_hal_comp_t BM_HAL_COMP_SIM0;

void bm_hal_comp_sim_set_threshold(const bm_hal_comp_t *comp, uint16_t threshold);
void bm_hal_comp_sim_trip(const bm_hal_comp_t *comp, uint16_t sample);
int bm_hal_comp_sim_is_latched(const bm_hal_comp_t *comp);

#endif /* BM_HAL_COMP_SIM_H */
