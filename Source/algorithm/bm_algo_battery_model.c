/**
 * @file bm_algo_battery_model.c
 * @brief 电池等效模型实现
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.2            Codex           补齐 Doxygen 合规元数据
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       1.0            zeh            补齐 Doxygen 注释，版本与头文件对齐
 * 2026-06-23       1.1            zeh            电压更新增加协方差对角元正定性兜底
 * 2026-07-09       1.2            zeh            H9：soc_ekf_predict/
 *                                                update_voltage 补 NaN/Inf
 *                                                输入护栏，避免一次毛刺永久
 *                                                污染 soc/bias_a/协方差状态
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_battery_model.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

/**
 * @brief 复位 SOC-EKF 状态，设置初始 SOC 并清零偏置与协方差
 *
 * 初始协方差固定为 P = diag(0.01, 0.01)，交叉项清零。
 *
 * @param state    EKF 状态指针，为 NULL 时静默返回
 * @param soc_init 初始 SOC（自动钳位到 [0.0, 1.0]）
 */
void bm_algo_soc_ekf_reset(bm_algo_soc_ekf_state_t *state, float soc_init) {
    if (state == NULL) {
        return;
    }
    state->soc = bm_algo_is_finite_f(soc_init)
                     ? bm_algo_clamp_f(soc_init, 0.0f, 1.0f)
                     : 0.0f;
    state->bias_a = 0.0f;
    state->p00 = 0.01f;
    state->p01 = 0.0f;
    state->p10 = 0.0f;
    state->p11 = 0.01f;
}

/**
 * @brief SOC-EKF 预测步：库仑积分 + 线性化协方差传播
 *
 * 状态方程（离散化）：
 *   dsoc = η * (I - bias_a) * dt / (C_nom * 3600)
 *   soc(k+1)  = clamp(soc(k) + dsoc, 0, 1)
 *   bias(k+1) = bias_a（随机游走，仅由 q_bias 驱动）
 *
 * 协方差传播（F 为状态转移矩阵雅可比，bias_gain = η*dt/(C_nom*3600)）：
 *   P = F*P*F^T + Q*dt，Q = diag(q_soc, q_bias)
 *
 * @param state     EKF 状态指针，为 NULL 时静默返回
 * @param config    EKF 配置指针，为 NULL 时静默返回
 * @param current_a 当前电流（A，充电为正）
 * @param dt_s      时间步长（秒），须 > 0
 */
void bm_algo_soc_ekf_predict(bm_algo_soc_ekf_state_t *state,
                             const bm_algo_soc_ekf_config_t *config,
                             float current_a,
                             float dt_s) {
    float dsoc;
    float bias_gain;
    float p00;
    float p01;
    float p10;
    float p11;

    /* H9：current_a/dt_s 非有限（NaN/Inf）会经 dsoc 直接污染 soc，
     * 而 bm_algo_clamp_f 对 NaN 不钳位，一次毛刺即可永久损坏持久状态；
     * 入口拒绝更新，保持旧状态不变。 */
    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        !bm_algo_is_finite_f(dt_s) || !bm_algo_is_finite_f(current_a) ||
        !bm_algo_is_finite_f(config->nominal_capacity_ah) ||
        config->nominal_capacity_ah <= 0.0f) {
        return;
    }

    bias_gain = config->coulomb_efficiency * dt_s /
                (config->nominal_capacity_ah * 3600.0f);
    dsoc = (current_a - state->bias_a) * bias_gain;
    state->soc = bm_algo_clamp_f(state->soc + dsoc, 0.0f, 1.0f);

    /* 保存旧协方差，计算 F*P*F^T + Q*dt */
    p00 = state->p00;
    p01 = state->p01;
    p10 = state->p10;
    p11 = state->p11;

    /* Batch-3：q_soc 与 q_bias 同享护栏；非有限过程噪声回退到 0，
     * 避免一次非法配置永久污染协方差。
     * 同时校验 p00 计算结果有限：极小容量可使 bias_gain²*p11 溢出为 Inf，
     * 进而在 update_voltage 中造成 Inf/Inf=NaN；溢出时保持旧 p00。 */
    {
        float q_soc_safe = bm_algo_is_finite_f(config->q_soc)
                               ? config->q_soc : 0.0f;
        float q_bias_safe = bm_algo_is_finite_f(config->q_bias)
                                ? config->q_bias : 0.0f;

        state->p00 = p00 - bias_gain * (p01 + p10)
                     + bias_gain * bias_gain * p11
                     + q_soc_safe * dt_s;
        if (!bm_algo_is_finite_f(state->p00)) {
            state->p00 = p00;
        }
        state->p01 = p01 - bias_gain * p11;
        state->p10 = p10 - bias_gain * p11;
        state->p11 = p11 + q_bias_safe * dt_s;
    }
    if (state->p11 < 1e-12f) {
        state->p11 = 1e-12f;
    }
}

