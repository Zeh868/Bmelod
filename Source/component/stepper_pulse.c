/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse.c
 * @brief STEP/DIR 脉冲步进驱动组件实现（芯片无关，resources 回调驱动）
 *
 * 定时模型：arm_timer 为一次性定时；on_timer 每次到期消费一个半周期
 * （STEP 翻转一次），上升沿 position ±1，随后按当前速度的半周期（及
 * min_high_us/min_low_us 约束）重新武装。方向切换：运行中反向先将 STEP
 * 拉低，可选 dir_hold_us 后再 dir_set 并武装 dir_setup_us（不再置
 * dir_wait_pending）；静止启动立即 dir_set 并置 dir_wait_pending。
 * GPIO 回调非 BM_OK 时锁存 fault、停机并尽量 STEP 拉低。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            stop 时 STEP 已为低则跳过 step_low
 * 2026-07-28       1.2            zeh            dir_hold/min 脉宽/GPIO fault/en_set
 * 2026-07-28       1.3            zeh            dir_hold 后不再置 dir_wait_pending
 * 2026-07-28       1.4            zeh            半周期 float→uint32 越界 UB 修复
 * 2026-08-01       1.4            zeh           补全 Doxygen 合规注释
 *                                                （isfinite+float 域钳位）；validate
 *                                                加法溢出改逐项比较；fault 态允许
 *                                                set_enable(axis,0) 断使能
 *
 */
#include "bm/component/stepper_pulse.h"

#include <math.h>
#include <stddef.h>

/**
 * @brief GPIO/定时器故障：停机、清等待槽、尽量 STEP 拉低并取消定时器。
 */
static void bm_stepper_pulse_latch_gpio_fault(bm_stepper_pulse_axis_t *axis)
{
    axis->state.fault             = 1u;
    axis->state.running           = 0u;
    axis->state.velocity_sps      = 0.0f;
    axis->state.dir_wait_pending  = 0u;
    axis->state.dir_hold_pending  = 0u;
    if (axis->state.step_level != 0u && axis->resources.step_low != NULL) {
        (void)axis->resources.step_low(axis->resources.user);
        axis->state.step_level = 0u;
    }
    if (axis->resources.arm_timer != NULL) {
        (void)axis->resources.arm_timer(axis->resources.user, 0u);
    }
}

/**
 * @brief 由当前速度计算半周期（µs），下限由 max_step_rate_hz 与 min 脉宽钳制。
 */
static uint32_t bm_stepper_pulse_half_period_us(const bm_stepper_pulse_axis_t *axis)
{
    float    v = axis->state.velocity_sps;
    float    abs_v = (v < 0.0f) ? -v : v;
    float    f_half;
    uint32_t min_half;
    uint32_t half;

    if (!isfinite(abs_v) || abs_v <= 0.0f) {
        return 0u;
    }
    /* 半周期下限：1s / (2 × max_rate) */
    min_half = 500000u / axis->config.max_step_rate_hz;
    if (min_half == 0u) {
        min_half = 1u;
    }
    if (axis->config.min_high_us > 0u && axis->config.min_high_us > min_half) {
        min_half = axis->config.min_high_us;
    }
    if (axis->config.min_low_us > 0u && axis->config.min_low_us > min_half) {
        min_half = axis->config.min_low_us;
    }
    /* float→uint32 超出表示域是 UB：先在 float 域钳位到 UINT32_MAX */
    f_half = 500000.0f / abs_v;
    half = (f_half >= (float)UINT32_MAX) ? UINT32_MAX : (uint32_t)f_half;
    if (half < min_half) {
        half = min_half;
    }
    if (half == 0u) {
        half = 1u;
    }
    return half;
}

/**
 * @brief STEP 拉高后下一次定时器间隔（µs）。
 */
static uint32_t bm_stepper_pulse_interval_after_high(
    const bm_stepper_pulse_axis_t *axis)
{
    uint32_t half = bm_stepper_pulse_half_period_us(axis);

    if (axis->config.min_high_us > 0u && axis->config.min_high_us > half) {
        return axis->config.min_high_us;
    }
    return half;
}

