/**
 * @file bm_algo_compensation.h
 * @brief 执行器非线性补偿：死区逆映射与摩擦前馈
 *
 * 纯数学核，供液压、门机与机械臂控制环前馈使用。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 * 2026-06-17       1.1            zeh            背隙逆补偿
 * 2026-06-23       1.2            zeh            bm_algo_backlash_inverse 换向时重置偏移，修复只增不减缺陷
 * 2026-06-23       1.3            zeh            背隙补偿升级为双向独立偏移：正向/反向各自维护累计偏移，换向时切换至另一方向已保存偏移继续渐进
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
/**
 * @brief 背隙补偿状态（双向独立偏移）
 *
 * 维护两个方向各自的累计已补量：
 * - @c offset_fwd：正向（command > 0，A→B）累计偏移，范围 [0, width]
 * - @c offset_rev：反向（command < 0，B→A）累计偏移，范围 [0, width]
 * - @c last_direction：上次有效方向（1 正向，-1 反向，0 初始未知）
 *
 * 换向时不清零，而是切换到另一方向已保存的偏移继续渐进，
 * 从而保留各方向的补偿历史，避免每次换向重新从零累积的过补偿振荡。
 */
typedef struct {
    int   last_direction; /**< 上次有效运动方向（1/-1/0） */
    float offset_fwd;     /**< 正向累计补偿量，范围 [0, width] */
    float offset_rev;     /**< 反向累计补偿量，范围 [0, width] */
} bm_algo_backlash_state_t;

void bm_algo_backlash_reset(bm_algo_backlash_state_t *state);

/**
 * @brief 背隙逆补偿：双向独立偏移，换向时切换至另一方向已保存偏移继续渐进
 *
 * 策略：
 * - 每次调用将当前方向对应的偏移（offset_fwd 或 offset_rev）向 width 渐进，
 *   每步最多增加 slope。
 * - 检测到换向时，不清零，而是直接切换到另一方向已保存的偏移继续渐进。
 * - 首次调用（last_direction == 0）视为无换向，直接按当前方向渐进。
 * - command == 0 时方向保持上次不变（不更新 last_direction）。
 *
 * @param command 原始指令
 * @param state   背隙状态（含正/反向独立偏移与上次方向）
 * @param width   总背隙宽度（同 command 单位，须 > 0）
 * @param slope   每步最大补偿增量（须 > 0）
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
