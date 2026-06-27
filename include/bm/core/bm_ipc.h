/**
 * @file bm_ipc.h
 * @brief 多核 IPC 协议与共享矩阵定义
 *
 * 定义 RT/SRT 双域之间共享控制块 `bm_ipc_shared_t`，包含电平命令通道、
 * 可靠命令环、遥测通道、boot 状态与 heartbeat 计数器。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            读游标迁入共享区 reader[cpu]
 * 2026-06-15       1.2            zeh            可靠命令 drain 以 head/tail 为准
 *
 */
#ifndef BM_IPC_H
#define BM_IPC_H

#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_IPC_REL_CMD_CAPACITY
#define BM_CONFIG_IPC_REL_CMD_CAPACITY  8u
#endif

typedef struct {
    uint32_t opcode;
    uint32_t data[3];
} bm_ipc_cmd_level_t;

typedef struct {
    uint32_t command_id;
    uint32_t opcode;
} bm_ipc_rel_command_t;

typedef struct {
    uint32_t temperature_centi;
    uint32_t voltage_mv;
    uint32_t current_ma;
} bm_ipc_telemetry_t;

typedef struct {
    uint8_t             payload[sizeof(bm_ipc_cmd_level_t)];
    uint32_t            crc32;
    bm_atomic_ipc_u32_t seq;
} bm_ipc_cmd_channel_t;

typedef struct {
    uint8_t             payload[sizeof(bm_ipc_telemetry_t)];
    uint32_t            crc32;
    bm_atomic_ipc_u32_t seq;
} bm_ipc_tel_channel_t;

typedef struct {
    bm_atomic_ipc_u32_t head;
    bm_atomic_ipc_u32_t tail;
    bm_atomic_ipc_u32_t count;
    bm_ipc_rel_command_t commands[BM_CONFIG_IPC_REL_CMD_CAPACITY];
} bm_ipc_rel_cmd_ring_t;

typedef struct {
    uint32_t  cmd_last_seq[BM_CONFIG_CPU_COUNT];
    uint32_t  tel_last_seq[BM_CONFIG_CPU_COUNT];
} bm_ipc_reader_endpoint_t;

typedef struct {
    uint32_t                 magic;
    uint32_t                 layout_version;
    bm_atomic_ipc_u32_t      boot_epoch;
    bm_atomic_ipc_u32_t      boot_state;
    bm_atomic_ipc_u32_t      rt_ready;
    bm_atomic_ipc_u32_t      srt_ready;
    bm_atomic_ipc_u32_t      rt_hb_seq;
    bm_atomic_ipc_u32_t      srt_hb_seq;
    bm_ipc_cmd_channel_t     cmd;
    bm_ipc_tel_channel_t     tel;
    bm_ipc_rel_cmd_ring_t    rel_cmd;
    bm_ipc_reader_endpoint_t reader[BM_CONFIG_CPU_COUNT];
} bm_ipc_shared_t;

#define BM_IPC_MAGIC           0x425A5030u
#define BM_IPC_LAYOUT_VERSION  3u

/**
 * @brief 格式化 IPC 共享区。
 */
int bm_ipc_format(bm_ipc_shared_t *ipc);

/**
 * @brief 绑定已格式化的 IPC 共享区。
 */
int bm_ipc_attach(bm_ipc_shared_t **ipc, uintptr_t base);

/**
 * @brief 发布电平命令。
 *
 * @warning 单写者契约：电平通道是单一 seqlock，无跨核互斥。同一通道
 *          只能由一个指定核发布；多核并发发布会破坏 seqlock（两写者同时
 *          翻动序号），导致读侧读到撕裂数据。多读者由 `_read_*_from`
 *          按 source 去重，安全。确定性流式下务必保证发布核唯一。
 */
int bm_ipc_publish_cmd_level(bm_ipc_shared_t *ipc, const bm_ipc_cmd_level_t *cmd);

/**
 * @brief 读取默认来源的电平命令。
 */
int bm_ipc_read_cmd_level(bm_ipc_shared_t *ipc, bm_ipc_cmd_level_t *out);

/**
 * @brief 读取指定来源 CPU 的电平命令。
 */
int bm_ipc_read_cmd_level_from(bm_ipc_shared_t *ipc,
                               uint8_t source_cpu,
                               bm_ipc_cmd_level_t *out);

/**
 * @brief 发布可靠命令。
 */
int bm_ipc_publish_cmd_rel(bm_ipc_shared_t *ipc, const bm_ipc_rel_command_t *cmd);

/**
 * @brief 从可靠命令环中 drain 命令。
 */
int bm_ipc_drain_cmd_rel(bm_ipc_shared_t *ipc,
                         bm_ipc_rel_command_t *out,
                         uint32_t capacity,
                         uint32_t *out_count);

/**
 * @brief 发布遥测数据。
 *
 * @warning 单写者契约：同 `bm_ipc_publish_cmd_level`，遥测通道亦为单一
 *          seqlock，只能由一个指定核发布。
 */
int bm_ipc_publish_telemetry(bm_ipc_shared_t *ipc, const bm_ipc_telemetry_t *tel);

/**
 * @brief 读取默认来源的遥测数据。
 */
int bm_ipc_read_telemetry(bm_ipc_shared_t *ipc, bm_ipc_telemetry_t *out);

/**
 * @brief 读取指定来源 CPU 的遥测数据。
 */
int bm_ipc_read_telemetry_from(bm_ipc_shared_t *ipc,
                               uint8_t source_cpu,
                               bm_ipc_telemetry_t *out);

/**
 * @brief 增加 RT 心跳计数。
 */
void bm_ipc_bump_rt_hb(bm_ipc_shared_t *ipc);

/**
 * @brief 增加 SRT 心跳计数。
 */
void bm_ipc_bump_srt_hb(bm_ipc_shared_t *ipc);

/**
 * @brief 读取 RT 心跳计数。
 */
uint32_t bm_ipc_read_rt_hb(const bm_ipc_shared_t *ipc);

/**
 * @brief 读取 SRT 心跳计数。
 */
uint32_t bm_ipc_read_srt_hb(const bm_ipc_shared_t *ipc);

#endif /* BM_IPC_H */
