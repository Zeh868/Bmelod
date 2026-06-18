/**
 * @file bm_channel.h
 * @brief 轻量级 SPSC 数据通道
 *
 * 单生产者单消费者环形缓冲区，支持 SRT 域内非阻塞收发。
 * 禁止在 Hardware/Scheduled HRT ISR 中访问（见混合域架构规范）。
 *
 * @core_affinity 同核优先
 * 索引更新采用临界区保护，保持有界、无自旋行为。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            SIL-2 查询 API 与编译期深度校验
 *
 */
#ifndef BM_CHANNEL_H
#define BM_CHANNEL_H

#include "bm/common/bm_types.h"

#if BM_CONFIG_CPU_COUNT > 1u
#include "bm/common/bm_atomic_ipc.h"
#endif

#include <stdbool.h>
#include <stdint.h>

#ifndef BM_CONFIG_CHANNEL_MAX_CHANNELS
#define BM_CONFIG_CHANNEL_MAX_CHANNELS 4
#endif

/** 环形通道控制块 */
typedef struct {
    uint8_t *buf;
    uint32_t elem_size;
    uint32_t capacity;
#if BM_CONFIG_CPU_COUNT > 1u
    bm_atomic_ipc_u32_t write_idx;
    bm_atomic_ipc_u32_t read_idx;
#else
    volatile uint32_t write_idx;
    volatile uint32_t read_idx;
#endif
} bm_channel_t;

/** 静态定义通道实例及 backing buffer
 *  示例：BM_CHANNEL_DEFINE(my_chan, sensor_t, 8); */
#define BM_CHANNEL_DEFINE(name, type, depth) \
    typedef char _bm_channel_depth_check_##name[((depth) >= 2) ? 1 : -1]; \
    static uint8_t _bm_channel_buf_##name[(depth) * sizeof(type)] = {0}; \
    static bm_channel_t name = { \
        .buf = _bm_channel_buf_##name, \
        .elem_size = sizeof(type), \
        .capacity = (depth), \
        .write_idx = 0, \
        .read_idx = 0 \
    }

/**
 * @brief 重置通道为空
 *
 * @param ch 通道控制块指针
 */
void bm_channel_reset(bm_channel_t *ch);

/**
 * @brief 非阻塞发送单个元素（SRT 域；临界区保护）
 *
 * @param ch 通道控制块指针
 * @param data 待发送元素数据指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数或控制块异常；BM_ERR_OVERFLOW 通道已满
 */
int bm_channel_send(bm_channel_t *ch, const void *data);

/**
 * @brief 非阻塞接收单个元素（SRT 域；临界区保护）
 *
 * @param ch 通道控制块指针
 * @param data 接收缓冲区指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数或控制块异常；BM_ERR_WOULD_BLOCK 通道为空
 */
int bm_channel_recv(bm_channel_t *ch, void *data);

/**
 * @brief 查询当前队列中的元素数量
 *
 * @param ch 通道控制块指针
 * @return 当前元素个数；无效通道返回 0
 */
uint32_t bm_channel_count(const bm_channel_t *ch);

/**
 * @brief 查询通道总容量
 *
 * @param ch 通道控制块指针
 * @return 通道容量；ch 为 NULL 时返回 0
 */
static inline uint32_t bm_channel_capacity(const bm_channel_t *ch) {
    return ch ? ch->capacity : 0;
}

/**
 * @brief 判断通道是否为空（临界区快照，可用于调度决策）
 *
 * @param ch 通道控制块指针
 * @return true 为空或 ch 无效；false 非空
 */
bool bm_channel_is_empty(const bm_channel_t *ch);

/**
 * @brief 判断通道是否已满（临界区快照）
 *
 * @param ch 通道控制块指针
 * @return true 已满或无效；false 仍可写入
 */
bool bm_channel_is_full(const bm_channel_t *ch);

#endif /* BM_CHANNEL_H */
