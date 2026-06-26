/**
 * @file bm_hal_uptime.h
 * @brief 全框架统一单调时钟后端接口
 *
 * 各 portable/sim/ 后端实现 `bm_hal_uptime_ns_raw()`；
 * 核心层 `bm_uptime.h` 在此之上提供 `bm_uptime_ns()` / `bm_uptime_us()`。
 *
 * 契约：
 * - 返回值为自系统启动（或后端初始化）起经过的纳秒数；
 * - 保证单调不减（同一 CPU 域内，连续两次调用第二次 >= 第一次）；
 * - 64 位无符号，约 584 年不回绕；
 * - 无需在调用前显式初始化（后端在首次读取时懒初始化）。
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
#ifndef BM_HAL_UPTIME_H
#define BM_HAL_UPTIME_H

#include <stdint.h>

/**
 * @brief 读取后端单调时钟原始纳秒值
 *
 * 由各 portable/sim/ 后端实现：
 * - ARM（qemu_cortexa_smp / qemu_aarch64_smp）：CNTPCT × (1e9 / CNTFRQ)
 * - native_sim（Windows）：QueryPerformanceCounter 换算
 * - native_sim（POSIX）：clock_gettime(CLOCK_MONOTONIC)
 *
 * @return 自系统启动起经过的纳秒数（uint64_t，单调不减）
 */
uint64_t bm_hal_uptime_ns_raw(void);

#endif /* BM_HAL_UPTIME_H */
