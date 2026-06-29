/**
 * @file bm_stream_relay.c
 * @brief 跨核块 relay 实现（有界 SPSC + 显式 memcpy）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            profile_epoch 钩子
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/hybrid/bm_stream_relay.h"
#include "bm/common/bm_profile_epoch.h"
#include "bm/core/bm_cpu_local.h"
#include "hal/bm_hal_cache.h"
#include "hal/bm_hal_cpu.h"

#include <stdbool.h>
#include <string.h>

/*
 * 确定性流式跨核一致性：relay 以显式 memcpy + release/acquire fence 跨核传递块，
 * 但不对 slot 缓冲做 D-cache 维护——故缓冲必须由调用方放置在 non-cacheable /
 * coherent 区（与 relay->cache_policy 声明一致）。runtime 无法核验物理内存属性，
 * 因此带 D-cache 的真机多核须在 Port 经 BM_MP_SHARED_PLACEMENT_VERIFIED=1 显式
 * 承诺放置正确，否则编译期 #error。
 */
#if (BM_CONFIG_CPU_COUNT > 1u) && !BM_HAL_CACHE_IS_NOOP && \
    !BM_MP_SHARED_PLACEMENT_VERIFIED
#error "Cached multicore: stream relay slot buffers must reside in non-cacheable/coherent memory. Place them via BM_MP_SHARED_SECTION (or an explicitly-placed region) and set BM_MP_SHARED_PLACEMENT_VERIFIED=1."
#endif

#if !BM_CONFIG_STREAM_RELAY_EXTERNAL_CORRUPT_HOOK
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
/**
 * @brief 跨核 relay 检测到损坏 slot 时的弱钩子（默认空实现）
 *
 * corrupt_dropped > 0 表明跨核同步不一致（epoch 不匹配、未完成写入、
 * 跨代陈旧数据）。hard RT 剖面应覆盖此钩子触发安全停机。
 * 不支持弱符号的平台可定义 BM_CONFIG_STREAM_RELAY_EXTERNAL_CORRUPT_HOOK=1
 * 并由应用提供该函数。
 *
 * @param relay 触发 corrupt 的 relay 实例
 * @param header 损坏 slot 的头部指针
 */
void bm_stream_relay_corrupt_hook(bm_stream_relay_t *relay,
                                  const bm_stream_relay_slot_header_t *header) {
    (void)relay;
    (void)header;
}
#endif

typedef struct {
    bm_stream_relay_t           *relay;
    bm_stream_relay_consume_fn_t consume;
    void                        *context;
} bm_stream_relay_binding_t;

static bm_stream_relay_binding_t
    s_relay_bindings[BM_CONFIG_CPU_COUNT][BM_CONFIG_MP_RELAY_REGISTRY_MAX];
static uint8_t s_relay_binding_count[BM_CONFIG_CPU_COUNT];
static bool s_relay_registry_frozen[BM_CONFIG_CPU_COUNT];

static int relay_valid(const bm_stream_relay_t *relay);

/**
 * @brief 重置当前核的 relay 注册表
 */
void bm_stream_relay_registry_reset(void) {
    memset(s_relay_bindings, 0, sizeof(s_relay_bindings));
    memset(s_relay_binding_count, 0, sizeof(s_relay_binding_count));
    memset(s_relay_registry_frozen, 0, sizeof(s_relay_registry_frozen));
}

/**
 * @brief 冻结当前核的 relay 注册表，禁止后续注册
 */
void bm_stream_relay_freeze_registry(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu < BM_CONFIG_CPU_COUNT) {
        s_relay_registry_frozen[cpu] = true;
    }
}

/**
 * @brief 在当前核注册一个 relay 及其消费回调
 *
 * @param relay relay 实例指针
 * @param consume 消费回调函数
 * @param context 回调上下文
 * @return BM_OK 成功；负值表示失败
 */