/**
 * @brief SOC-EKF 电压更新步：卡尔曼增益修正 SOC 与电流偏置
 *
 * 观测方程：h(x) = OCV(soc)，雅可比 H = [ocv_slope, 0]。
 * 新息：y = terminal_v - ocv_from_soc
 * 新息协方差：S = H*P*H^T + R_v = p00 * slope^2 + r_v
 * 卡尔曼增益：K = P*H^T / S，即 k0 = p00*slope/S，k1 = p10*slope/S
 * 状态更新：soc += k0*y；bias_a += k1*y
 * 协方差更新：P = (I - K*H)*P（先更新，再做对称化 P01=(P01+P10)/2）
 *
 * @param state         EKF 状态指针，为 NULL 时静默返回
 * @param config        EKF 配置指针，为 NULL 时静默返回
 * @param terminal_v    实测端电压（V）
 * @param ocv_from_soc  由当前 SOC 估计查表得到的开路电压（V）
 */
void bm_algo_soc_ekf_update_voltage(bm_algo_soc_ekf_state_t *state,
                                    const bm_algo_soc_ekf_config_t *config,
                                    float terminal_v,
                                    float ocv_from_soc) {
    float y;
    float s;
    float k0;
    float k1;
    float p00;
    float p01;
    float p10;
    float p11;
    float slope;

    if (state == NULL || config == NULL) {
        return;
    }
    /* H9：terminal_v/ocv_from_soc 非有限会经 y/k0/k1 污染 soc/bias_a
     * 持久状态，入口拒绝更新，保持旧状态不变。 */
    if (!bm_algo_is_finite_f(terminal_v) || !bm_algo_is_finite_f(ocv_from_soc)) {
        return;
    }

    slope = config->ocv_slope_v_per_soc;
    if (slope <= 0.0f) {
        slope = 0.5f;  /* 默认斜率保护，防止零增益 */
    }
    y = terminal_v - ocv_from_soc;
    s = state->p00 * slope * slope + config->r_v;
    if (s <= 1e-9f) {
        return;  /* 新息协方差过小，跳过更新防止数值溢出 */
    }

    k0 = state->p00 * slope / s;
    k1 = state->p10 * slope / s;

    /* 保存旧协方差用于更新 */
    p00 = state->p00;
    p01 = state->p01;
    p10 = state->p10;
    p11 = state->p11;

    state->soc += k0 * y;
    state->bias_a += k1 * y;
    state->soc = bm_algo_clamp_f(state->soc, 0.0f, 1.0f);

    /* 协方差更新：P = (I - K*H)*P */
    state->p00 = p00 - k0 * slope * p00;
    state->p01 = p01 - k0 * slope * p01;
    state->p10 = p10 - k1 * slope * p00;
    state->p11 = p11 - k1 * slope * p01;
    /* 对称化处理，保持协方差矩阵数值对称 */
    state->p01 = 0.5f * (state->p01 + state->p10);
    state->p10 = state->p01;
    /* 对角元正定性兜底：理论上 (I-KH)P 在正定初值下保持非负，
     * 此处仅防浮点极端边缘出现微小负值，避免后续 S 计算异常。 */
    if (state->p00 < 0.0f) {
        state->p00 = 0.0f;
    }
    if (state->p11 < 0.0f) {
        state->p11 = 0.0f;
    }
}
