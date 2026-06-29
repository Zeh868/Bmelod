/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uptime_native.h
 * @brief native_sim 单调时钟测试辅助接口
 *
 * 提供测试专用的 uptime 偏移量控制函数，供单元测试模拟时间流逝，
 * 无需依赖实际硬件定时器或 sleep。
 *
 * 典型用法：
 * @code
 * // setUp:
 * bm_hal_uptime_native_reset();
 *
 * // 模拟 1001 ms 过去：
 * bm_wdg_feed_module("mod");
 * bm_hal_uptime_native_advance_us(1001000u);
 * bm_wdg_feed();  // 应检测到超时
 * @endcode
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 2a）
 *
 */
#ifndef BM_HAL_UPTIME_NATIVE_H
#define BM_HAL_UPTIME_NATIVE_H

#include <stdint.h>

/**
 * @brief 测试辅助：将 bm_uptime_us() 偏移量推进 delta_us 微秒
 *
 * 累加偏移量（单调不减），下次 bm_uptime_us()/bm_uptime_ns() 返回值
 * 将增加对应时长。
 *
 * @param delta_us 推进量（微秒）
 */
void bm_hal_uptime_native_advance_us(uint64_t delta_us);

/**
 * @brief 测试辅助：重置 uptime 偏移量为 0
 *
 * 在每个测试用例的 setUp 中调用，防止前一用例的偏移影响后续用例。
 */
void bm_hal_uptime_native_reset(void);

#endif /* BM_HAL_UPTIME_NATIVE_H */
