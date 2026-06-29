/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ipc_stub_backend.h
 * @brief BM_BUS_IPC native 测试桩后端：单进程内存模拟跨核共享区。
 */
#ifndef IPC_STUB_BACKEND_H
#define IPC_STUB_BACKEND_H

#include <stdint.h>
#include "bm/core/bm_ipc_backend.h"
#include "bm/common/bm_atomic_ipc.h"

#define IPC_STUB_MAX_CAP  8u
#define IPC_STUB_MAX_ELEM 64u

/** @brief FIFO 桩：模拟跨核 SPSC 环（暂存 + 共享环 + head/tail + 每槽 CRC）。 */
typedef struct {
    uint8_t  shared[IPC_STUB_MAX_CAP][IPC_STUB_MAX_ELEM]; /**< 模拟共享环 */
    uint32_t crc[IPC_STUB_MAX_CAP];                       /**< 每槽 CRC */
    bm_atomic_ipc_u32_t head;                             /**< 写绝对游标 */
    bm_atomic_ipc_u32_t tail;                             /**< 读绝对游标 */
    uint8_t  scratch[IPC_STUB_MAX_ELEM];                  /**< 本地读写暂存 */
    uint8_t  wr_pending;                                  /**< acquire_write 后置 1 */
    uint32_t cap;                                         /**< 环容量（可用 cap-1） */
    uint32_t elem;                                        /**< 元素字节数 */
} fifo_stub_t;

/** @brief 最新值桩：seqlock 单槽（偶稳奇写）+ CRC + 读写暂存。 */
typedef struct {
    uint8_t  shared[IPC_STUB_MAX_ELEM];
    uint32_t crc;
    bm_atomic_ipc_u32_t seq;       /**< 偶=稳定，奇=写进行中 */
    uint8_t  scratch_w[IPC_STUB_MAX_ELEM];
    uint8_t  scratch_r[IPC_STUB_MAX_ELEM];
    uint8_t  wr_pending;
    uint8_t  published;
    uint32_t elem;
} latest_stub_t;

void fifo_stub_init(fifo_stub_t *s, uint32_t elem, uint32_t cap);
void latest_stub_init(latest_stub_t *s, uint32_t elem);

extern const bm_ipc_backend_iface_t g_fifo_ipc_iface;
extern const bm_ipc_backend_iface_t g_latest_ipc_iface;

#endif /* IPC_STUB_BACKEND_H */
