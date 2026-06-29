/**
 * @file bm_algo_power_quality.c
 * @brief 电能质量：THD 与 P/Q/S 计量实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 * 2026-06-23       1.1            zeh            bm_algo_harmonic_group_index 修复
 *                                                fundamental_bin * group_width uint32 乘法溢出：
 *                                                改用 uint64_t 中间量计算后再收窄
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_power_quality.h"
#include <stddef.h>
#include <stdint.h>

#include <math.h>

float bm_algo_thd_percent(const float *harmonics, uint32_t count) {
    float fundamental;
    float harm_sum_sq = 0.0f;
    uint32_t i;

    if (harmonics == NULL || count < 2u) {
        return 0.0f;
    }

    fundamental = harmonics[0];
    if (fundamental <= 1e-12f) {
        return 0.0f;
    }

    for (i = 1u; i < count; ++i) {
        harm_sum_sq += harmonics[i] * harmonics[i];
    }

    return sqrtf(harm_sum_sq) / fundamental * 100.0f;
}

void bm_algo_power_quality_pq(float v_rms,
                              float i_rms,
                              float phase_rad,
                              float *p_active,
                              float *q_reactive,
                              float *s_apparent) {
    float s;
    float cos_phi;
    float sin_phi;

    s = v_rms * i_rms;
    cos_phi = cosf(phase_rad);
    sin_phi = sinf(phase_rad);

    if (p_active != NULL) {
        *p_active = s * cos_phi;
    }
    if (q_reactive != NULL) {
        *q_reactive = s * sin_phi;
    }
    if (s_apparent != NULL) {
        *s_apparent = s;
    }
}

void bm_algo_energy_wh_reset(bm_algo_energy_wh_state_t *state) {
    if (state != NULL) {
        state->accumulated_wh = 0.0f;
    }
}

float bm_algo_energy_wh_integrator_step(bm_algo_energy_wh_state_t *state,
                                        float p_watts,
                                        float dt_s) {
    if (state == NULL || dt_s <= 0.0f) {
        return (state != NULL) ? state->accumulated_wh : 0.0f;
    }

    state->accumulated_wh += p_watts * dt_s / 3600.0f;
    return state->accumulated_wh;
}

/**
 * @brief FFT bin 索引转谐波组号
 *
 * @param bin           当前 FFT bin 索引（0 返回 0）
 * @param fundamental_bin 基波 bin（0 返回 0）
 * @param group_width   每组谐波阶次宽度（0 返回 0，>=1 有效）
 * @return 谐波组号
 *
 * @note fundamental_bin 与 group_width 均为 uint32_t，直接相乘在极端参数下
 *       （如两者均接近 UINT32_MAX 的平方根约 65535）会溢出 uint32_t。
 *       本实现使用 uint64_t 中间量完成乘法后再收窄，消除溢出风险。
 */
uint32_t bm_algo_harmonic_group_index(uint32_t bin,
                                      uint32_t fundamental_bin,
                                      uint32_t group_width) {
    uint64_t stride;  /* fundamental_bin * group_width，用 64 位防 uint32 乘法溢出 */
    uint64_t result;

    if (bin == 0u || fundamental_bin == 0u || group_width == 0u) {
        return 0u;
    }

    stride = (uint64_t)fundamental_bin * (uint64_t)group_width;
    result = ((uint64_t)bin + stride / 2u) / stride;

    /* 正常参数下组号远小于 UINT32_MAX，安全收窄 */
    return (uint32_t)result;
}
