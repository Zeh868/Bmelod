/* SPDX-License-Identifier: GPL-3.0-or-later */
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

/**
 * @brief 测试辅助：设置 sim 编码器读故障注入。
 *
 * fail 非零时 native_encoder_read 返回非 BM_OK（模拟 I2C 读失败），
 * 用于验证组件 encoder_timeout_s 丢样容忍逻辑。
 *
 * @param[in] enc  目标 sim 编码器实例。
 * @param[in] fail 非零=后续 read 失败；0=恢复正常读。
 */
void bm_hal_encoder_sim_set_fail(const bm_hal_encoder_t *enc, int fail);

#endif /* BM_HAL_ENCODER_SIM_H */
