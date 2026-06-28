/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_atomic.h
 * @brief 原子变量读写操作
 *
 * 单核：关中断实现（低开销）。
 *
 * 统一提供 32 位无符号整数的读写、自增接口。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            统一原子实现
 *
 */
#ifndef BM_ATOMIC_H
#define BM_ATOMIC_H

#include "bm/common/bm_types.h"

/**
 * @brief 原子读取 32 位无符号整数值
 *
 * @param value 原子变量指针
 * @return 当前存储的值
 */
uint32_t bm_atomic_load(bm_atomic_t *value);

/**
 * @brief 原子写入 32 位无符号整数值
 *
 * @param value 原子变量指针
 * @param new_value 待写入的新值
 */
void bm_atomic_store(bm_atomic_t *value, uint32_t new_value);

/**
 * @brief 原子自增并返回递增后的值（UINT32_MAX 处饱和）
 *
 * @par 饱和语义（Saturation Semantics）
 *
 * - **单核路径（非路由模式）**：关中断串行化，值达 UINT32_MAX 时保持不变并返回
 *   UINT32_MAX，严格饱和。
 *
 * - **多核路由模式（BM_CPU_LOCAL_ENABLE_ROUTE）**：使用 CAS 环重试；若争用导致
 *   连续 CAS 失败超过 BM_CONFIG_ATOMIC_MAX_RETRIES 次，**直接饱和写入 UINT32_MAX**
 *   并返回，而非精确递增。这意味着：
 *   - 饱和后的 UINT32_MAX 表示"争用过载"，而非真实计数值。
 *   - 恢复需调用方手动清零（如系统重置/诊断周期归零）。
 *
 * @par 使用限制（CRITICAL）
 *
 * **仅适用于可饱和的诊断/统计计数器**（如超限次数、错误帧计数等），
 * 调用方须能容忍饱和后不再准确递增的语义。
 *
 * **禁止用于以下语义**：
 * - 环形缓冲区 head/tail 游标（饱和会破坏 SPSC 不变式）；
 * - 序列号、消息 ID 等需精确单调性的字段；
 * - 任何饱和到 UINT32_MAX 会被当作合法"值"的场景。
 *
 * @param value 原子变量指针
 * @return 自增后的值；已达 UINT32_MAX 时（或争用超限时）返回 UINT32_MAX
 */
uint32_t bm_atomic_inc(bm_atomic_t *value);

#endif /* BM_ATOMIC_H */
