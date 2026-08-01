/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_hrtimer_native.h
 * @brief native_sim 高精度 Timer 后端测试辅助接口
 * @maturity E1
 *
 * 仅供 native_sim 单元测试使用，用于推进虚拟时间并手动触发 Timer 回调。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim 高精度 Timer 测试辅助
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_HRTIMER_NATIVE_H
#define BM_HAL_HRTIMER_NATIVE_H

#include "hal/bm_hal_hrtimer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief native_sim 高精度 Timer 实例 0。 */
extern const bm_hal_hrtimer_t bm_native_hrtimer0;
/** @brief native_sim 高精度 Timer 实例 1。 */
extern const bm_hal_hrtimer_t bm_native_hrtimer1;

/**
 * @brief 重置所有 native_sim 高精度 Timer 状态（测试用）。
 */
void bm_hal_hrtimer_native_reset(void);

/**
 * @brief 将虚拟时间推进 delta_us 微秒（测试用）。
 *
 * 推进过程中会按周期/单次模式触发到期回调。
 *
 * @param delta_us 推进量（µs）
 */
void bm_hal_hrtimer_native_advance_us(uint64_t delta_us);

/**
 * @brief 手动触发指定 Timer 的到期检查（测试用）。
 *
 * 若当前虚拟时间已到达或超过到期时刻，则调用回调并按模式重装载。
 *
 * @param dev Timer 设备实例
 */
void bm_hal_hrtimer_native_fire(const bm_hal_hrtimer_t *dev);

/**
 * @brief 读取 native_sim 内部虚拟时间（µs，测试用）。
 *
 * @return 当前虚拟时间（µs）
 */
uint64_t bm_hal_hrtimer_native_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_HRTIMER_NATIVE_H */
