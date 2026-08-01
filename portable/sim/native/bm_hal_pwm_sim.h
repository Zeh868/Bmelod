/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_pwm_sim.h
 * @brief 原生仿真 PWM 实例与测试辅助接口
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
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */

#ifndef BM_HAL_PWM_SIM_H
#define BM_HAL_PWM_SIM_H

#include "bm_hal_pwm.h"

extern const bm_hal_pwm_t BM_HAL_PWM_SIM0;
extern const bm_hal_pwm_t BM_HAL_PWM_SIM1;
extern const bm_hal_pwm_t BM_HAL_PWM_SIM2;

/**
 * @brief 触发 PWM 更新事件及已绑定回调。
 * @param pwm PWM 设备实例。
 */
void bm_hal_pwm_sim_fire_update(const bm_hal_pwm_t *pwm);
/**
 * @brief 读取 PWM 指定相的仿真占空比值。
 * @param pwm PWM 设备实例。
 * @param phase PWM 相索引。
 * @return 指定 PWM 相的占空比编码值；设备或相无效时返回 0。
 */
uint16_t bm_hal_pwm_sim_get_duty(const bm_hal_pwm_t *pwm, uint32_t phase);
/**
 * @brief 查询 PWM 仿真输出是否启用。
 * @param pwm PWM 设备实例。
 * @return 条件成立时返回非 0，否则返回 0。
 */
int bm_hal_pwm_sim_outputs_enabled(const bm_hal_pwm_t *pwm);

#endif /* BM_HAL_PWM_SIM_H */
