/**
 * @file bm_algo_motion.h
 * @brief 运动辅助：编码器展开、速度估算与 DDA 插补
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            编码器 index/丢脉冲诊断
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_MOTION_H
#define BM_ALGO_MOTION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 编码器计数展开 ---------- */

/**
 * @brief 编码器配置参数
 */
typedef struct {
    uint32_t counts_per_rev; /**< 每转脉冲计数，须 >0 */
    int32_t  prev_count;     /**< 上一次原始计数（历史字段，兼容旧版） */
    int32_t  turns;          /**< 已累计圈数（历史字段，兼容旧版） */
    float    position_rad;   /**< 当前绝对位置（弧度，历史字段） */
} bm_algo_encoder_config_t;

/**
 * @brief 编码器运行状态
 */
typedef struct {
    int32_t  prev_count;      /**< 上次更新时的原始计数值 */
    int32_t  turns;           /**< 累计整圈圈数（带符号，支持溢出保护） */
    float    position_rad;    /**< 绝对位置（弧度） */
    float    velocity_rad_s;  /**< 估算角速度（弧度/秒） */
} bm_algo_encoder_state_t;

/**
 * @brief 重置编码器状态并以 raw_count 作为当前绝对原点
 *
 * @param state     编码器状态（不可为 NULL）
 * @param config    编码器配置（不可为 NULL）
 * @param raw_count 初始原始计数值
 */
void bm_algo_encoder_reset(bm_algo_encoder_state_t *state,
                           const bm_algo_encoder_config_t *config,
                           int32_t raw_count);

/**
 * @brief 更新编码器状态，解算绝对位置与速度
 *
 * 通过检测计数跳变是否超过半圈（counts_per_rev/2）来判断是否发生跨圈，
 * 并累计 turns 以保持绝对位置连续。
 *
 * @param state     编码器状态（不可为 NULL）
 * @param config    编码器配置（不可为 NULL，counts_per_rev 须 >0）
 * @param raw_count 本次读取的原始计数值
 * @param dt_s      自上次更新以来的时间间隔（秒，须 >0）
 * @return 更新后的绝对位置（弧度）；参数非法时返回 0.0
 */
float bm_algo_encoder_update(bm_algo_encoder_state_t *state,
                             const bm_algo_encoder_config_t *config,
                             int32_t raw_count,
                             float dt_s);

/* ---------- DDA 直线插补（二维） ---------- */

/**
 * @brief DDA 直线插补配置（起点 → 终点）
 */
typedef struct {
    float x0;        /**< 起点 X 坐标 */
    float y0;        /**< 起点 Y 坐标 */
    float x1;        /**< 终点 X 坐标 */
    float y1;        /**< 终点 Y 坐标 */
    float step_size; /**< 每步步长（须 >0 且有限） */
} bm_algo_dda_config_t;

/**
 * @brief DDA 直线插补运行状态（内部字段，勿直接修改）
 */
typedef struct {
    float    x;           /**< 当前位置 X */
    float    y;           /**< 当前位置 Y */
    float    err;         /**< 累积误差（保留，当前实现未使用） */
    int      done;        /**< 插补完成标志（1=完成） */
    int      step_x;      /**< X 方向符号（+1 或 -1） */
    int      step_y;      /**< Y 方向符号（+1 或 -1） */
    float    dx;          /**< 总 X 位移 */
    float    dy;          /**< 总 Y 位移 */
    float    target_x;    /**< 缓存终点 X（用于配置一致性校验） */
    float    target_y;    /**< 缓存终点 Y */
    float    step_size;   /**< 缓存步长 */
    uint32_t steps;       /**< 总步数 */
    uint32_t step_count;  /**< 已执行步数 */
} bm_algo_dda_state_t;

/**
 * @brief 重置 DDA 插补状态，根据配置预计算步数与增量
 *
 * @param state  DDA 状态（不可为 NULL）
 * @param config DDA 配置（不可为 NULL）
 */
void bm_algo_dda_reset(bm_algo_dda_state_t *state,
                       const bm_algo_dda_config_t *config);

