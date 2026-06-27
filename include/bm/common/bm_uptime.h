/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_uptime.h
 * @brief 全框架统一单调时钟公共 API
 *
 * 提供自系统启动起经过的纳秒/微秒值，底层由 `bm_hal_uptime_ns_raw()` 后端支撑。
 *
 * 特性：
 * - 64 位无符号纳秒计数，约 584 年不回绕；
 * - 单调不减（同一时钟域，连续两次读取第二次 >= 第一次）；
 * - 无需显式初始化，首次调用由后端懒初始化；
 * - `bm_uptime_us()` 为便捷换算，精度 >= 1 µs。
 *
 * 用法：
 * @code
 * uint64_t t0 = bm_uptime_ns();
 * // ... do work ...
 * uint64_t elapsed_us = (bm_uptime_ns() - t0) / 1000u;
 * @endcode
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 1a）
 *
 */
#ifndef BM_UPTIME_H
#define BM_UPTIME_H

#include <stdint.h>

/**
 * @brief 读取单调时钟纳秒值
 *
 * 返回自系统启动（或 backend 初始化）起经过的纳秒数。
 * 连续两次调用保证第二次返回值 >= 第一次。
 *
 * @return 经过纳秒数（uint64_t，单调不减，~584 年不回绕）
 */
uint64_t bm_uptime_ns(void);

/**
 * @brief 读取单调时钟微秒值
 *
 * 等价于 `bm_uptime_ns() / 1000`，精度 >= 1 µs。
 * 连续两次调用保证第二次返回值 >= 第一次。
 *
 * @return 经过微秒数（uint64_t，单调不减）
 */
uint64_t bm_uptime_us(void);

#endif /* BM_UPTIME_H */