int bm_stream_relay_register_on_this_cpu(bm_stream_relay_t *relay,
                                         bm_stream_relay_consume_fn_t consume,
                                         void *context) {
    uint32_t cpu = BM_CPU_THIS();
    uint8_t n;

    if (!relay || !consume || !bm_cpu_local_valid()) {
        return BM_ERR_INVALID;
    }
    if (s_relay_registry_frozen[cpu]) {
        return BM_ERR_BUSY;
    }
    if (relay->target_cpu != (uint8_t)cpu) {
        return BM_ERR_INVALID;
    }
    if (!relay_valid(relay)) {
        return BM_ERR_NOT_INIT;
    }
    n = s_relay_binding_count[cpu];
    if (n >= BM_CONFIG_MP_RELAY_REGISTRY_MAX) {
        return BM_ERR_NO_MEM;
    }
    s_relay_bindings[cpu][n].relay = relay;
    s_relay_bindings[cpu][n].consume = consume;
    s_relay_bindings[cpu][n].context = context;
    s_relay_binding_count[cpu]++;
    return BM_OK;
}

/**
 * @brief 在当前核 drain 所有已注册 relay 的事件
 *
 * @param budget 最大 drain 条数
 * @return 实际 drain 条数
 */
int bm_stream_relay_drain_on_this_cpu(uint32_t budget) {
    uint32_t cpu = BM_CPU_THIS();
    uint32_t drained = 0u;
    uint8_t i;

    if (!bm_cpu_local_valid() || budget == 0u) {
        return 0;
    }
    for (i = 0u; i < s_relay_binding_count[cpu] && drained < budget; i++) {
        bm_stream_relay_binding_t *binding = &s_relay_bindings[cpu][i];
        int n;

        if (!binding->relay || !binding->consume) {
            continue;
        }
        n = bm_stream_relay_drain(binding->relay,
                                  budget - drained,
                                  binding->consume,
                                  binding->context);
        if (n > 0) {
            drained += (uint32_t)n;
        }
    }
    return (int)drained;
}

/** @brief 校验 relay 描述符字段合法 */
static int relay_valid(const bm_stream_relay_t *relay) {
    return relay != NULL && relay->initialized != 0 &&
           relay->slots != NULL && relay->depth >= 2u &&
           (relay->depth & (relay->depth - 1u)) == 0u &&
           relay->slot_bytes > 0u;
}

/** @brief 计算当前生效的 profile epoch */
static uint32_t relay_active_profile_epoch(const bm_stream_relay_t *relay) {
    uint32_t epoch = bm_profile_epoch_current();

    if (epoch != 0u) {
        return epoch;
    }
    return relay != NULL ? relay->profile_epoch : 0u;
}

/** @brief 计算单个 relay slot 的字节跨度（含头部） */
static uint32_t relay_slot_stride(const bm_stream_relay_t *relay) {
    return BM_STREAM_RELAY_SLOT_STRIDE(relay->slot_bytes);
}

/**
 * @brief 获取指定 relay slot 的头部指针
 *
 * @param relay relay 实例指针
 * @param index slot 索引
 * @return slot 头部指针
 */
static bm_stream_relay_slot_header_t *relay_slot_header(
    bm_stream_relay_t *relay, uint32_t index) {
    return (bm_stream_relay_slot_header_t *)(void *)
        (relay->slots + (index * relay_slot_stride(relay)));
}

/**
 * @brief 初始化 relay 实例
 *
 * @param relay relay 描述符指针
 * @return BM_OK 成功；负值表示失败
 */