/**
 * @brief 执行 DDA 单步，输出当前插补坐标
 *
 * @param state  DDA 状态（不可为 NULL，done 为 1 时立即返回 0）
 * @param config DDA 配置（须与 reset 时一致）
 * @param x_out  输出当前 X 坐标（可为 NULL）
 * @param y_out  输出当前 Y 坐标（可为 NULL）
 * @return 1 步进成功；0 已完成或参数非法
 */
int bm_algo_dda_step(bm_algo_dda_state_t *state,
                     const bm_algo_dda_config_t *config,
                     float *x_out,
                     float *y_out);

/* ---------- 步进脉冲生成（速度给定 → 步脉冲） ---------- */

/**
 * @brief 步进电机配置
 */
typedef struct {
    float max_velocity_steps_s; /**< 最大速度（步/秒），≤0 表示不限制 */
} bm_algo_stepper_config_t;

/**
 * @brief 步进电机脉冲生成状态
 */
typedef struct {
    float   phase;           /**< 当前相位累计值（[0,1) 之间时无输出脉冲） */
    int32_t position_steps;  /**< 绝对位置（步数） */
} bm_algo_stepper_state_t;

/**
 * @brief 重置步进电机状态并设定初始位置
 *
 * @param state    步进状态（不可为 NULL）
 * @param position 初始绝对位置（步数）
 */
void bm_algo_stepper_reset(bm_algo_stepper_state_t *state, int32_t position);

/**
 * @brief 根据速度给定和时间步长生成步进脉冲序列
 *
 * 使用相位累积法：每次调用将 |velocity| * dt_s 累加到 phase；
 * phase 每超过 1.0 发出一个 ±1 方向脉冲并更新 position_steps。
 *
 * @param state              步进状态（不可为 NULL）
 * @param config             步进配置（不可为 NULL）
 * @param velocity_steps_s   目标速度（步/秒，正向 +1，反向 -1）
 * @param dt_s               时间步长（秒，须 >0）
 * @param pulses             输出脉冲方向数组（±1），可为 NULL（仅计数）
 * @param max_pulses         pulses 缓冲容量（pulses 为 NULL 时忽略）
 * @return 本次产生的脉冲数
 */
uint32_t bm_algo_stepper_process(bm_algo_stepper_state_t *state,
                                 const bm_algo_stepper_config_t *config,
                                 float velocity_steps_s,
                                 float dt_s,
                                 int8_t *pulses,
                                 uint32_t max_pulses);

/* ---------- 编码器 index/丢脉冲诊断 ---------- */

/** 无故障 */
#define BM_ALGO_ENCODER_FAULT_NONE    0u
/** 计数跳变超过 max_delta_per_step，疑似丢脉冲 */
#define BM_ALGO_ENCODER_FAULT_MISSED  (1u << 0u)
/** 本周期检测到 index 脉冲 */
#define BM_ALGO_ENCODER_FAULT_INDEX   (1u << 1u)

/**
 * @brief 编码器诊断配置
 */
typedef struct {
    int32_t max_delta_per_step; /**< 单步最大允许计数跳变，≤0 则不检测跳变 */
} bm_algo_encoder_diag_config_t;

/**
 * @brief 编码器诊断状态
 */
typedef struct {
    int32_t prev_count; /**< 上次原始计数 */
} bm_algo_encoder_diag_state_t;

/**
 * @brief 重置编码器诊断状态
 *
 * @param state     诊断状态（不可为 NULL）
 * @param raw_count 当前原始计数（作为基准值）
 */
void bm_algo_encoder_diag_reset(bm_algo_encoder_diag_state_t *state,
                                int32_t raw_count);

/**
 * @brief 编码器诊断单步：检测计数跳变与 index 事件
 *
 * @param state 诊断状态
 * @param config 阈值配置（max_delta_per_step）
 * @param raw_count 当前原始计数
 * @param index_pulse_seen 本周期是否检测到 index 脉冲（非 0 为真）
 * @return fault_flags（BM_ALGO_ENCODER_FAULT_* 位或）
 */
uint32_t bm_algo_encoder_diag_step(bm_algo_encoder_diag_state_t *state,
                                   const bm_algo_encoder_diag_config_t *config,
                                   int32_t raw_count,
                                   int index_pulse_seen);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_MOTION_H */
