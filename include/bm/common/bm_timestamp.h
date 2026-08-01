/**
 * @file bm_timestamp.h
 * @brief 全框架单调时钟域时间戳（采样/块流用）—— common 公共层定义
 *
 * 自 bm/hybrid/bm_timestamp.h 下沉（路线图 #9 时间基统一 1b）。
 * bm/hybrid/bm_timestamp.h 已改为转发头，现有 27 个引用文件无需改动仍可编译。
 *
 * 本次升级要点：
 * - `ticks` 由 `uint32_t` 升至 `uint64_t`，消除 ~49 天（@1MHz）回绕；
 * - `bm_timestamp_before()`/`bm_timestamp_after()` 对 64 位单调时钟做全宽比较；
 * - 新增 `bm_timestamp_before_32()`/`bm_timestamp_after_32()` 供 32 位 HRT 计数器
 *   做半量程回绕安全比较；
 * - 新增 `bm_timestamp_from_uptime()`，供 1c 路径用 `bm_uptime_ns()` 构造时间戳
 *   （ns 粒度：ticks=uptime_ns、rate_hz=1e9）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布（原 hybrid 层）
 * 2026-06-14       1.1            zeh            clock_id 辅助
 * 2026-06-26       2.0            zeh            下沉至 common；ticks 升 64 位（#9-1b）
 * 2026-08-01       2.0            zeh           补全 Doxygen 合规注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_COMMON_TIMESTAMP_H
#define BM_COMMON_TIMESTAMP_H

#include <stdint.h>
#include "bm/common/bm_uptime.h"

/** HRT 定时器时钟域（与调用 CPU 对齐） */
#define BM_TIMESTAMP_CLOCK_HRT  0u

/**
 * @brief 返回逻辑 CPU 对应的 HRT 时钟域 ID
 *
 * `clock_id == cpu`；默认配置下与 `BM_TIMESTAMP_CLOCK_HRT` 一致。
 *
 * @param cpu 逻辑 CPU 编号
 * @return 与该核 HRT 对齐的时钟域 ID
 */
static inline uint16_t bm_timestamp_clock_for_cpu(uint32_t cpu) {
    return (uint16_t)cpu;
}

/**
 * @brief 单调时钟域时间戳
 *
 * 描述一次采样发生的时刻，供块流、融合与多媒体算法进行可比较的时间对齐。
 * 多 ADC、音频与相机时间对齐可借助 clock_id/clock_epoch 区分时钟域。
 *
 * 字段说明：
 *  - clock_id    : 时钟域标识（0 = HRT，与 CPU 绑定）
 *  - quality     : 时间戳质量（0 = 未知，非 0 = 有效）
 *  - clock_epoch : 时钟纪元（profile 切换或局部复位时递增）
 *  - ticks       : 单调节拍（64 位；@1MHz 约 584000 年不回绕；
 *                  旧路径填入 32 位 HRT 值时高 32 位为零，语义不变）
 *  - rate_hz     : 计数频率（Hz），用于将 ticks 换算为微秒
 */
typedef struct {
    uint16_t clock_id;
    uint16_t quality;
    uint32_t clock_epoch;
    uint64_t ticks;    /**< 单调节拍，64 位（#9-1b 由 uint32_t 升级） */
    uint32_t rate_hz;
} bm_timestamp_t;

/**
 * @brief 判断 32 位 HRT 计数器 a 是否早于 b（半量程回绕安全）
 *
 * 适用于 32 位硬件定时器节拍，允许 |a-b| < 2^31 个 tick 时正确判断先后。
 *
 * @param a 较早时刻候选（32 位单调节拍）
 * @param b 较晚时刻候选（32 位单调节拍）
 * @return 非零表示 a 在时间轴上早于 b
 */
static inline int bm_timestamp_before_32(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

/**
 * @brief 判断 32 位 HRT 计数器 a 是否晚于 b（半量程回绕安全）
 *
 * @param a 较晚时刻候选（32 位单调节拍）
 * @param b 较早时刻候选（32 位单调节拍）
 * @return 非零表示 a 在时间轴上晚于 b
 */
static inline int bm_timestamp_after_32(uint32_t a, uint32_t b) {
    return bm_timestamp_before_32(b, a);
}

/**
 * @brief 判断 64 位单调时间戳 a 是否早于 b
 *
 * 对 64 位单调时钟（如 bm_uptime_ns() 产生的 ns 值）做全宽直接比较。
 * 64 位 ns 粒度下可保证约 292 年内正确，不再受 32 位截断的 2.147 s 限制。
 *
 * @param a 较早时刻候选（64 位单调节拍）
 * @param b 较晚时刻候选（64 位单调节拍）
 * @return 非零表示 a 在时间轴上早于 b
 */
static inline int bm_timestamp_before(uint64_t a, uint64_t b) {
    return a < b;
}

/**
 * @brief 判断 64 位单调时间戳 a 是否晚于 b
 *
 * @param a 较晚时刻候选（64 位单调节拍）
 * @param b 较早时刻候选（64 位单调节拍）
 * @return 非零表示 a 在时间轴上晚于 b
 */
static inline int bm_timestamp_after(uint64_t a, uint64_t b) {
    return bm_timestamp_before(b, a);
}

/**
 * @brief 用 bm_uptime_ns() 构造 bm_timestamp_t（纳秒粒度）
 *
 * 以当前系统单调纳秒时钟直存 ticks，rate_hz 固定 1000000000（1 GHz，ns 粒度），
 * clock_id=0（HRT 域），clock_epoch/quality 置 0。
 * 供 #9-1c 统一时间基路径在无显式时间戳时自动打戳；调用方可按需覆写字段
 *（如用 bm_timestamp_clock_for_cpu() 覆写 clock_id）。
 *
 * @note ticks 为纳秒原值、rate_hz=1e9，与 1c bus BLOCK adapter 的 ns 直存一致；
 *       消费端按通用公式 `ticks * 1e6 / rate_hz` 换算微秒，与具体粒度无关。
 *
 * @return 以当前 uptime_ns 为 ticks 的时间戳
 */
static inline bm_timestamp_t bm_timestamp_from_uptime(void) {
    bm_timestamp_t ts;
    ts.clock_id    = 0u;
    ts.quality     = 0u;
    ts.clock_epoch = 0u;
    ts.ticks       = bm_uptime_ns();
    ts.rate_hz     = 1000000000u;
    return ts;
}

#endif /* BM_COMMON_TIMESTAMP_H */