int bm_stream_relay_init(bm_stream_relay_t *relay) {
    if (!relay || !relay->slots || relay->depth < 2u ||
        relay->depth > BM_CONFIG_STREAM_RELAY_MAX_DEPTH ||
        (relay->depth & (relay->depth - 1u)) != 0u ||
        relay->slot_bytes == 0u ||
        ((uintptr_t)relay->slots % sizeof(uint32_t)) != 0u) {
        return BM_ERR_INVALID;
    }
#if BM_CONFIG_CPU_COUNT > 1u
    if (((uintptr_t)relay % BM_CONFIG_CACHE_LINE) != 0u ||
        ((uintptr_t)relay->slots % BM_CONFIG_CACHE_LINE) != 0u) {
        return BM_ERR_INVALID;
    }
#endif
    if (relay->cache_policy != BM_STREAM_RELAY_CACHE_NON_CACHEABLE &&
        relay->cache_policy != BM_STREAM_RELAY_CACHE_COHERENT) {
        return BM_ERR_INVALID;
    }
    if (relay->source_cpu >= BM_CONFIG_CPU_COUNT ||
        relay->target_cpu >= BM_CONFIG_CPU_COUNT ||
        relay->source_cpu == relay->target_cpu ||
        bm_hal_cpu_id() != relay->source_cpu) {
        return BM_ERR_INVALID;
    }
    if (relay->slot_bytes > UINT32_MAX -
        (uint32_t)sizeof(bm_stream_relay_slot_header_t)) {
        return BM_ERR_OVERFLOW;
    }
    memset(relay->slots, 0,
           (size_t)relay->depth * (size_t)relay_slot_stride(relay));
    bm_atomic_ipc_store_u32(&relay->head, 0u);
    bm_atomic_ipc_store_u32(&relay->tail, 0u);
    if (relay->boot_epoch == 0u) {
        relay->boot_epoch = 1u;
    }
    if (relay->producer_epoch == 0u) {
        relay->producer_epoch = 1u;
    }
    relay->next_sequence = 0u;
    memset(&relay->stats, 0, sizeof(relay->stats));
    relay->initialized = 1;
    return BM_OK;
}

/**
 * @brief 向 relay 发布一个 payload（源 CPU 调用）
 *
 * @param relay relay 实例指针
 * @param payload payload 数据指针
 * @param len payload 长度
 * @return BM_OK 成功；负值表示失败
 */
int bm_stream_relay_publish(bm_stream_relay_t *relay,
                            const void *payload,
                            uint32_t len) {
    uint32_t next;
    uint32_t head;
    uint32_t tail;
    bm_stream_relay_slot_header_t *header;
    uint8_t *dst;

    if (!relay_valid(relay) || !payload || len == 0u ||
        len > relay->slot_bytes) {
        return BM_ERR_INVALID;
    }
    if (bm_hal_cpu_id() != relay->source_cpu) {
        return BM_ERR_INVALID;
    }

    head = bm_atomic_ipc_load_u32(&relay->head);
    tail = bm_atomic_ipc_load_u32(&relay->tail);
    /*
     * 显式 acquire fence——ARM 弱内存序下确保 head/tail 的读取完成于
     * 后续 memcpy 开始之前。acquire-load 配对 consumer 的 release-store，
     * 但非原子 store（memcpy）可能被重排到 acquire-load 之前。
     */
    bm_atomic_ipc_fence_acquire();
    next = (head + 1u) & ((uint32_t)relay->depth - 1u);
    if (next == tail) {
        relay->stats.drop++;
        return BM_ERR_OVERFLOW;
    }

    header = relay_slot_header(relay, head);
    dst = (uint8_t *)(void *)(header + 1);
    memcpy(dst, payload, len);
    header->len = len;
    header->boot_epoch = relay->boot_epoch;
    header->profile_epoch = relay_active_profile_epoch(relay);
    header->producer_epoch = relay->producer_epoch;
    /*
     * 确定性跨核发布协议：
     * - 先写全部数据和 sequence，再 release fence，再原子提交 published_sequence。
     * - 不预先清零 sequence/published_sequence——消除 TOCTOU 窗口。
     * - consumer 通过 published_sequence==sequence 确认槽位完整。
     */
    /*
     * 确定性流式序列号：跳过 0（哨兵值与未初始化/损坏检测共享）。
     * consumer 通过 sequence==0 识别未完成写入，回绕到 0 会导致
     * 误判——主动跳过 0 消除此窗口。代价：每 2^32-1 条有效消息
     * 跳过一个序列号（运行约 49 天后首次发生）。
     */
    if (++relay->next_sequence == 0u) {
        relay->next_sequence = 1u;
    }
    header->sequence = relay->next_sequence;
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&header->published_sequence, header->sequence);
    bm_atomic_ipc_store_u32(&relay->head, next);
    return BM_OK;
}

