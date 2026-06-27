/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_mp_ipc_backend.h
 * @brief matrix payload 通道 → bm_ipc_backend_iface_t adapter。
 *
 * 把某 (source→target) 的 cmd_ring（FIFO）/tel_channel（LATEST）暴露为 bus IPC 后端。
 * adapter 自包含：仅操作 matrix struct 新通道字段，依赖 bm_atomic_ipc + bm_crc32，
 * 不引用 matrix.c 的多核 HAL（cpu/boot/drain）。ctx 显式带 source/target，
 * native 单进程可测双向。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-27       1.0            zeh            阶段 2：matrix payload 通道 adapter 头文件
 *
 */
#ifndef BM_MP_IPC_BACKEND_H
#define BM_MP_IPC_BACKEND_H

#include <stdint.h>
#include "bm/mp/bm_mp_ipc.h"
#include "bm/core/bm_ipc_backend.h"

/**
 * @brief adapter 上下文：定位某 (source→target) 通道 + 本地暂存。
 *
 * source/target 确定操作哪个 cmd_ring[source][target] 或 tel_channel[source][target]。
 * scratch/scratch_r 为本地对齐暂存，热路径定长 memcpy（实参 ctx->elem 字节）。
 */
typedef struct {
    bm_mp_ipc_matrix_t *m;       /**< 共享矩阵指针 */
    uint8_t             source;  /**< 源核编号（写端） */
    uint8_t             target;  /**< 目标核编号（读端） */
    uint32_t            elem;    /**< payload 字节数（<= 对应通道 PAYLOAD_SIZE） */
    /** @brief 写暂存：供 acquire_write 借出，commit 时拷入共享区 */
    uint8_t  scratch[BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE > BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE \
                     ? BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE : BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE];
    /** @brief 读暂存：acquire_read 时从共享区拷出后借出给调用方 */
    uint8_t  scratch_r[BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE > BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE \
                       ? BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE : BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE];
    uint8_t  wr_pending; /**< 1=已 acquire_write 待 commit/abort，0=空闲 */
} bm_mp_ipc_backend_ctx_t;

/**
 * @brief 初始化 adapter ctx，绑定到矩阵 m 的 (source→target) 通道。
 *
 * @param ctx    adapter 上下文指针（调用方分配）
 * @param m      已格式化的共享矩阵指针
 * @param source 源核编号（须 < BM_CONFIG_CPU_COUNT）
 * @param target 目标核编号（须 < BM_CONFIG_CPU_COUNT）
 * @param elem   payload 字节数（须 > 0，且 ≤ 对应通道 PAYLOAD_SIZE）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_mp_ipc_backend_open(bm_mp_ipc_backend_ctx_t *ctx, bm_mp_ipc_matrix_t *m,
                            uint8_t source, uint8_t target, uint32_t elem);

/** @brief FIFO 后端 vtable：映射 cmd_ring[source][target]（SPSC 保序队列）。 */
extern const bm_ipc_backend_iface_t g_mp_ipc_fifo_iface;

/** @brief LATEST 后端 vtable：映射 tel_channel[source][target]（seqlock 最新值）。 */
extern const bm_ipc_backend_iface_t g_mp_ipc_latest_iface;

#endif /* BM_MP_IPC_BACKEND_H */
