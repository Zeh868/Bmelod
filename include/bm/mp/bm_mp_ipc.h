/**
 * @file bm_mp_ipc.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 扩展 RTD 单向通道语义为 `event_ring[source][target]` 矩阵；读游标保存在
 * 目标核 `endpoint[target]`，禁止函数级 static `last_seq`。
 * 阶段 2 新增 N×N cmd_ring（FIFO）与 tel_channel（seqlock 最新值）payload 通道。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            事件环增加序列异常与 drain 阻塞计数
 * 2026-06-27       1.2            zeh            新增 cmd_ring/tel_channel N×N payload 通道；layout 版本 5→6
 *
 */
#ifndef BM_MP_IPC_H
#define BM_MP_IPC_H

#include "bm/mp/bm_mp_types.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_event.h"

#ifndef BM_CONFIG_IPC_EVENT_RING_DEPTH
#define BM_CONFIG_IPC_EVENT_RING_DEPTH  8u
#endif

#ifndef BM_CONFIG_IPC_PER_SOURCE_BUDGET
#define BM_CONFIG_IPC_PER_SOURCE_BUDGET  4u
#endif

#ifndef BM_CONFIG_IPC_DRAIN_BUDGET
#define BM_CONFIG_IPC_DRAIN_BUDGET  \
    ((BM_CONFIG_CPU_COUNT > 1u) ? \
     ((BM_CONFIG_CPU_COUNT - 1u) * BM_CONFIG_IPC_PER_SOURCE_BUDGET) : 0u)
#endif

#ifndef BM_CONFIG_MP_IPC_EVENT_RING_DEPTH
#define BM_CONFIG_MP_IPC_EVENT_RING_DEPTH  BM_CONFIG_IPC_EVENT_RING_DEPTH
#endif

#ifndef BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET
#define BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET  BM_CONFIG_IPC_PER_SOURCE_BUDGET
#endif

#ifndef BM_CONFIG_MP_IPC_DRAIN_BUDGET
#define BM_CONFIG_MP_IPC_DRAIN_BUDGET  BM_CONFIG_IPC_DRAIN_BUDGET
#endif

/* ---- 阶段 2：payload 通道 config ---- */

#ifndef BM_CONFIG_MP_IPC_CMD_RING_DEPTH
/** @brief 命令 FIFO 环深度，必须为 2 的幂 */
#define BM_CONFIG_MP_IPC_CMD_RING_DEPTH  8u
#endif
#ifndef BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE
/** @brief 命令 payload 定长字节槽（阶段 5 校准） */
#define BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE  32u
#endif
#ifndef BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE
/** @brief 遥测 payload 定长字节槽（阶段 5 校准） */
#define BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE  32u
#endif

#if (BM_CONFIG_MP_IPC_CMD_RING_DEPTH & (BM_CONFIG_MP_IPC_CMD_RING_DEPTH - 1u)) != 0u
#error "BM_CONFIG_MP_IPC_CMD_RING_DEPTH must be a power of two"
#endif

#if defined(BM_CONFIG_MP_IPC_EVENT_RING_DEPTH) && \
    !defined(BM_CONFIG_IPC_EVENT_RING_DEPTH)
#define BM_CONFIG_IPC_EVENT_RING_DEPTH  BM_CONFIG_MP_IPC_EVENT_RING_DEPTH
#endif

#if defined(BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET) && \
    !defined(BM_CONFIG_IPC_PER_SOURCE_BUDGET)
#define BM_CONFIG_IPC_PER_SOURCE_BUDGET  BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET
#endif

#if defined(BM_CONFIG_MP_IPC_DRAIN_BUDGET) && \
    !defined(BM_CONFIG_IPC_DRAIN_BUDGET)
#define BM_CONFIG_IPC_DRAIN_BUDGET  BM_CONFIG_MP_IPC_DRAIN_BUDGET
#endif

#if BM_CONFIG_IPC_EVENT_RING_DEPTH < 2u
#error "BM_CONFIG_IPC_EVENT_RING_DEPTH must be at least 2"
#endif

