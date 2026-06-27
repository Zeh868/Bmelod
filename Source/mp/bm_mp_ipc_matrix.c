/**
 * @file bm_mp_ipc_matrix.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief PERCPU IPC 矩阵实现
 *
 * N×N 有向 SPSC 事件环；读游标保存在目标核 endpoint 状态。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-18
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            seq 异常计数与 source_seq 跳零
 * 2026-06-18       1.2            zeh            seq 异常触发故障钩子，硬实时下可 safe-stop
 *
 */
#include "bm/mp/bm_mp_ipc.h"
#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm/mp/bm_mp_boot.h"
#include "bm_critical_wrap.h"
#include "bm_event.h"
#include "bm_log.h"
#include "hal/bm_hal_cache.h"

#include <string.h>

/*
 * 确定性流式跨核一致性：默认静态存储位于普通（可缓存）BSS。在带 D-cache 的
 * 真机多核上，IPC 矩阵必须落在 non-cacheable / coherent 区——否则跨核读取
 * 见到陈旧 cache 行，静默损坏事件流。此处编译期强制：要么经 BM_MP_SHARED_SECTION
 * 将矩阵放入非缓存 section（并置 BM_MP_SHARED_PLACEMENT_VERIFIED=1），要么改用
 * bm_mp_ipc_matrix_attach() 显式挂载到已正确放置的区域。
 */
#if BM_MP_MULTICORE && !BM_HAL_CACHE_IS_NOOP && !BM_MP_SHARED_PLACEMENT_VERIFIED
#error "Cached multicore: IPC matrix must reside in non-cacheable/coherent memory. Define BM_MP_SHARED_SECTION to a non-cacheable linker section (or use bm_mp_ipc_matrix_attach() with an explicitly-placed base), then set BM_MP_SHARED_PLACEMENT_VERIFIED=1."
#endif

static bm_mp_ipc_matrix_t *s_matrix;
static bm_mp_ipc_fault_fn_t s_ipc_fault_hook;

void bm_mp_ipc_set_fault_hook(bm_mp_ipc_fault_fn_t fn) {
    s_ipc_fault_hook = fn;
}
/** 默认静态矩阵；多核真机须经 BM_MP_SHARED_SECTION 放入 coherent 区 */
static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) BM_MP_SHARED_SECTION
    bm_mp_ipc_matrix_t s_matrix_storage;

int bm_mp_ipc_matrix_format(bm_mp_ipc_matrix_t *matrix) {
    if (!matrix) {
        return BM_ERR_INVALID;
    }
    memset(matrix, 0, sizeof(*matrix));
    matrix->magic = BM_MP_IPC_MAGIC;
    matrix->layout_version = BM_MP_IPC_LAYOUT_VERSION;
    return BM_OK;
}

int bm_mp_ipc_matrix_attach(bm_mp_ipc_matrix_t **out, uintptr_t base) {
    bm_mp_ipc_matrix_t *p;

    if (!out) {
        return BM_ERR_INVALID;
    }
    p = (bm_mp_ipc_matrix_t *)(void *)base;
    if (!p || p->magic != BM_MP_IPC_MAGIC ||
        p->layout_version != BM_MP_IPC_LAYOUT_VERSION) {
        return BM_ERR_INVALID;
    }
    s_matrix = p;
    *out = p;
    return BM_OK;
}

bm_mp_ipc_matrix_t *bm_mp_ipc_matrix(void) {
    return s_matrix;
}

/** 有向环 source→target；禁止自环，越界返回 NULL */
static bm_mp_ipc_event_ring_t *ring_for(uint8_t source, uint8_t target) {
    if (!s_matrix || source >= BM_CONFIG_CPU_COUNT ||
        target >= BM_CONFIG_CPU_COUNT || source == target) {
        return NULL;
    }
    return &s_matrix->event_ring[source][target];
}

