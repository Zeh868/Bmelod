/**
 * @file fault_derating.c
 * @brief 故障锁存与线性降额组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 * 2026-06-23       0.3            zeh            恢复计时比较加半个 dt 容差，消除浮点边界差一拍
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/component/fault_derating.h"
#include "bm/common/bm_types.h"

#include <string.h>

/**
 * @brief 校验故障降额配置合法性
 *
 * 检查规则：
 * - config 不可为 NULL
 * - dt_s > 0
 * - derate_ramp.rate_per_s > 0
 * - recovery_time_s >= 0
 * - derate_target 在 [0.0, 1.0] 区间内
 *
 * @param config 配置结构体指针（const），NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段不合法
 */
int bm_fault_derating_validate_config(const bm_fault_derating_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->derate_ramp.rate_per_s <= 0.0f ||
        config->recovery_time_s < 0.0f ||
        config->derate_target < 0.0f || config->derate_target > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 初始化故障降额轴
 *
 * 校验配置后调用 @ref bm_fault_derating_reset 将运行状态置为全额（1.0）。
 *
 * @param axis 故障降额轴实例指针，NULL 或配置非法时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法
 */
int bm_fault_derating_init(bm_fault_derating_axis_t *axis) {
    if (axis == NULL ||
        bm_fault_derating_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_fault_derating_reset(axis);
    return BM_OK;
}

/**
 * @brief 复位故障降额轴至全额状态
 *
 * 将 derate_factor 和 ramp 输出恢复到 1.0，清零锁存标志、
 * 恢复计时器与步计数，并清零遥测缓存（derate_factor 除外）。
 *
 * @param axis 故障降额轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_reset(bm_fault_derating_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_ramp_reset(&axis->state.derate_ramp, 1.0f);
    axis->state.fault_latched = 0;
    axis->state.derate_factor = 1.0f;
    axis->state.recovery_elapsed_s = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    axis->state.telemetry.derate_factor = 1.0f;
}

/**
 * @brief 锁存故障，触发降额斜坡
 *
 * 置位 fault_latched 标志并将恢复计时器清零。
 * 下次 @ref bm_fault_derating_step 开始沿斜坡降到 derate_target。
 *
 * @param axis 故障降额轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_latch(bm_fault_derating_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.fault_latched = 1;
    axis->state.recovery_elapsed_s = 0.0f;
}

/**
 * @brief 请求清除故障锁存，重新启动恢复计时器
 *
 * 清零 fault_latched 标志并将 recovery_elapsed_s 清零；
 * 实际 derate_factor 的恢复斜坡需等待 recovery_elapsed_s 累计达到
 * config.recovery_time_s 后才在 @ref bm_fault_derating_step 中启动。
 * axis 为 NULL 或当前未锁存时静默返回。
 *
 * @param axis 故障降额轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_clear_request(bm_fault_derating_axis_t *axis) {
    if (axis == NULL || !axis->state.fault_latched) {
        return;
    }
    axis->state.fault_latched = 0;
    axis->state.recovery_elapsed_s = 0.0f;
}

/**
 * @brief 故障降额单步更新
 *
 * 每个控制周期调用一次：
 * - 若 fault_latched：derate_factor 沿斜坡向 derate_target 逼近。
 * - 否则：recovery_elapsed_s 累计；达到 recovery_time_s 后 derate_factor
 *   沿斜坡向 1.0 恢复。
 * 更新遥测并（若已配置）调用 publish_telemetry 回调。
 *
 * @param axis 故障降额轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_step(bm_fault_derating_axis_t *axis) {
    const bm_fault_derating_config_t *cfg;
    bm_fault_derating_state_t *st;
    float target;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (st->fault_latched) {
        target = cfg->derate_target;
        st->derate_factor = bm_algo_ramp_step(&st->derate_ramp, &cfg->derate_ramp,
                                              target, cfg->dt_s);
    } else {
        st->recovery_elapsed_s += cfg->dt_s;
        /* 加半个 dt 容差，避免浮点累加误差导致恢复计时边界差一拍 */
        if (st->recovery_elapsed_s + 0.5f * cfg->dt_s >= cfg->recovery_time_s) {
            target = 1.0f;
            st->derate_factor = bm_algo_ramp_step(&st->derate_ramp, &cfg->derate_ramp,
                                                  target, cfg->dt_s);
        }
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_FAULT_DERATING_TEL_VALID;
    if (st->fault_latched) {
        st->telemetry.status |= BM_FAULT_DERATING_TEL_LATCHED;
    }
    st->telemetry.derate_factor = st->derate_factor;
    st->telemetry.recovery_elapsed_s = st->recovery_elapsed_s;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}
