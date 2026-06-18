/**
 * @file bm_algo_compensation.h
 * @brief 执行器非线性补偿：死区逆映射与摩擦前馈
 *
 * 纯数学核，供液压、门机与机械臂控制环前馈使用。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 * 2026-06-17       1.1            zeh            背隙逆补偿
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_COMPENSATION_H
#define BM_ALGO_COMPENSATION_H

#ifdef __cplusplus
extern "C" {
#endif

/** 死区逆映射：将死区内指令置零，区外按 gain 放大 */
float bm_algo_deadzone_inverse(float command, float deadband, float gain);

/** 库仑 + 粘性摩擦补偿量（叠加到控制输出） */
float bm_algo_friction_comp(float velocity,
                            float coulomb,
                            float viscous,
                            float v_deadband);

/* ---------- 扰动观测器（DOB，E1 简化） ---------- */
typedef struct {
    float plant_gain;
    float lpf_alpha;
} bm_algo_dob_config_t;

typedef struct {
    float y_hat;
    float disturbance;
} bm_algo_dob_state_t;

void bm_algo_dob_reset(bm_algo_dob_state_t *state);

/**
 * @brief DOB 单步：由 u/y 估计扰动
 *
 * @param state 观测器状态
 * @param config 模型增益与低通系数
 * @param u 控制输入
 * @param y 被控输出测量
 * @param disturbance_out 可选扰动估计输出（可为 NULL）
 * @return 扰动估计
 */
float bm_algo_dob_step(bm_algo_dob_state_t *state,
                       const bm_algo_dob_config_t *config,
                       float u,
                       float y,
                       float *disturbance_out);

/* ---------- 背隙逆补偿（E1 简化） ---------- */
typedef struct {
    int   last_direction;
    float backlash_offset;
} bm_algo_backlash_state_t;

void bm_algo_backlash_reset(bm_algo_backlash_state_t *state);

/**
 * @brief 背隙逆补偿：换向时按 slope 渐补间隙
 *
 * @param command 原始指令
 * @param state 背隙状态（方向与已补量）
 * @param width 总背隙宽度（同 command 单位）
 * @param slope 每步最大补偿量（>0）
 * @return 补偿后指令
 */
float bm_algo_backlash_inverse(float command,
                               bm_algo_backlash_state_t *state,
                               float width,
                               float slope);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_COMPENSATION_H */
