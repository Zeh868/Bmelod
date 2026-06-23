/**
 * @file bm_algo_detection.h
 * @brief 检测算法：匹配滤波与同步检波
 *
 * 用于 DTMF、超声回波与仪器弱信号捕获。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_DETECTION_H
#define BM_ALGO_DETECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 匹配滤波 ---------- */

/**
 * @brief 滑动相关匹配滤波
 *
 * 计算 signal 与 template 的滑动点积，返回最大相关值及其位置。
 * 当 signal_len < template_len 或指针为空时返回 0.0f 并将 best_index 置 0。
 *
 * @param signal       输入信号数组，长度 signal_len
 * @param signal_len   信号长度（采样点数）
 * @param template     模板数组，长度 template_len
 * @param template_len 模板长度（采样点数），须 > 0
 * @param best_index   输出最大相关值对应的起始位置（可为 NULL）
 * @return 最大相关值；参数无效时返回 0.0f
 */
float bm_algo_matched_filter(const float *signal,
                             uint32_t signal_len,
                             const float *template,
                             uint32_t template_len,
                             uint32_t *best_index);

/* ---------- 同步检波 ---------- */

/**
 * @brief 同步检波状态
 */
typedef struct {
    float    i_accum; /**< 同相（I）分量一阶低通累积值 */
    float    q_accum; /**< 正交（Q）分量一阶低通累积值 */
    float    alpha;   /**< 低通平滑系数，范围 (0, 1]，默认 0.1 */
    uint32_t count;   /**< 已喂入的采样点计数 */
} bm_algo_sync_demod_state_t;

/**
 * @brief 复位同步检波状态
 *
 * 清零 I/Q 累积值和计数，alpha 复位为默认值 0.1。
 *
 * @param state 检波状态结构体，NULL 时静默返回
 */
void bm_algo_sync_demod_reset(bm_algo_sync_demod_state_t *state);

/**
 * @brief 喂入一个采样点更新同步检波状态
 *
 * 将 sample 分别与参考余弦（I 路）和参考正弦（Q 路）相乘，
 * 再以 alpha 系数做一阶低通平滑更新 i_accum / q_accum。
 *
 * @param state   检波状态结构体
 * @param sample  当前输入采样值
 * @param ref_sin 参考正弦值（与载波同频同相）
 * @param ref_cos 参考余弦值（与载波同频同相）
 */
void bm_algo_sync_demod_feed(bm_algo_sync_demod_state_t *state,
                             float sample,
                             float ref_sin,
                             float ref_cos);

/**
 * @brief 读取同步检波当前幅度（I² + Q² 的平方根）
 *
 * @param state 检波状态结构体（const）
 * @return 当前信号幅度估计值；state 为 NULL 时返回 0.0f
 */
float bm_algo_sync_demod_magnitude(const bm_algo_sync_demod_state_t *state);

/* ---------- 超声回波 ToF（包络峰检测） ---------- */

/**
 * @brief 超声回波飞行时间检测（包络阈值法）
 *
 * 从 min_delay 位置开始，对回波信号用一阶低通跟踪包络，
 * 当包络首次超过 threshold 时记录该索引作为飞行时间。
 *
 * @param echo           回波信号数组，长度 n
 * @param n              信号长度（采样点数）
 * @param min_delay      开始检测的最小延迟（跳过近场盲区）
 * @param threshold      包络检测阈值
 * @param envelope_alpha 包络低通系数，<=0 时使用默认值 0.1，>1 时截断至 1.0
 * @return 首次超阈值的采样索引；未检测到或参数无效时返回 -1
 */
int32_t bm_algo_ultrasonic_tof(const float *echo,
                               uint32_t n,
                               uint32_t min_delay,
                               float threshold,
                               float envelope_alpha);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_DETECTION_H */
