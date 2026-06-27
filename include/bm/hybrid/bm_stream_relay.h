/**
 * @file bm_stream_relay.h
 * @brief 跨核块数据显式拷贝中继（有界 SPSC）
 *
 * 不得冒充同核 `bm_stream` 零拷贝语义；复制延迟计入目标核 WCET。
 *
 * @core_affinity 跨核专用
 * 专为跨核 block 传输设计：源核调用 bm_stream_relay_publish，目标核调用
 * bm_stream_relay_drain。通过 bm_atomic_ipc_* + release/acquire fence 保证
 * 跨核内存序。不应用于同核传输（同核直接用 bm_stream）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_STREAM_RELAY_H
#define BM_STREAM_RELAY_H

#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_cpu_local.h"

#include <stdint.h>

#ifndef BM_CONFIG_STREAM_RELAY_MAX_DEPTH
#define BM_CONFIG_STREAM_RELAY_MAX_DEPTH  4u
#endif

#ifndef BM_CONFIG_RELAY_REGISTRY_MAX
#define BM_CONFIG_RELAY_REGISTRY_MAX  4u
#endif

#ifndef BM_CONFIG_MP_RELAY_REGISTRY_MAX
#define BM_CONFIG_MP_RELAY_REGISTRY_MAX  BM_CONFIG_RELAY_REGISTRY_MAX
#endif

#if defined(BM_CONFIG_MP_RELAY_REGISTRY_MAX) && \
    !defined(BM_CONFIG_RELAY_REGISTRY_MAX)
#define BM_CONFIG_RELAY_REGISTRY_MAX  BM_CONFIG_MP_RELAY_REGISTRY_MAX
#endif

#define BM_STREAM_RELAY_CACHE_NON_CACHEABLE  (1u << 0)
#define BM_STREAM_RELAY_CACHE_COHERENT       (1u << 1)

typedef struct {
    uint32_t drop;             /* 干净溢出丢弃（环满） */
    uint32_t corrupt_dropped;  /* 因 slot 损坏/epoch 不匹配丢弃 */
    uint32_t late;
    uint32_t delivered;
} bm_stream_relay_stats_t;

typedef struct bm_stream_relay bm_stream_relay_t;

typedef struct {
    uint32_t len;
    uint32_t boot_epoch;
    uint32_t profile_epoch;
    uint32_t producer_epoch;
    uint32_t sequence;
    bm_atomic_ipc_u32_t published_sequence;
} bm_stream_relay_slot_header_t;

typedef void (*bm_stream_relay_consume_fn_t)(bm_stream_relay_t *relay,
                                             const void *payload,
                                             uint32_t len,
                                             uint32_t sequence,
                                             void *context);

struct bm_stream_relay {
    uint8_t              source_cpu;
    uint8_t              target_cpu;
    uint32_t             slot_bytes;
    uint8_t              depth;   /* must be >= 2; init validates */
    uint8_t             *slots;
    bm_atomic_ipc_u32_t   head;
    bm_atomic_ipc_u32_t   tail;
    uint32_t              boot_epoch;
    uint32_t              profile_epoch;
    uint32_t              producer_epoch;
    uint32_t              cache_policy;
    uint32_t              next_sequence;
    bm_stream_relay_stats_t stats;
    int                  initialized;
};

#define BM_STREAM_RELAY_SLOT_STRIDE(slot_bytes) \
    (((uint32_t)sizeof(bm_stream_relay_slot_header_t) + \
      (uint32_t)(slot_bytes) + (uint32_t)sizeof(uint32_t) - 1u) & \
     ~((uint32_t)sizeof(uint32_t) - 1u))

#define BM_STREAM_RELAY_SLOTS(name, depth, slot_bytes) \
    static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) \
    uint8_t _bm_stream_relay_slots_##name \
        [(depth) * BM_STREAM_RELAY_SLOT_STRIDE(slot_bytes)]

#define BM_STREAM_RELAY_INSTANCE(name, depth, slot_bytes, src, dst) \
    static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) bm_stream_relay_t name = { \
        .source_cpu = (uint8_t)(src), \
        .target_cpu = (uint8_t)(dst), \
        .slot_bytes = (slot_bytes), \
        .depth = (depth), \
        .slots = _bm_stream_relay_slots_##name, \
        .cache_policy = BM_STREAM_RELAY_CACHE_NON_CACHEABLE \
    }

/**
 * @brief 初始化 relay 槽区
 *
 * @param relay relay 实例
 * @return BM_OK 成功
 */
int bm_stream_relay_init(bm_stream_relay_t *relay);

/**
 * @brief 源核发布一块 payload（显式拷贝入环）
 *
 * @param relay relay 实例
 * @param payload 源数据
 * @param len 字节长度（须 <= slot_bytes）
 * @return BM_OK 成功；BM_ERR_OVERFLOW 环满
 */
int bm_stream_relay_publish(bm_stream_relay_t *relay,
                            const void *payload,
                            uint32_t len);

/**
 * @brief 目标核按预算 drain 并回调消费
 *
 * @param relay relay 实例
 * @param budget 本轮最多处理块数
 * @param consume 消费回调
 * @param context 用户上下文
 * @return 实际交付块数
 */
int bm_stream_relay_drain(bm_stream_relay_t *relay,
                          uint32_t budget,
                          bm_stream_relay_consume_fn_t consume,
                          void *context);

/**
 * @brief 重置本进程 relay 注册表（启动期调用）
 */
void bm_stream_relay_registry_reset(void);

/**
 * @brief 冻结 relay 注册表（流式运行前调用）
 *
 * 调用后 bm_stream_relay_register_on_this_cpu 返回 BM_ERR_BUSY。
 */
void bm_stream_relay_freeze_registry(void);

/**
 * @brief 目标核注册 relay 消费回调（须在 `bm_stream_relay_init` 之后，冻结前）
 */
int bm_stream_relay_register_on_this_cpu(bm_stream_relay_t *relay,
                                         bm_stream_relay_consume_fn_t consume,
                                         void *context);

/**
 * @brief 目标核按预算 drain 已注册 relay（主循环调用）
 */
int bm_stream_relay_drain_on_this_cpu(uint32_t budget);

const bm_stream_relay_stats_t *bm_stream_relay_stats(const bm_stream_relay_t *relay);

/**
 * @brief 跨核 relay 检测到损坏 slot 时的弱钩子
 *
 * corrupt_dropped > 0 表明跨核同步不一致（epoch 不匹配、未完成写入、
 * 跨代陈旧数据）。涵盖 hard RT 剖面下须触发安全停机的场景。
 * 默认实现为空；不支持弱符号的平台可定义
 * BM_CONFIG_STREAM_RELAY_EXTERNAL_CORRUPT_HOOK=1 并由应用提供。
 *
 * @param relay 触发 corrupt 的 relay 实例
 * @param header 损坏 slot 的头部指针
 */
void bm_stream_relay_corrupt_hook(bm_stream_relay_t *relay,
                                  const bm_stream_relay_slot_header_t *header);

#endif /* BM_STREAM_RELAY_H */
