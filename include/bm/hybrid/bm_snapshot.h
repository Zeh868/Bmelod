/**
 * @file bm_snapshot.h
 * @brief 三缓冲无锁快照（header-only）
 *
 * 写者发布、读者拷贝，通过 published/reading 索引避免读写竞争。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_SNAPSHOT_H
#define BM_SNAPSHOT_H

#include "bm/common/bm_atomic_ipc.h"

#include <stdint.h>

/** reading 槽位空闲标记 */
#define BM_SNAPSHOT_NONE UINT32_MAX

/** 定义快照盒类型（含 3 份 buffer）
 *  示例：BM_SNAPSHOT_DEFINE(my_snap, sensor_data_t); */
#define BM_SNAPSHOT_DEFINE(name, type) \
    typedef struct { \
        bm_atomic_ipc_u32_t published; \
        bm_atomic_ipc_u32_t reading; \
        type buffer[3]; \
    } name##_snapshot_t

#define BM_SNAPSHOT_INITIALIZER \
    { .published = 0u, .reading = BM_SNAPSHOT_NONE, .buffer = { 0 } }

/**
 * @brief 选取可写入的 buffer 索引（避开 published 与 reading）
 *
 * @param published 当前已发布 buffer 索引
 * @param reading 当前读者正在读取的 buffer 索引
 * @return 可用于写入的 buffer 索引
 */
static inline uint32_t bm_snapshot_choose_buffer(uint32_t published,
                                                 uint32_t reading) {
    uint32_t i;

    for (i = 0u; i < 3u; ++i) {
        if (i != published && i != reading) {
            return i;
        }
    }
    return 0u;
}

/** 发布快照（写者侧） */
#define BM_SNAPSHOT_PUBLISH(box, value_ptr) do { \
    uint32_t _p = bm_atomic_ipc_load_u32(&(box).published); \
    uint32_t _r = bm_atomic_ipc_load_u32(&(box).reading); \
    uint32_t _w = bm_snapshot_choose_buffer(_p, _r); \
    (box).buffer[_w] = *(value_ptr); \
    bm_atomic_ipc_store_u32(&(box).published, _w); \
} while (0)

/** 读取快照（读者侧，一致性拷贝） */
#define BM_SNAPSHOT_READ(box, out_ptr) do { \
    uint32_t _p; \
    do { \
        _p = bm_atomic_ipc_load_u32(&(box).published); \
        bm_atomic_ipc_store_u32(&(box).reading, _p); \
    } while (bm_atomic_ipc_load_u32(&(box).published) != _p); \
    *(out_ptr) = (box).buffer[_p]; \
    bm_atomic_ipc_store_u32(&(box).reading, BM_SNAPSHOT_NONE); \
} while (0)

#endif /* BM_SNAPSHOT_H */
