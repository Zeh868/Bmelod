/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_pwm.h
 * @brief PWM HAL 接口
 *
 * 占空比设置、输出使能、安全态请求及 HRT 更新事件绑定。
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
#ifndef BM_HAL_PWM_H
#define BM_HAL_PWM_H

#include "drv/bm_drv_pwm.h"
#include "hal/bm_hal_hrt.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_pwm bm_hal_pwm_t;

/**
 * @brief 设置指定相位的 PWM 占空比
 *
 * @param pwm PWM 实例指针
 * @param phase 相位索引
 * @param duty 占空比值
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_pwm_set_duty(const bm_hal_pwm_t *pwm, uint32_t phase, uint16_t duty);

/**
 * @brief 使能或禁用 PWM 输出
 *
 * @param pwm PWM 实例指针
 * @param enable 非零为使能，0 为禁用
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_pwm_enable_outputs(const bm_hal_pwm_t *pwm, int enable);

/**
 * @brief 请求 PWM 进入硬件安全态（如关断输出）
 *
 * 工业安全关键函数：调用者必须检查返回值。
 * 失败时调用者应触发更高层级的紧急停机（如硬件看门狗复位）。
 *
 * @param pwm PWM 实例指针
 * @return BM_OK 成功进入安全态；BM_ERR_INVALID 参数无效；其他为平台错误码
 */
int bm_hal_pwm_request_safe_state(const bm_hal_pwm_t *pwm);

/**
 * @brief 绑定 PWM 更新事件到 HRT 回调
 *
 * @param pwm PWM 实例指针
 * @param binding HRT 绑定参数
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_pwm_bind_update(const bm_hal_pwm_t *pwm,
                           const bm_hal_hrt_binding_t *binding);

#endif /* BM_HAL_PWM_H */
