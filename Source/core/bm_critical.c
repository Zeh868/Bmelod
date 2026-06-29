/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_critical.c
 * @brief 原子变量读写实现
 *
 * 默认路径：BM_CRITICAL_ENTER() 关中断保证原子性（低开销）。
 * 按 CPU 路由启用时：bm_atomic_ipc_* 原子指令保证可见性与排序（自动切换）。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            统一 BM_CRITICAL 宏；修正 inc 语义
 * 2026-06-14       1.2            zeh            按 CPU 路由时自动切换 bm_atomic_ipc_*
 *
 */
#include "bm_atomic.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm_critical_wrap.h"
#include "bm/core/bm_cpu_local.h"

#include <stdint.h>

#if BM_CPU_LOCAL_ENABLE_ROUTE

/*
 * 按 CPU 路由时：索引可能跨 CPU 共享，必须用原子保证 acquire/release 语义；
 * 关中断仅屏蔽本 CPU 抢占，无法防止另一 CPU 并发改写 write_idx。
 */
/**
 * @brief 原子加载 32 位无符号整数
 *
 * @param v 原子变量指针
 * @return 当前值；v 为 NULL 时返回 0
 */
uint32_t bm_atomic_load(bm_atomic_t *v) {
    if (!v) {
        return 0u;
    }
    return bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)v);
}

/**
 * @brief 原子存储 32 位无符号整数
 *
 * @param v 原子变量指针
 * @param val 待写入值
 */
void bm_atomic_store(bm_atomic_t *v, uint32_t val) {
    if (!v) {
        return;
    }
    bm_atomic_ipc_store_u32((bm_atomic_ipc_u32_t *)v, val);
}

/**
 * @brief 原子自增 1（多核路由路径：CAS 环，超限饱和）
 *
 * 按 CPU 路由启用时使用 CAS 环实现；若并发争用导致连续 CAS 失败超过
 * BM_CONFIG_ATOMIC_MAX_RETRIES 次，**直接饱和写入 UINT32_MAX** 并返回，
 * 不再精确递增。饱和后值为 UINT32_MAX，语义为"争用过载/溢出"。
 *
 * @par [F-6 饱和语义] 使用限制
 *
 * 此函数**仅适用于可饱和的诊断/统计计数**（超限次数、错误帧计数等）：
 *   - 路由模式下争用超 BM_CONFIG_ATOMIC_MAX_RETRIES 次即饱和到 UINT32_MAX；
 *   - 饱和后不再精确，须由调用方容忍（如诊断读取后清零）。
 * **禁止用于** 环形缓冲区游标、消息序列号或任何需精确单调性的索引/序号语义。
 *
 * @param v 原子变量指针
 * @return 自增后的值；v 为 NULL 时返回 0；争用超限或已饱和时返回 UINT32_MAX
 */
uint32_t bm_atomic_inc(bm_atomic_t *v) {
    uint32_t current;
    uint32_t retry;

    if (!v) {
        return 0u;
    }
    /*
     * 无原生 fetch_add 时用手写 CAS 环：失败说明并发争用，重读 current 重试。
     * 超过重试上界则饱和到 UINT32_MAX，避免无限自旋拖死 ISR/主循环。
     */
    current = bm_atomic_ipc_load_u32((bm_atomic_ipc_u32_t *)v);
    for (retry = 0u; retry < BM_CONFIG_ATOMIC_MAX_RETRIES; retry++) {
        uint32_t desired;

        if (current == UINT32_MAX) {
            return UINT32_MAX;
        }
        desired = current + 1u;
        if (bm_atomic_ipc_compare_exchange_u32(
                (bm_atomic_ipc_u32_t *)v, &current, desired)) {
            return desired;
        }
    }
    bm_atomic_ipc_store_u32((bm_atomic_ipc_u32_t *)v, UINT32_MAX);
    return UINT32_MAX;
}

#else /* !BM_CPU_LOCAL_ENABLE_ROUTE — 默认路径：关中断即原子 */

/*
 * 默认路径无跨 CPU 争用：BM_CRITICAL_ENTER 关中断即可串行化 ISR 与主循环，
 * 比引入原子指令开销更低，且与框架其余子系统临界区模型一致。
 */

/**
 * @brief 单核下以关中断方式原子加载
 *
 * @param v 原子变量指针
 * @return 当前值；v 为 NULL 时返回 0
 */
uint32_t bm_atomic_load(bm_atomic_t *v) {
    if (!v) {
        return 0u;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t val = *v;
    BM_CRITICAL_EXIT(s);
    return val;
}

/**
 * @brief 单核下以关中断方式原子存储
 *
 * @param v 原子变量指针
 * @param val 待写入值
 */
void bm_atomic_store(bm_atomic_t *v, uint32_t val) {
    if (!v) {
        return;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    *v = val;
    BM_CRITICAL_EXIT(s);
}

/**
 * @brief 单核下以关中断方式原子自增 1（默认路径：饱和语义）
 *
 * 关中断串行化，值达 UINT32_MAX 时保持不变并返回 UINT32_MAX，严格饱和。
 *
 * @par [F-6 饱和语义] 使用限制
 *
 * 此函数**仅适用于可饱和的诊断/统计计数**；饱和到 UINT32_MAX 后不再递增。
 * **禁止用于**环形缓冲区游标、消息序列号或任何需精确单调性的索引/序号语义；
 * 此约束在多核路由路径下更为关键（见路由路径同名函数注释）。
 *
 * @param v 原子变量指针
 * @return 自增后的值；v 为 NULL 时返回 0；已达 UINT32_MAX 时保持并返回 UINT32_MAX
 */
uint32_t bm_atomic_inc(bm_atomic_t *v) {
    if (!v) {
        return 0u;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t val = *v;

    if (val != UINT32_MAX) {
        val++;
        *v = val;
    }
    BM_CRITICAL_EXIT(s);
    return val;
}

#endif /* BM_CPU_LOCAL_ENABLE_ROUTE */