int bm_mp_ipc_publish_event_forward(uint8_t target_cpu,
                                    const bm_event_t *event,
                                    const void *data,
                                    size_t len) {
    uint8_t source = (uint8_t)BM_CPU_THIS();
    bm_mp_ipc_event_ring_t *ring;
    uint32_t head;
    uint32_t tail;
    uint32_t next;
    bm_mp_ipc_event_slot_t *slot;
    bm_irq_state_t irq_state;

    if (!event || target_cpu >= BM_CONFIG_CPU_COUNT ||
        source >= BM_CONFIG_CPU_COUNT || source == target_cpu ||
        event->type >= BM_CONFIG_MAX_EVENT_TYPES ||
        event->priority >= BM_CONFIG_EVENT_PRIORITIES ||
        (len > 0u && data == NULL)) {
        return BM_ERR_INVALID;
    }
    if (len > BM_CONFIG_EVENT_INLINE_DATA_SIZE) {
        return BM_ERR_NO_MEM;
    }
    ring = ring_for(source, target_cpu);
    if (!ring) {
        return BM_ERR_NOT_INIT;
    }

    irq_state = BM_CRITICAL_ENTER();
    head = bm_atomic_ipc_load_u32(&ring->head.value);
    tail = bm_atomic_ipc_load_u32(&ring->tail.value);
    next = (head + 1u) % BM_CONFIG_MP_IPC_EVENT_RING_DEPTH;
    if (next == tail) {
        /* 环满：计丢弃并立即返回，避免阻塞发布方（通常为 SRT 主循环） */
        bm_atomic_ipc_inc_u32(&ring->overflow_drops);
        BM_CRITICAL_EXIT(irq_state);
        return BM_ERR_OVERFLOW;
    }

    slot = &ring->slots[head];
    slot->type = event->type;
    slot->priority = event->priority;
    slot->source_id = event->source_id;
    slot->data_len = (uint8_t)len;
    slot->source_seq = bm_atomic_ipc_inc_u32(&ring->next_source_seq);
    if (slot->source_seq == 0u) {
        slot->source_seq = bm_atomic_ipc_inc_u32(&ring->next_source_seq);
    }
    if (data && len > 0u) {
        memcpy(slot->inline_data, data, len);
    }
    /*
     * 冗余 release fence：下方 release-store 已保证槽写入可见性；
     * 在弱序架构及 store-release 语义可能不可靠的工具链
     * （如 MSVC ARM）上保留为防御性冗余。
     */
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&ring->head.value, next);
    BM_CRITICAL_EXIT(irq_state);
    return BM_OK;
}

int bm_mp_ipc_drain_on_this_cpu(uint32_t budget) {
    uint8_t target = (uint8_t)BM_CPU_THIS();
    uint32_t drained = 0u;
    uint32_t src;
    bm_mp_ipc_matrix_t *matrix = s_matrix;

    if (!matrix || !bm_mp_cpu_valid()) {
        return 0;
    }

    for (src = 0u; src < BM_CONFIG_CPU_COUNT && drained < budget; src++) {
        uint32_t per_src = 0u;

        while (per_src < BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET &&
               drained < budget) {
            bm_mp_ipc_event_ring_t *ring = ring_for((uint8_t)src, target);
            uint32_t tail;
            uint32_t head;
            bm_mp_ipc_event_slot_t *slot;
            bm_mp_ipc_event_slot_t local_slot;
            uint32_t *last_seq;
            int rc;

            if (!ring) {
                break;
            }
            tail = bm_atomic_ipc_load_u32(&ring->tail.value);
            head = bm_atomic_ipc_load_u32(&ring->head.value);
            if (tail == head) {
                break;
            }

            slot = &ring->slots[tail];
            bm_atomic_ipc_fence_acquire();
            local_slot = *slot;

            last_seq = &matrix->endpoint[target].event_last_seq[src];
            /*
             * 使用带符号差值的回绕安全比较：uint32_t 序列号在 2^32 后回绕。
             * seq==0 视为未初始化/回绕哨兵（每 2^32 条消息丢失一条）。
             * (int32_t)(seq - last) <= 0 正确处理回绕，只要差值 < 2^31。
             */
            if (local_slot.source_seq == 0u ||
                (int32_t)(local_slot.source_seq - *last_seq) <= 0) {
                bm_atomic_ipc_inc_u32(&ring->sequence_errors);
                /*
                 * 序列异常表明跨核事件流已不可信。通知故障钩子（若注册）；
                 * 硬实时剖面据此安全停机，确定性流式不得在不可信流上继续。
                 */
                if (s_ipc_fault_hook) {
                    s_ipc_fault_hook((uint8_t)src, target);
                }
                bm_atomic_ipc_store_u32(
                    &ring->tail.value,
                    (tail + 1u) % BM_CONFIG_MP_IPC_EVENT_RING_DEPTH);
                per_src++;
                drained++;
#if BM_CONFIG_MP_HARD_RT_PROFILE
                bm_mp_boot_report_failure();
                bm_mp_signal_demo_stop();
                return BM_ERR_INVALID;
#else
                continue;
#endif
            }

            /*
             * 确定性流式注入：IPC drain 将环槽数据通过 publish_copy 重新拷贝
             * 入目标核事件队列。使用 publish_copy（而非 publish_event），因为：
             * 1. hard RT 剖面下 publish_event 被禁止（零拷贝生命周期不可静态分析）
             * 2. 环槽数据已在 local_slot 栈拷贝中，publish_copy 再内联拷贝入队列，
             *    三次拷贝在 WCET 预算中均已被统计
             * 3. ev.source_id 在 event_publish_impl 中自动设为本核 ID，
             *    区分原始发布核（local_slot.source_id）与注入核
             */
            rc = bm_event_publish_copy_from_source(
                local_slot.type,
                local_slot.priority,
                local_slot.source_id,
                local_slot.inline_data,
                local_slot.data_len);
            if (rc != BM_OK) {
                bm_atomic_ipc_inc_u32(&ring->drain_stalls);
#if BM_CONFIG_MP_HARD_RT_PROFILE
                /*
                 * Hard RT 下事件入队失败视为致命:无法在 WCET 内消化跨核事件
                 * 流,上报故障并安全停机。
                 */
                bm_mp_boot_report_failure();
                bm_mp_signal_demo_stop();
                return rc;
#else
                /*
                 * 目标事件队列瞬态打满(publish_copy 返回 BM_ERR_OVERFLOW 等)
                 * 是可恢复条件:不推进 tail、不更新 last_seq,保留环槽消息,
                 * 下一轮 drain 重试,形成背压而非静默丢包。
                 */
                return (drained > 0u) ? (int)drained : rc;
#endif
            }

            *last_seq = local_slot.source_seq;
            bm_atomic_ipc_store_u32(
                &ring->tail.value,
                (tail + 1u) % BM_CONFIG_MP_IPC_EVENT_RING_DEPTH);
            per_src++;
            drained++;
        }
    }
    return (int)drained;
}

