/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse.c
 * @brief STEP/DIR 脉冲步进驱动组件实现（芯片无关，resources 回调驱动）
 *
 * 定时模型：arm_timer 为一次性定时；on_timer 每次到期消费一个半周期
 * （STEP 翻转一次），上升沿 position ±1，随后按当前速度的半周期重新
 * 武装。方向切换：set_velocity 立即 dir_set 并置 dir_wait_pending，
 * 下一次 on_timer 不翻脉冲、仅武装 dir_setup_us 建立槽一次。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            stop 时 STEP 已为低则跳过 step_low
 *
 */
#include "bm/component/stepper_pulse.h"

#include <stddef.h>

/**
 * @brief 由当前速度计算半周期（µs），下限由 max_step_rate_hz 钳制。
 */
static uint32_t bm_stepper_pulse_half_period_us(const bm_stepper_pulse_axis_t *axis)
{
    float    v = axis->state.velocity_sps;
    float    abs_v = (v < 0.0f) ? -v : v;
    uint32_t min_half;
    uint32_t half;

    if (abs_v <= 0.0f) {
        return 0u;
    }
    /* 半周期下限：1s / (2 × max_rate) */
    min_half = 500000u / axis->config.max_step_rate_hz;
    if (min_half == 0u) {
        min_half = 1u;
    }
    half = (uint32_t)(500000.0f / abs_v);
    if (half < min_half) {
        half = min_half;
    }
    if (half == 0u) {
        half = 1u;
    }
    return half;
}

int bm_stepper_pulse_validate_config(const bm_stepper_pulse_config_t *config) {
    if (config == NULL || config->max_step_rate_hz == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_stepper_pulse_init(bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_stepper_pulse_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (axis->resources.step_high == NULL || axis->resources.step_low == NULL
        || axis->resources.dir_set == NULL || axis->resources.arm_timer == NULL) {
        return BM_ERR_INVALID;
    }
    bm_stepper_pulse_reset(axis);
    return BM_OK;
}

void bm_stepper_pulse_reset(bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.position         = 0;
    axis->state.velocity_sps     = 0.0f;
    axis->state.dir              = 1;
    axis->state.step_level       = 0u;
    axis->state.running          = 0u;
    axis->state.dir_wait_pending = 0u;
    if (axis->resources.step_low != NULL) {
        axis->resources.step_low(axis->resources.user);
    }
    if (axis->resources.arm_timer != NULL) {
        (void)axis->resources.arm_timer(axis->resources.user, 0u);
    }
}

void bm_stepper_pulse_set_velocity(bm_stepper_pulse_axis_t *axis,
                                   float velocity_sps) {
    int new_dir;

    if (axis == NULL) {
        return;
    }
    if (velocity_sps == 0.0f) {
        bm_stepper_pulse_stop(axis);
        return;
    }
    /* 钳制到频率上限 */
    if (velocity_sps > (float)axis->config.max_step_rate_hz) {
        velocity_sps = (float)axis->config.max_step_rate_hz;
    } else if (velocity_sps < -(float)axis->config.max_step_rate_hz) {
        velocity_sps = -(float)axis->config.max_step_rate_hz;
    }

    new_dir = (velocity_sps > 0.0f) ? 1 : -1;
    if (axis->state.running == 0u || new_dir != axis->state.dir) {
        /* 静止启动或方向翻转：先给 DIR 电平并留建立槽 */
        axis->resources.dir_set(axis->resources.user,
                                (new_dir > 0) ? 1 : 0);
        axis->state.dir_wait_pending = 1u;
        axis->state.dir = new_dir;
    }
    /*
     * 每次调速都发出到期上限请求（arm_timer 语义见头文件：平台仅在
     * 请求短于当前剩余时重设）——加速立即生效，且不打断当前半周期。
     */
    axis->state.velocity_sps = velocity_sps;
    axis->state.running      = 1u;
    (void)axis->resources.arm_timer(axis->resources.user,
                                    bm_stepper_pulse_half_period_us(axis));
}

void bm_stepper_pulse_stop(bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.velocity_sps     = 0.0f;
    axis->state.running          = 0u;
    axis->state.dir_wait_pending = 0u;
    if (axis->state.step_level != 0u) {
        axis->resources.step_low(axis->resources.user);
    }
    axis->state.step_level       = 0u;
    (void)axis->resources.arm_timer(axis->resources.user, 0u);
}

int32_t bm_stepper_pulse_position(const bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return 0;
    }
    return axis->state.position;
}

void bm_stepper_pulse_on_timer(bm_stepper_pulse_axis_t *axis) {
    uint32_t half;

    if (axis == NULL || axis->state.running == 0u) {
        return;
    }

    /* 方向建立槽：本拍不发脉冲，仅等待 DIR 建立 */
    if (axis->state.dir_wait_pending != 0u) {
        axis->state.dir_wait_pending = 0u;
        half = axis->config.dir_setup_us;
        if (half == 0u) {
            half = bm_stepper_pulse_half_period_us(axis);
        }
        (void)axis->resources.arm_timer(axis->resources.user, half);
        return;
    }

    if (axis->state.step_level == 0u) {
        axis->resources.step_high(axis->resources.user);
        axis->state.step_level = 1u;
        axis->state.position  += axis->state.dir;
    } else {
        axis->resources.step_low(axis->resources.user);
        axis->state.step_level = 0u;
    }
    (void)axis->resources.arm_timer(axis->resources.user,
                                    bm_stepper_pulse_half_period_us(axis));
}
