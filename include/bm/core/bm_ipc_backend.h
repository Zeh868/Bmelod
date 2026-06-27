/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_ipc_backend.h
 * @brief bm_bus BM_BUS_IPC 模式后端接口（控制反转 vtable）
 *
 * bus core 经此接口委托跨核读写给后端（阶段 1 为 native stub，阶段 2 为 bm_mp_ipc 矩阵）。
 * 契约：后端 acquire_* 借出的是后端持有的本地对齐暂存槽（非跨核共享区指针），调用方
 * memcpy 进出暂存；后端在 commit/acquire_read 内完成共享区拷贝 + CRC + seqlock。
 * 依赖方向 backend→core，core 不引用任何跨核实现类型。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-27
 *
 * @par 修改日志:
 *    Date         Version   Author   Description
 * 2026-06-27       1.0       zeh      初稿：IPC 模式控制反转 vtable
 */
#ifndef BM_IPC_BACKEND_H
#define BM_IPC_BACKEND_H

/**
 * @brief IPC 后端函数指针表（vtable），bm_bus_bind_ipc_backend 时写入，运行期只读。
 *
 * FIFO（QUEUE 风格）与最新值（LATEST 风格）语义由后端实例自带，bus 不感知。
 */
typedef struct {
    /** @brief 借出写暂存槽（后端本地、对齐安全）。
     *  @param ctx 后端上下文。 @param slot_out 输出暂存写槽指针。
     *  @return BM_OK；BM_ERR_OVERFLOW（FIFO 满）；BM_ERR_BUSY（重入）；BM_ERR_INVALID。 */
    int (*acquire_write)(void *ctx, void **slot_out);
    /** @brief 提交：暂存→共享区 + CRC + seqlock 发布。 @return BM_OK；BM_ERR_INVALID。 */
    int (*commit)(void *ctx);
    /** @brief 放弃当前写暂存，不发布。 @return BM_OK；BM_ERR_INVALID。 */
    int (*abort)(void *ctx);
    /** @brief 借出读暂存槽：共享区→暂存 + 校验 CRC/seqlock，返回只读暂存指针。
     *  @return BM_OK；BM_ERR_WOULD_BLOCK（空/未发布/失稳）；BM_ERR_INVALID（CRC 失配）。 */
    int (*acquire_read)(void *ctx, const void **slot_out);
    /** @brief 归还读暂存（FIFO 推进读游标 / LATEST 幂等）。 @return BM_OK。 */
    int (*release)(void *ctx);
} bm_ipc_backend_iface_t;

#endif /* BM_IPC_BACKEND_H */
