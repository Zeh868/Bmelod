/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_log.c
 * @brief 分级日志实现：按 CPU 分域的 ring 或直写输出钩子
 *
 * 默认 `bm_log_output` 为空操作；启用 BM_CONFIG_LOG_USE_STDIO 时写入 stdout。
 * ring 模式下 RT 路径仅入队，由 bootstrap 按预算 drain。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            per-CPU 有界 ring
 * 2026-06-15       1.2            zeh            hard RT 日志配置 fail-closed
 * 2026-06-15       1.3            zeh            hard RT 下裁剪格式化实现
 *
 */
#include "bm_log.h"
#include "bm_config.h"
#include "bm/common/bm_atomic_ipc.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_critical.h"

#if BM_CONFIG_ENABLE_LOG && !BM_CONFIG_HARD_RT_PROFILE
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#endif

#if BM_CONFIG_HARD_RT_PROFILE && BM_CONFIG_ENABLE_LOG && !BM_CONFIG_LOG_RING
#error "hard RT profile requires BM_CONFIG_LOG_RING or BM_CONFIG_ENABLE_LOG=0"
#endif

#if BM_CONFIG_ENABLE_LOG && !BM_CONFIG_HARD_RT_PROFILE
/** 等级前缀字符 */
static const char *const k_level_chars[] = {
    "E", "W", "I", "D", "T"
};
#endif

#if BM_CONFIG_ENABLE_LOG && BM_CONFIG_LOG_RING

typedef struct {
    bm_atomic_ipc_u32_t head;
    bm_atomic_ipc_u32_t tail;
    bm_atomic_ipc_u32_t drop;
    struct {
        uint16_t len;
        char     data[BM_CONFIG_LOG_BUF_SIZE];
    } slot[BM_CONFIG_LOG_RING_DEPTH];
} bm_log_ring_t;

static bm_log_ring_t s_log_ring[BM_CONFIG_CPU_COUNT];

static bm_log_ring_t *log_ring_this(void) {
    uint32_t cpu = bm_hal_cpu_id();

    /* CPU 越界时回退到 0 号环，避免空指针崩溃（fail-operational） */
    return (cpu < BM_CONFIG_CPU_COUNT) ? &s_log_ring[cpu] : &s_log_ring[0];
}

/*
 * 回绕安全索引：head/tail 在 [0, 2*DEPTH) 内自由运行，而非单调到 2^32。
 * 2*DEPTH 是 DEPTH 的整数倍，物理槽位 = 计数 % DEPTH 在回绕点连续，
 * 消除单调计数在 2^32 回绕（2^32 通常不是 DEPTH 的倍数）导致的槽位别名。
 * occupancy ∈ [0, DEPTH]，故 head==tail 唯一表示空，无空/满歧义。
 */
#define BM_LOG_RING_SPAN (2u * (uint32_t)BM_CONFIG_LOG_RING_DEPTH)

/**
 * @brief 回绕推进 ring 索引
 */
static inline uint32_t log_ring_advance(uint32_t v) {
    uint32_t next = v + 1u;

    return (next >= BM_LOG_RING_SPAN) ? 0u : next;
}

/**
 * @brief 计算当前环占用量
 */
static inline uint32_t log_ring_occupancy(uint32_t head, uint32_t tail) {
    return (head >= tail) ? (head - tail) : (head + BM_LOG_RING_SPAN - tail);
}

/**
 * @brief 将一条日志推入 per-CPU ring
 *
 * @param ring 目标 ring 指针
 * @param buf 日志缓冲区
 * @param len 日志长度
 */
static void log_ring_push(bm_log_ring_t *ring, const char *buf, size_t len) {
    uint32_t head;
    uint32_t tail;
    uint32_t slot;
    uint32_t i;

    if (!ring || !buf || len == 0u) {
        return;
    }
    if (len > BM_CONFIG_LOG_BUF_SIZE) {
        len = BM_CONFIG_LOG_BUF_SIZE;
    }

    head = bm_atomic_ipc_load_u32(&ring->head);
    tail = bm_atomic_ipc_load_u32(&ring->tail);
    if (log_ring_occupancy(head, tail) >= BM_CONFIG_LOG_RING_DEPTH) {
        bm_atomic_ipc_inc_u32(&ring->drop);
        return;
    }

    slot = head % BM_CONFIG_LOG_RING_DEPTH;
    ring->slot[slot].len = (uint16_t)len;
    for (i = 0u; i < len; i++) {
        ring->slot[slot].data[i] = buf[i];
    }
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&ring->head, log_ring_advance(head));
}

/**
 * @brief 从指定 CPU 的日志 ring 中 drain 最多 budget 条日志
 *
 * 仅 bootstrap CPU 可调用，且作为每 CPU SPSC 环的唯一消费者。
 *
 * @param cpu 目标 CPU ID
 * @param budget 最大 drain 条数
 * @return 实际 drain 条数
 */
