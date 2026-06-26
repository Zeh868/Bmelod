/**
 * @file bm_block_backend.h
 * @brief bm_bus BLOCK 模式后端接口（控制反转 vtable）
 *
 * 定义块流后端的函数指针表（vtable）与绑定描述符。bus core 层通过此接口
 * 委托 BLOCK 模式的产消操作给上层（如 hybrid 层的 bm_stream adapter），
 * 依赖方向保持 hybrid → core，core 层不引用任何 hybrid 类型。
 *
 * 设计契约：
 *   - block 以 `void *` 不透明指针传递，core 层不解引用其内容；
 *   - 时间戳以裸 `uint64_t ts_ns`（纳秒）传递，core 层不引用 bm_timestamp_t；
 *   - BLOCK = SPSC 单读者（继承 bm_stream 语义），由上层后端保证；
 *   - owner_cpu 由 bus 透传给后端，后端按需校验；
 *   - 后端未绑定时调用 BLOCK 入口返回 BM_ERR_INVALID。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            初稿：BLOCK 模式控制反转 vtable
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_BLOCK_BACKEND_H
#define BM_BLOCK_BACKEND_H

#include <stdint.h>

/**
 * @brief BLOCK 后端函数指针表（vtable）
 *
 * 所有函数指针在 bm_bus_bind_block_backend 时写入，运行期只读。
 * bus 在 BLOCK 入口仅做参数校验 + 透传，不解释 block 内容。
 *
 * produce / consume 命名成对，与 bm_stream API 风格对齐：
 *   producer_acquire  → 借出空闲块（DMA_OWNED 状态），由生产者填充
 *   producer_commit   → 提交块（DMA_OWNED → READY），携带有效字节数与时间戳
 *   producer_abort    → 放弃已借块（DMA_OWNED → FREE）
 *   consumer_acquire  → 借出最旧 READY 块，由消费者读取
 *   consumer_release  → 归还块（PROCESSING → FREE）
 */
typedef struct {
    /**
     * @brief 生产者借出空闲块
     *
     * @param ctx       后端上下文（bm_stream_t * 或其他后端实例）
     * @param block_out 输出：不透明 block 指针（失败时置 NULL）
     * @return BM_OK 成功；BM_ERR_OVERFLOW 无空闲块；BM_ERR_INVALID 参数非法
     */
    int (*producer_acquire)(void *ctx, void **block_out);

    /**
     * @brief 生产者提交块（发布数据）
     *
     * @param ctx         后端上下文
     * @param block       由 producer_acquire 借出的块指针
     * @param valid_bytes 有效数据字节数（不超过块容量）
     * @param ts_ns       时间戳（纳秒，单调时钟），0 表示无时间戳
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法或块状态不符
     */
    int (*producer_commit)(void *ctx, void *block,
                           uint32_t valid_bytes, uint64_t ts_ns);

    /**
     * @brief 生产者放弃已借块（不发布）
     *
     * @param ctx   后端上下文
     * @param block 由 producer_acquire 借出的块指针
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法或块状态不符
     */
    int (*producer_abort)(void *ctx, void *block);

    /**
     * @brief 消费者借出最旧 READY 块
     *
     * @param ctx       后端上下文
     * @param block_out 输出：不透明 block 指针（失败时置 NULL）
     * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 无 READY 块；BM_ERR_INVALID 参数非法
     */
    int (*consumer_acquire)(void *ctx, void **block_out);

    /**
     * @brief 消费者归还块
     *
     * @param ctx   后端上下文
     * @param block 由 consumer_acquire 借出的块指针
     * @return BM_OK 成功；BM_ERR_INVALID 参数非法或块状态不符
     */
    int (*consumer_release)(void *ctx, void *block);
} bm_block_backend_iface_t;

#endif /* BM_BLOCK_BACKEND_H */