uint32_t bm_mp_ipc_event_ring_drops(uint8_t source, uint8_t target) {
    bm_mp_ipc_event_ring_t *ring = ring_for(source, target);

    if (!ring) {
        return 0u;
    }
    return bm_atomic_ipc_load_u32(&ring->overflow_drops);
}

int bm_ipc_drain_on_this_cpu(uint32_t budget) {
#if BM_MP_MULTICORE
    return bm_mp_ipc_drain_on_this_cpu(budget);
#else
    (void)budget;
    return 0;
#endif
}

void bm_mp_ipc_matrix_use_static_storage(void) {
    if (bm_mp_ipc_matrix_format(&s_matrix_storage) == BM_OK) {
        s_matrix = &s_matrix_storage;
    }
}

void bm_mp_ipc_count_stream_block(uint8_t cpu) {
    if (s_matrix && cpu < BM_CONFIG_CPU_COUNT) {
        bm_atomic_ipc_inc_u32(&s_matrix->stream_blocks_processed[cpu]);
    }
}

uint32_t bm_mp_ipc_stream_blocks_processed(uint8_t cpu) {
    if (!s_matrix || cpu >= BM_CONFIG_CPU_COUNT) {
        return 0u;
    }
    return bm_atomic_ipc_load_u32(
        &s_matrix->stream_blocks_processed[cpu]);
}

int bm_mp_ipc_publish_cmd_snapshot(uint8_t target_cpu) {
    uint8_t source = (uint8_t)BM_CPU_THIS();
    bm_mp_ipc_matrix_t *matrix = s_matrix;

    if (!matrix || source >= BM_CONFIG_CPU_COUNT ||
        target_cpu >= BM_CONFIG_CPU_COUNT || source == target_cpu) {
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_inc_u32(
        &matrix->cmd_snapshot_seq[source][target_cpu]);
    return BM_OK;
}

int bm_mp_ipc_cmd_snapshot_read(uint8_t source,
                                uint32_t *seq_out,
                                uint32_t *last_seq_inout) {
    uint8_t target = (uint8_t)BM_CPU_THIS();
    bm_mp_ipc_matrix_t *matrix = s_matrix;
    uint32_t seq;
    uint32_t *cursor;

    if (!matrix || source >= BM_CONFIG_CPU_COUNT ||
        target >= BM_CONFIG_CPU_COUNT || source == target) {
        return BM_ERR_INVALID;
    }
    cursor = last_seq_inout ?
                 last_seq_inout :
                 &matrix->endpoint[target].cmd_last_seq[source];
    seq = bm_atomic_ipc_load_u32(
        &matrix->cmd_snapshot_seq[source][target]);
    if (seq == *cursor) {
        return BM_ERR_WOULD_BLOCK;
    }
    *cursor = seq;
    if (seq_out) {
        *seq_out = seq;
    }
    return BM_OK;
}
