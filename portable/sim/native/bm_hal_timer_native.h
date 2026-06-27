/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_timer_native.h
 * @brief 原生仿真定时器测试辅助接口
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
 */

#ifndef BM_HAL_TIMER_NATIVE_H
#define BM_HAL_TIMER_NATIVE_H

#include <stdint.h>

/** 手动推进 delta 个 tick（当前 TLS 逻辑 CPU） */
void bm_hal_timer_native_advance_ticks(uint32_t delta);

/**
 * @brief 推进指定逻辑 CPU 的 tick 并在该 CPU TLS 下触发回调
 *
 * @param cpu   逻辑 CPU 编号
 * @param delta 推进节拍数
 */
void bm_hal_timer_native_advance_ticks_on_cpu(uint32_t cpu, uint32_t delta);

/** 跳跃 delta 个 tick 后仅触发一次回调（模拟 deadline 错过） */
void bm_hal_timer_native_jump_ticks(uint32_t delta);

/** 重置 tick 计数为 0 */
void bm_hal_timer_native_reset_ticks(void);

/** 测试辅助：复位定时器为未初始化状态（freq=0） */
void bm_hal_timer_native_deinit(void);

/** 测试辅助：设置后续 init 的返回值，BM_OK 恢复成功路径 */
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