#if BM_CONFIG_EVENT_INLINE_DATA_SIZE > 255u
#error "BM_CONFIG_EVENT_INLINE_DATA_SIZE must fit uint8_t data_len"
#endif

/** 跨核转发的事件槽（inline payload） */
typedef struct {
    bm_event_type_t      type;
    bm_event_priority_t  priority;
    uint8_t              source_id;
    uint8_t              data_len;
    uint8_t              inline_data[BM_CONFIG_EVENT_INLINE_DATA_SIZE];
    uint32_t             source_seq;
} bm_mp_ipc_event_slot_t;

typedef struct {
    bm_atomic_ipc_u32_t value;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_atomic_ipc_u32_t) % BM_CONFIG_CACHE_LINE)];
} bm_mp_ipc_cursor_t;

/** 单向 SPSC 事件环（source != target） */
typedef struct BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) {
    bm_mp_ipc_cursor_t        head;
    bm_mp_ipc_cursor_t        tail;
    bm_atomic_ipc_u32_t       next_source_seq;
    bm_atomic_ipc_u32_t       overflow_drops;
    bm_atomic_ipc_u32_t       sequence_errors;
    bm_atomic_ipc_u32_t       drain_stalls;
    bm_mp_ipc_event_slot_t    slots[BM_CONFIG_IPC_EVENT_RING_DEPTH];
} bm_mp_ipc_event_ring_t;

/** 目标核 IPC 读侧状态（电平通道 last_seq 按 source 分槽） */
typedef struct {
    uint32_t  cmd_last_seq[BM_CONFIG_CPU_COUNT];
    uint32_t  tel_last_seq[BM_CONFIG_CPU_COUNT];
    uint32_t  event_last_seq[BM_CONFIG_CPU_COUNT];
} bm_mp_ipc_endpoint_state_t;

/** @brief N×N 点对点命令 FIFO 环（SPSC，head/tail+fence，无每槽 CRC）。 */
typedef struct BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) {
    bm_mp_ipc_cursor_t  head;   /**< 写绝对游标（cache-line 隔离，复用现有 cursor 类型） */
    bm_mp_ipc_cursor_t  tail;   /**< 读绝对游标 */
    uint8_t             slots[BM_CONFIG_MP_IPC_CMD_RING_DEPTH]
                              [BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE]; /**< 定长 payload 槽 */
} bm_mp_ipc_cmd_ring_t;

/** @brief N×N 点对点遥测 seqlock 最新值通道（偶稳奇写 + CRC，复刻 bm_ipc tel channel）。 */
typedef struct {
    uint8_t             payload[BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE]; /**< 定长 payload */
    uint32_t            crc32;  /**< payload CRC32 */
    bm_atomic_ipc_u32_t seq;    /**< seqlock 序列（偶=稳定，奇=写进行中） */
} bm_mp_ipc_tel_channel_t;

/** 链接脚本放入 non-cacheable SRAM 的共享矩阵 */
typedef struct {
    uint32_t                    magic;
    uint32_t                    layout_version;
    bm_atomic_ipc_u32_t         boot_epoch;
    bm_atomic_ipc_u32_t         boot_phase;
    bm_atomic_ipc_u32_t         cpu_ready[BM_CONFIG_CPU_COUNT];
    uint32_t                    partition_crc;
    bm_mp_ipc_event_ring_t      event_ring[BM_CONFIG_CPU_COUNT]
                                         [BM_CONFIG_CPU_COUNT];
    bm_mp_ipc_endpoint_state_t  endpoint[BM_CONFIG_CPU_COUNT];
    bm_atomic_ipc_u32_t         cmd_snapshot_seq[BM_CONFIG_CPU_COUNT]
                                               [BM_CONFIG_CPU_COUNT];
    bm_atomic_ipc_u32_t         cpu_hb_seq[BM_CONFIG_CPU_COUNT];
    bm_atomic_ipc_u32_t         stream_blocks_processed[BM_CONFIG_CPU_COUNT];
    /** @brief 阶段 2：N×N 点对点命令 FIFO 环（SPSC，head/tail+fence） */
    bm_mp_ipc_cmd_ring_t        cmd_ring[BM_CONFIG_CPU_COUNT][BM_CONFIG_CPU_COUNT];
    /** @brief 阶段 2：N×N 点对点遥测 seqlock 最新值通道（偶稳奇写+CRC） */
    bm_mp_ipc_tel_channel_t     tel_channel[BM_CONFIG_CPU_COUNT][BM_CONFIG_CPU_COUNT];
} bm_mp_ipc_matrix_t;