/**
 * @brief 从 relay drain 事件到消费回调（目标 CPU 调用）
 *
 * @param relay relay 实例指针
 * @param budget 最大 drain 条数
 * @param consume 消费回调函数
 * @param context 回调上下文
 * @return 实际 drain 条数
 */
int bm_stream_relay_drain(bm_stream_relay_t *relay,
                          uint32_t budget,
                          bm_stream_relay_consume_fn_t consume,
                          void *context) {
    uint32_t drained = 0u;

    if (!relay_valid(relay) || budget == 0u) {
        return 0;
    }
    if (bm_hal_cpu_id() != relay->target_cpu) {
        return 0;
    }

    while (drained < budget) {
        uint32_t tail = bm_atomic_ipc_load_u32(&relay->tail);
        uint32_t head = bm_atomic_ipc_load_u32(&relay->head);
        bm_stream_relay_slot_header_t *header;
        const uint8_t *src;

        if (tail == head) {
            break;
        }
        header = relay_slot_header(relay, tail);
        /*
         * 确定性跨核消费协议：
         * 1. 原子读取 published_sequence（隐式 acquire）
         * 2. 显式 acquire fence —— 确保后续 header 字段读取
         *    不会重排到 published_sequence 读取之前（ARM 弱内存序必需）
         * 3. 验证 published_sequence == sequence 确认槽位完整
         * 4. 验证 epoch 一致性，拒绝跨代陈旧数据
         */
        {
            uint32_t pub_seq =
                bm_atomic_ipc_load_u32(&header->published_sequence);
            uint32_t slot_seq;

            bm_atomic_ipc_fence_acquire();
            slot_seq = header->sequence;
            if (pub_seq != slot_seq ||
                slot_seq == 0u ||
                header->boot_epoch != relay->boot_epoch ||
                header->profile_epoch != relay_active_profile_epoch(relay) ||
                header->producer_epoch != relay->producer_epoch ||
                header->len == 0u || header->len > relay->slot_bytes) {
                /*
                 * 确定性流式：区分 clean drop（环满）与 corrupt drop
                 *（epoch 不匹配 / 跨代陈旧数据 / 未完成写入）。
                 * 硬实时剖面中 corrupt_dropped > 0 表明跨核同步不一致，
                 * 须触发安全停机。
                 */
                relay->stats.corrupt_dropped++;
                bm_stream_relay_corrupt_hook(relay, header);
                bm_atomic_ipc_store_u32(
                    &relay->tail, (tail + 1u) & ((uint32_t)relay->depth - 1u));
                drained++;
                continue;
            }
        }
        src = (const uint8_t *)(const void *)(header + 1);
        if (consume) {
            consume(relay, src, header->len, header->sequence, context);
        }
        bm_atomic_ipc_store_u32(
            &relay->tail, (tail + 1u) & ((uint32_t)relay->depth - 1u));
        relay->stats.delivered++;
        drained++;
    }
    return (int)drained;
}

/**
 * @brief 获取 relay 统计信息指针
 *
 * @param relay relay 实例指针
 * @return 统计信息指针；无效时返回 NULL
 */
const bm_stream_relay_stats_t *bm_stream_relay_stats(const bm_stream_relay_t *relay) {
    return relay_valid(relay) ? &relay->stats : NULL;
}
