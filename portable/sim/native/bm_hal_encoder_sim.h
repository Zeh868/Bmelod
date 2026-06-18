/**
 * @file bm_hal_encoder_sim.h
 * @brief 原生仿真编码器实例与测试辅助接口
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_ENCODER_SIM_H
#define BM_HAL_ENCODER_SIM_H

#include "bm_hal_encoder.h"

extern const bm_hal_encoder_t BM_HAL_ENC_SIM0;

void bm_hal_encoder_sim_set_count(const bm_hal_encoder_t *enc, int32_t value);

#endif /* BM_HAL_ENCODER_SIM_H */
