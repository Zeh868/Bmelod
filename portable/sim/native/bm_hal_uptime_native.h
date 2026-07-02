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
 * @version 1.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 2a）
 * 2026-07-02       1.1            zeh            新增纯虚拟时钟开关，消除微秒级精确断言的墙钟泄漏
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
 * 不改变虚拟时钟开关状态（开关由 bm_hal_uptime_native_set_virtual 显式控制）。
 */
void bm_hal_uptime_native_reset(void);

/**
 * @brief 测试辅助：切换 uptime 是否使用纯虚拟时钟
 *
 * enable 非 0 时，bm_hal_uptime_ns_raw() 只返回测试注入的偏移量
 * s_uptime_offset_ns（不叠加 QPC/clock_gettime 真实时钟分量），从而使
 * bm_hal_uptime_native_advance_us() 精确控制时间前进，消除 begin/end
 * 之间真实流逝的微秒级抖动，避免 µs 级精确断言 flaky。
 * enable 为 0 时恢复默认行为（真实时钟 + 偏移量）。
 *
 * @param enable 非 0 启用纯虚拟时钟；0 恢复真实时钟 + 偏移量
 */
void bm_hal_uptime_native_set_virtual(int enable);

#endif /* BM_HAL_UPTIME_NATIVE_H */