uint32_t bm_log_drain_cpu(uint32_t cpu, uint32_t budget) {
    bm_log_ring_t *ring;
    uint32_t drained = 0u;

    /*
     * Bootstrap CPU 是每个 per-CPU SPSC 环的唯一消费者；
     * 生产者仅推进 head，仅该消费者推进 tail。
     */
    if (cpu >= BM_CONFIG_CPU_COUNT || budget == 0u ||
        !bm_hal_cpu_is_bootstrap()) {
        return 0u;
    }
    ring = &s_log_ring[cpu];
    while (drained < budget) {
        uint32_t tail = bm_atomic_ipc_load_u32(&ring->tail);
        uint32_t head = bm_atomic_ipc_load_u32(&ring->head);

        if (head == tail) {
            break;
        }
        uint32_t index = tail % BM_CONFIG_LOG_RING_DEPTH;
        uint16_t len = ring->slot[index].len;
        char buf[BM_CONFIG_LOG_BUF_SIZE];

        if (len > 0u && len <= BM_CONFIG_LOG_BUF_SIZE) {
            uint32_t j;
            for (j = 0u; j < len; j++) {
                buf[j] = ring->slot[index].data[j];
            }
            bm_log_output(buf, len);
        }
        bm_atomic_ipc_store_u32(&ring->tail, log_ring_advance(tail));
        drained++;
    }
    return drained;
}

/**
 * @brief 在当前 CPU 上 drain 日志（仅 bootstrap CPU 实际工作）
 *
 * @param budget 最大 drain 条数
 * @return 实际 drain 条数
 */
uint32_t bm_log_drain_on_this_cpu(uint32_t budget) {
    return bm_hal_cpu_is_bootstrap() ?
        bm_log_drain_cpu(bm_hal_cpu_id(), budget) : 0u;
}

/**
 * @brief 获取指定 CPU 的日志丢弃计数
 *
 * @param cpu 目标 CPU ID
 * @return 丢弃计数
 */
uint32_t bm_log_drop_count(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return 0u;
    }
    return bm_atomic_ipc_load_u32(&s_log_ring[cpu].drop);
}

/**
 * @brief 检查日志 ring 配置是否满足最小深度要求
 *
 * @return 1 配置可用；0 配置不可用
 */
int bm_log_mp_profile_ok(void) {
    return (BM_CONFIG_LOG_RING_DEPTH >= 4u) ? 1 : 0;
}

#else

/**
 * @brief 非 ring 模式下默认认为配置可用
 */
int bm_log_mp_profile_ok(void) {
    return 1;
}

#endif /* BM_CONFIG_ENABLE_LOG && BM_CONFIG_LOG_RING */

/**
 * @brief 按等级格式化并输出一条日志
 *
 * @param level 日志等级
 * @param tag 模块标签
 * @param fmt printf 风格格式字符串
 */
void bm_log(bm_log_level_t level, const char *tag, const char *fmt, ...) {
#if BM_CONFIG_ENABLE_LOG && !BM_CONFIG_HARD_RT_PROFILE
    char buf[BM_CONFIG_LOG_BUF_SIZE];
    int level_index = (int)level;
    int prefix_len;
    va_list ap;
    size_t len;

    if (!fmt) {
        return;
    }
    if (level_index < (int)BM_LOG_ERROR) {
        level_index = (int)BM_LOG_ERROR;
    }
    if (level_index > (int)BM_LOG_TRACE) {
        level_index = (int)BM_LOG_TRACE;
    }
    if (!tag) {
        tag = "bm";
    }

    prefix_len = snprintf(buf, sizeof(buf), "[%s][%s] ",
                          k_level_chars[level_index], tag);
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(buf)) {
        return;
    }

    va_start(ap, fmt);
    {
        int body_len = vsnprintf(buf + prefix_len,
                                 sizeof(buf) - (size_t)prefix_len, fmt, ap);

        va_end(ap);
        /*
         * body_len < 0 为编码错误，无有效内容可输出，丢弃整条。
         * body_len >= 剩余容量表示 vsnprintf 已就地截断（写满 cap-1 字节
         * 并补 '\0'）：诊断路径宁可截断也不丢，故不再 return，
         * 下方 strlen(buf) 自然取到截断后的有效长度（min(ret, cap-1)）。
         */
        if (body_len < 0) {
            return;
        }
    }

    len = strlen(buf);
    if (len + 1u < sizeof(buf) && (len == 0u || buf[len - 1u] != '\n')) {
        buf[len] = '\n';
        buf[len + 1u] = '\0';
        len++;
    }

#if BM_CONFIG_ENABLE_LOG && BM_CONFIG_LOG_RING
    {
        /*
         * 使用原生 bm_hal_critical_enter（全关中断），而非可能启用
         * 优先级掩码的 BM_CRITICAL_ENTER。原因：优先级掩码下 HRT ISR
         * 可能在槽位写入中途抢占并向同一环写日志，导致 head 推进前
         * 槽数据损坏。日志为诊断用途，此处短暂全关中断可接受且具确定性。
         */
        bm_irq_state_t irq = bm_hal_critical_enter();
        log_ring_push(log_ring_this(), buf, len);
        bm_hal_critical_exit(irq);
    }
#else
    bm_log_output(buf, len);
#endif
#else
    (void)level;
    (void)tag;
    (void)fmt;
#endif
}
