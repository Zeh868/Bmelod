/**
 * @file bm_algo_signal_quality.h
 * @brief 信号质量：去抖、范围监控、变化率与冻结值检测
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            stable_count 自增前加饱和保护，防止 uint32_t 绕回导致误复位
 * 2026-08-01       1.1            Codex          补齐公共 API 中文 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_SIGNAL_QUALITY_H
#define BM_ALGO_SIGNAL_QUALITY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t stable_count_required;
    float    tolerance;
} bm_algo_debounce_analog_config_t;

typedef struct {
    float    candidate;
    uint32_t stable_count;
    float    latched;
    int      valid;
} bm_algo_debounce_analog_state_t;

/**
 * @brief 复位模拟量去抖器状态。
 * @param state 算法状态对象。
 * @param initial 复位后的初始稳定值。
 */
void bm_algo_debounce_analog_reset(bm_algo_debounce_analog_state_t *state,
                                   float initial);
/**
 * @brief 执行一次模拟量去抖器更新。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param sample 当前输入样本。
 * @return 输入达到稳定计数并锁存时返回 1，否则返回 0；参数无效时返回 0。
 */
int bm_algo_debounce_analog_step(bm_algo_debounce_analog_state_t *state,
                                 const bm_algo_debounce_analog_config_t *config,
                                 float sample);

typedef struct {
    float min_v;
    float max_v;
    float max_rate_per_s;
} bm_algo_range_monitor_config_t;

typedef struct {
    float prev;
    uint32_t fault_flags;
} bm_algo_range_monitor_state_t;

#define BM_ALGO_FAULT_UNDER_RANGE  (1u << 0)
#define BM_ALGO_FAULT_OVER_RANGE   (1u << 1)
#define BM_ALGO_FAULT_RATE         (1u << 2)
#define BM_ALGO_FAULT_FROZEN              (1u << 3)
#define BM_ALGO_FAULT_REDUNDANT_MISMATCH  (1u << 4)
#define BM_ALGO_FAULT_RANGE_NAN           (1u << 5)

/**
 * @brief 复位范围监测器状态。
 * @param state 算法状态对象。
 * @param v 复位后用于变化率比较的初始样本。
 */
void bm_algo_range_monitor_reset(bm_algo_range_monitor_state_t *state, float v);
/**
 * @brief 执行一次范围监测器更新。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param sample 当前输入样本。
 * @param dt_s 本次更新的时间间隔，单位 s。
 * @return 返回诊断故障位掩码；无故障或参数无效时返回 0。
 */
uint32_t bm_algo_range_monitor_step(bm_algo_range_monitor_state_t *state,
                                    const bm_algo_range_monitor_config_t *config,
                                    float sample,
                                    float dt_s);

/* ---------- 冗余通道一致性 ---------- */
typedef struct {
    float tolerance_abs;
    float tolerance_rel;
} bm_algo_redundant_pair_config_t;

/**
 * @brief 比较冗余测量对是否一致
 *
 * @param a 通道 A 测量值
 * @param b 通道 B 测量值
 * @param config 容差配置（可为 NULL，此时仅做精确比较）
 * @return 故障标志；一致时返回 0
 */
uint32_t bm_algo_redundant_pair_step(float a,
                                     float b,
                                     const bm_algo_redundant_pair_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_SIGNAL_QUALITY_H */