/**
 * @brief STEP 拉低后下一次定时器间隔（µs）。
 */
static uint32_t bm_stepper_pulse_interval_after_low(
    const bm_stepper_pulse_axis_t *axis)
{
    uint32_t half = bm_stepper_pulse_half_period_us(axis);

    if (axis->config.min_low_us > 0u && axis->config.min_low_us > half) {
        return axis->config.min_low_us;
    }
    return half;
}

int bm_stepper_pulse_validate_config(const bm_stepper_pulse_config_t *config) {
    uint32_t max_period;

    if (config == NULL || config->max_step_rate_hz == 0u) {
        return BM_ERR_INVALID;
    }
    /*
     * min_high_us + min_low_us 直接相加可溢出 uint32 绕过校验，
     * 改用逐项比较（|| 短路保证 max_period - min_low_us 不下溢）。
     */
    max_period = 1000000u / config->max_step_rate_hz;
    if (config->min_high_us > max_period ||
        config->min_low_us > max_period ||
        config->min_high_us > max_period - config->min_low_us) {
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
    axis->state.position          = 0;
    axis->state.velocity_sps      = 0.0f;
    axis->state.dir               = 1;
    axis->state.step_level        = 0u;
    axis->state.running           = 0u;
    axis->state.dir_wait_pending  = 0u;
    axis->state.dir_hold_pending  = 0u;
    axis->state.fault             = 0u;
    if (axis->resources.step_low != NULL) {
        (void)axis->resources.step_low(axis->resources.user);
    }
    if (axis->resources.arm_timer != NULL) {
        (void)axis->resources.arm_timer(axis->resources.user, 0u);
    }
}

void bm_stepper_pulse_clear_fault(bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.fault = 0u;
}

void bm_stepper_pulse_set_velocity(bm_stepper_pulse_axis_t *axis,
                                   float velocity_sps) {
    int      new_dir;
    int      old_dir;
    int      was_running;
    uint32_t interval;
    int      rc;

    if (axis == NULL) {
        return;
    }
    if (axis->state.fault != 0u) {
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

    new_dir     = (velocity_sps > 0.0f) ? 1 : -1;
    old_dir     = axis->state.dir;
    was_running = (axis->state.running != 0u) ? 1 : 0;

    axis->state.velocity_sps = velocity_sps;
    axis->state.running    = 1u;

    if (was_running == 0) {
        /* 静止启动：立即 dir_set 并留建立槽 */
        rc = axis->resources.dir_set(axis->resources.user,
                                     (new_dir > 0) ? 1 : 0);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
            return;
        }
        axis->state.dir              = new_dir;
        axis->state.dir_wait_pending = 1u;
        interval = bm_stepper_pulse_half_period_us(axis);
    } else if (new_dir != old_dir) {
        /* 运行中方向翻转：先拉低 STEP，再 hold 或立即改 DIR */
        if (axis->state.step_level != 0u) {
            rc = axis->resources.step_low(axis->resources.user);
            if (rc != BM_OK) {
                bm_stepper_pulse_latch_gpio_fault(axis);
                return;
            }
            axis->state.step_level = 0u;
        }
        axis->state.dir              = new_dir;
        axis->state.dir_wait_pending = 0u;
        axis->state.dir_hold_pending = 0u;
        if (axis->config.dir_hold_us > 0u) {
            axis->state.dir_hold_pending = 1u;
            interval = axis->config.dir_hold_us;
        } else {
            rc = axis->resources.dir_set(axis->resources.user,
                                         (new_dir > 0) ? 1 : 0);
            if (rc != BM_OK) {
                bm_stepper_pulse_latch_gpio_fault(axis);
                return;
            }
            axis->state.dir_wait_pending = 1u;
            interval = bm_stepper_pulse_half_period_us(axis);
        }
    } else {
        /*
         * 同向调速：发出到期上限请求（arm_timer 语义见头文件），
         * 加速立即生效且不打断当前半周期。
         */
        interval = bm_stepper_pulse_half_period_us(axis);
    }

    rc = axis->resources.arm_timer(axis->resources.user, interval);
    if (rc != BM_OK) {
        bm_stepper_pulse_latch_gpio_fault(axis);
    }
}

void bm_stepper_pulse_stop(bm_stepper_pulse_axis_t *axis) {
    int rc;

    if (axis == NULL) {
        return;
    }
    axis->state.velocity_sps     = 0.0f;
    axis->state.running          = 0u;
    axis->state.dir_wait_pending = 0u;
    axis->state.dir_hold_pending = 0u;
    if (axis->state.step_level != 0u) {
        rc = axis->resources.step_low(axis->resources.user);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
            return;
        }
        axis->state.step_level = 0u;
    }
    rc = axis->resources.arm_timer(axis->resources.user, 0u);
    if (rc != BM_OK) {
        bm_stepper_pulse_latch_gpio_fault(axis);
    }
}