#define BM_MP_IPC_MAGIC           0x425A5032u   /* "BZP2" */
/** layout version：5→6（新增 cmd_ring/tel_channel payload 通道） */
#define BM_MP_IPC_LAYOUT_VERSION  6u

/**
 * @brief 格式化 IPC 矩阵共享区
 *
 * @param matrix 共享区指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_mp_ipc_matrix_format(bm_mp_ipc_matrix_t *matrix);

/**
 * @brief attach 已格式化的 IPC 矩阵
 *
 * @param out 输出矩阵指针
 * @param base 共享区物理/虚拟基址
 * @return BM_OK 成功；BM_ERR_INVALID magic/layout 不匹配
 */
int bm_mp_ipc_matrix_attach(bm_mp_ipc_matrix_t **out, uintptr_t base);

/**
 * @brief 获取当前 attach 的 IPC 矩阵
 *
 * @return 矩阵指针；未 attach 时为 NULL
 */
bm_mp_ipc_matrix_t *bm_mp_ipc_matrix(void);

/**
 * @brief 将事件转发到目标核 SPSC 环
 *
 * @param target_cpu 目标 CPU
 * @param event 事件描述
 * @param data inline 载荷（可为 NULL）
 * @param len 载荷长度
 * @return BM_OK 成功；BM_ERR_OVERFLOW 环满
 */
int bm_mp_ipc_publish_event_forward(uint8_t target_cpu,
                                    const bm_event_t *event,
                                    const void *data,
                                    size_t len);

/**
 * @brief 本核按固定 source 顺序 drain 跨核事件环
 *
 * @param budget 本轮最多消费条数
 * @return 实际注入本核 event 队列的条数；负值为错误
 */
int bm_mp_ipc_drain_on_this_cpu(uint32_t budget);

/**
 * @brief `bm_mp_cpu_main` 使用的 IPC drain 入口（含 RTD 兼容层）
 *
 * @param budget 本轮预算
 * @return 实际处理条数
 */
int bm_ipc_drain_on_this_cpu(uint32_t budget);

/**
 * @brief 递增共享矩阵上的 stream 块处理计数
 */
void bm_mp_ipc_count_stream_block(uint8_t cpu);
uint32_t bm_mp_ipc_stream_blocks_processed(uint8_t cpu);

/**
 * @brief 发布命令快照代际（源核 → 目标核 doorbell）
 */
int bm_mp_ipc_publish_cmd_snapshot(uint8_t target_cpu);

/**
 * @brief 读取来自 source 的命令快照代际（目标核 endpoint 去重）
 */
int bm_mp_ipc_cmd_snapshot_read(uint8_t source,
                                uint32_t *seq_out,
                                uint32_t *last_seq_inout);

/**
 * @brief 查询 source→target 事件环溢出丢弃计数
 */
uint32_t bm_mp_ipc_event_ring_drops(uint8_t source, uint8_t target);

/**
 * @brief 跨核 IPC 序列异常故障钩子
 *
 * 当 drain 检测到 source→target 环出现序列异常（source_seq==0、回退或重复，
 * 表明跨代陈旧数据或未完成写入）时回调。硬实时剖面应注册为安全停机，
 * 避免该环静默卡死、事件永久丢失。
 *
 * @param source_cpu 出错的源 CPU
 * @param target_cpu 出错的目标 CPU（即当前 drain 核）
 */
typedef void (*bm_mp_ipc_fault_fn_t)(uint8_t source_cpu, uint8_t target_cpu);

/**
 * @brief 注册跨核 IPC 序列异常故障钩子（NULL 清除）
 */
void bm_mp_ipc_set_fault_hook(bm_mp_ipc_fault_fn_t fn);

#endif /* BM_MP_IPC_H */
