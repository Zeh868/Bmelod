/**
 * @file control_loop.c
 * @brief 串级 PI 控制环实现
 *
 * 外环 PI 输出作为内环设定，经饱和后驱动 plant 读回与执行器写入。
 * 提供 bm_exec_ops_t 标准封装（exec_init/exec_start/exec_safe_stop + ops 表），
 * 可直接接入调度框架；bm_control_loop_step() 直接调用路径保持不变。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 * 2026-06-23       0.2            zeh            补 bm_exec_ops_t 标准调度封装接口
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/control_loop.h"
#include "bm/common/bm_types.h"

int bm_control_loop_validate_config(const bm_control_loop_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_pi_validate_config(&config->outer_pi) != 0 ||
        bm_algo_pi_validate_config(&config->inner_pi) != 0) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_control_loop_reset(bm_control_loop_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_pi_reset(&axis->state.outer_pi, 0.0f);
    bm_algo_pi_reset(&axis->state.inner_pi, 0.0f);
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    axis->state.step_count = 0u;
}

void bm_control_loop_step(bm_control_loop_axis_t *axis) {
    const bm_control_loop_config_t *cfg;
    bm_control_loop_state_t *st;
    float outer_meas = 0.0f;
    float inner_meas = 0.0f;
    float setpoint = 0.0f;
    float outer_err;
    float inner_err;

    if (axis == NULL ||
        bm_control_loop_validate_config(&axis->config) != BM_OK) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_plant != NULL &&
        axis->resources.read_plant(axis->resources.read_plant_user,
                                   &outer_meas, &inner_meas,
                                   &setpoint) != 0) {
        st->step_count++;
        return;
    }

    outer_err = setpoint - outer_meas;
    st->outer_out = bm_algo_pi_step(&st->outer_pi, &cfg->outer_pi,
                                    outer_err, cfg->dt_s);

    inner_err = st->outer_out - inner_meas;
    st->inner_out = bm_algo_pi_step(&st->inner_pi, &cfg->inner_pi,
                                    inner_err, cfg->dt_s);

    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           st->inner_out);
    }
    st->step_count++;
}

/* ---------------------------------------------------------------------------
 * bm_exec_ops_t 标准封装
 * 语义与 power_control 组件保持一致：
 *   exec_init  → 校验配置 + 复位状态
 *   exec_start → 无额外操作（保留扩展点）
 *   exec_step  → 转发 bm_control_loop_step()（由调度槽 run 回调调用）
 *   exec_safe_stop → 输出归零 + 复位两级积分器
 * ---------------------------------------------------------------------------
 */

/**
 * @brief exec 封装：运行一步串级 PI
 *
 * 通过 instance->state 取得 bm_control_loop_axis_t 指针后调用
 * bm_control_loop_step()，行为与直接调用完全一致。
 *
 * @param instance exec 实例指针，instance->state 须为 bm_control_loop_axis_t*
 */
void bm_control_loop_exec_step(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_control_loop_step((bm_control_loop_axis_t *)instance->state);
    }
}

/**
 * @brief exec 生命周期：初始化
 *
 * 校验配置合法性，并将所有状态复位为零初值。
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_control_loop_exec_init(const bm_exec_t *instance) {
    bm_control_loop_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_control_loop_axis_t *)instance->state;
    if (bm_control_loop_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_control_loop_reset(axis);
    return BM_OK;
}

/**
 * @brief exec 生命周期：启动
 *
 * 当前无额外操作，保留供后续扩展（如使能执行器使能信号）。
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_control_loop_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec 生命周期：安全停机
 *
 * 将内外环输出归零，复位两级 PI 积分器，并通过 write_output 回调向
 * 执行器写入零值，确保停机时执行器处于安全状态。
 *
 * @param instance exec 实例指针
 */
void bm_control_loop_exec_safe_stop(const bm_exec_t *instance) {
    bm_control_loop_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_control_loop_axis_t *)instance->state;
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    bm_algo_pi_reset(&axis->state.outer_pi, 0.0f);
    bm_algo_pi_reset(&axis->state.inner_pi, 0.0f);
    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           0.0f);
    }
}

/**
 * @brief control_loop 标准 exec ops 表
 *
 * 将此指针赋给 bm_exec_t::ops，即可将 control_loop 实例
 * 接入调度框架的生命周期管理。
 */
const bm_exec_ops_t bm_control_loop_exec_ops = {
    bm_control_loop_exec_init,
    bm_control_loop_exec_start,
    bm_control_loop_exec_safe_stop
};
