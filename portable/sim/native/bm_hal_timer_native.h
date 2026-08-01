/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_timer_native.h
 * @brief 原生仿真定时器测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            按 CPU 查询 tick
 * 2026-06-14       1.2            zeh            advance_ticks_on_cpu
 *
 * 2026-08-01       1.2            Codex            补全中文 Doxygen 合规注释
 */

#ifndef BM_HAL_TIMER_NATIVE_H
#define BM_HAL_TIMER_NATIVE_H

#include <stdint.h>

/**
 * @brief 按 tick 逐步推进当前逻辑 CPU 的仿真定时器。
 * @param delta 待推进的 tick 数。
 */
void bm_hal_timer_native_advance_ticks(uint32_t delta);

/**
 * @brief 推进指定逻辑 CPU 的 tick 并在该 CPU TLS 下触发回调
 *
 * @param cpu   逻辑 CPU 编号
 * @param delta 推进节拍数
 */
void bm_hal_timer_native_advance_ticks_on_cpu(uint32_t cpu, uint32_t delta);

/**
 * @brief 跳跃推进当前逻辑 CPU 的 tick 并仅触发一次回调。
 * @param delta 待推进的 tick 数。
 */
void bm_hal_timer_native_jump_ticks(uint32_t delta);

/**
 * @brief 将当前逻辑 CPU 的仿真 tick 计数清零。
 */
void bm_hal_timer_native_reset_ticks(void);

/**
 * @brief 将仿真定时器恢复为未初始化状态。
 */
void bm_hal_timer_native_deinit(void);

/**
 * @brief 设置后续仿真定时器初始化的注入返回码。
 * @param result 后续定时器初始化应返回的状态码。
 */
void bm_hal_timer_native_set_init_result(int result);

/**
 * @brief 读取指定逻辑 CPU 的 tick（监督/测试路径）
 *
 * @param cpu 逻辑 CPU 编号
 * @return 该核 tick 计数值
 */
uint32_t bm_hal_timer_native_ticks_on_cpu(uint32_t cpu);

/**
 * @brief 读取指定逻辑 CPU 的计数频率（Hz）
 *
 * @param cpu 逻辑 CPU 编号
 * @return 计数频率（Hz）
 */
uint32_t bm_hal_timer_native_freq_on_cpu(uint32_t cpu);

#endif /* BM_HAL_TIMER_NATIVE_H */
