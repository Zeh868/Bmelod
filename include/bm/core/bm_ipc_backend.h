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
 *
 * @par 后端实现必须遵守的确定性契约（Determinism Contract）
 *
 * 所有函数指针所指向的实现**必须**满足以下三条约束，违反即破坏整个框架的
 * 确定性保证（WCET 可分析性）：
 *
 * 1. **有界（Bounded）**：执行路径有静态上界（trip count 来自编译期常量或
 *    已校验的运行期上限），禁止出现以数据为界的无上限循环或递归。
 *
 * 2. **非阻塞（Non-blocking）**：满/空/失稳等异常情况须立即以错误码返回
 *    （BM_ERR_OVERFLOW / BM_ERR_WOULD_BLOCK / BM_ERR_BUSY），禁止忙等待、
 *    睡眠或无界自旋，确保调用方下一 tick 重取策略有效。
 *
 * 3. **WCET 可静态分析（Statically Analyzable WCET）**：后端的最坏执行时间
 *    须可在离线 RTA 阶段静态确定，并纳入调用方所在执行槽的 WCET 预算；
 *    典型边界：memcpy(payload_size) + fence + seqlock/ring 游标操作，
 *    payload_size 须为编译期已知常量。
 */
typedef struct {
    /**
     * @brief 借出写暂存槽（后端本地、对齐安全，非跨核共享区指针）。
     *
     * 后端契约：O(1)，非阻塞。满（FIFO）或重入时立即返回错误码，不自旋。
     * @param ctx      后端上下文。
     * @param slot_out 输出暂存写槽指针（供调用方 memcpy 填入 payload）。
     * @return BM_OK；BM_ERR_OVERFLOW（FIFO 满）；BM_ERR_BUSY（重入）；BM_ERR_INVALID。
     */
    int (*acquire_write)(void *ctx, void **slot_out);

    /**
     * @brief 提交：暂存 → 共享区 + CRC/fence + seqlock 发布。
     *
     * 后端契约：WCET = memcpy(payload_size) + CRC(payload_size) + 原子操作，
     * 均为编译期已知常量界，可纳入 RTA 预算。非阻塞，无等待。
     * @param ctx 后端上下文。
     * @return BM_OK；BM_ERR_INVALID（无未决写）。
     */
    int (*commit)(void *ctx);

    /**
     * @brief 放弃当前写暂存，不发布。
     *
     * 后端契约：O(1)，仅清写挂起标记，无内存操作，WCET 固定。
     * @param ctx 后端上下文。
     * @return BM_OK；BM_ERR_INVALID。
     */
    int (*abort)(void *ctx);

    /**
     * @brief 借出读暂存槽：共享区 → 暂存 + CRC/seqlock 校验，返回只读暂存指针。
     *
     * 后端契约：O(1)，非阻塞——未发布/写进行中/seqlock 失稳时立即返回
     * BM_ERR_WOULD_BLOCK，禁止自旋重试（调用方须在下一 tick 重取）。
     * WCET = 原子读 + memcpy(payload_size) + CRC(payload_size) + fence，均有界。
     * @param ctx      后端上下文。
     * @param slot_out 输出只读读暂存指针。
     * @return BM_OK；BM_ERR_WOULD_BLOCK（空/未发布/失稳）；BM_ERR_INVALID（CRC 失配）。
     */
    int (*acquire_read)(void *ctx, const void **slot_out);

    /**
     * @brief 归还读暂存：FIFO 推进读游标；LATEST 幂等（无游标）。
     *
     * 后端契约：O(1)，原子写游标或直接返回，WCET 固定。
     * @param ctx 后端上下文。
     * @return BM_OK。
     */
    int (*release)(void *ctx);
} bm_ipc_backend_iface_t;

#endif /* BM_IPC_BACKEND_H */