int bm_stepper_pulse_set_enable(bm_stepper_pulse_axis_t *axis, int enable) {
    int rc;

    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (axis->resources.en_set == NULL) {
        return BM_ERR_NOT_SUPPORTED;
    }
    /* fault 锁存后仍允许断使能（enable==0），禁止重新使能 */
    if (axis->state.fault != 0u && enable != 0) {
        return BM_ERR_IO;
    }
    rc = axis->resources.en_set(axis->resources.user, (enable != 0) ? 1 : 0);
    if (rc != BM_OK) {
        bm_stepper_pulse_latch_gpio_fault(axis);
    }
    return rc;
}

int32_t bm_stepper_pulse_position(const bm_stepper_pulse_axis_t *axis) {
    if (axis == NULL) {
        return 0;
    }
    return axis->state.position;
}

void bm_stepper_pulse_on_timer(bm_stepper_pulse_axis_t *axis) {
    uint32_t interval;
    int      rc;

    if (axis == NULL || axis->state.running == 0u || axis->state.fault != 0u) {
        return;
    }

    /* dir_hold 槽：保持 STEP 低、不改 DIR，等待上一次上升沿后的保持时间 */
    if (axis->state.dir_hold_pending != 0u) {
        axis->state.dir_hold_pending = 0u;
        rc = axis->resources.dir_set(axis->resources.user,
                                     (axis->state.dir > 0) ? 1 : 0);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
            return;
        }
        interval = axis->config.dir_setup_us;
        if (interval == 0u) {
            interval = bm_stepper_pulse_half_period_us(axis);
        }
        rc = axis->resources.arm_timer(axis->resources.user, interval);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
        }
        return;
    }

    /* 方向建立槽：本拍不发脉冲，仅等待 DIR 建立 */
    if (axis->state.dir_wait_pending != 0u) {
        axis->state.dir_wait_pending = 0u;
        interval = axis->config.dir_setup_us;
        if (interval == 0u) {
            interval = bm_stepper_pulse_half_period_us(axis);
        }
        rc = axis->resources.arm_timer(axis->resources.user, interval);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
        }
        return;
    }

    if (axis->state.step_level == 0u) {
        rc = axis->resources.step_high(axis->resources.user);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
            return;
        }
        axis->state.step_level = 1u;
        axis->state.position  += axis->state.dir;
        interval = bm_stepper_pulse_interval_after_high(axis);
    } else {
        rc = axis->resources.step_low(axis->resources.user);
        if (rc != BM_OK) {
            bm_stepper_pulse_latch_gpio_fault(axis);
            return;
        }
        axis->state.step_level = 0u;
        interval = bm_stepper_pulse_interval_after_low(axis);
    }
    rc = axis->resources.arm_timer(axis->resources.user, interval);
    if (rc != BM_OK) {
        bm_stepper_pulse_latch_gpio_fault(axis);
    }
}
