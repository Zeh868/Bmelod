/**
 * @file bm_stream.h
 * @brief 静态零拷贝块流（单生产者单消费者）
 *
 * 与 bm_channel 分离：服务 DMA 大块所有权与 Block/Frame RT，禁止在 SRT 中
 * 误用为消息队列。ISR 仅提交 descriptor，不复制 payload。状态迁移为
 * FREE → DMA_OWNED → READY → PROCESSING → FREE（及 OUTPUT 路径）。
 *
 * @core_affinity owner_cpu 约束
 * 每个 bm_stream 实例绑定到创建时指定的 owner_cpu，仅对应 CPU 可调用
 * producer/consumer API。
 * 传输路径由上层调度完成，不在此头文件暴露具体转发细节。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            commit/drain 解耦；owner_cpu / pending_drain
 * 2026-07-27       1.2            zeh            将 BM_STREAM_* 静态分配宏迁到 bm_stream_impl.h
 * 2026-07-27       1.3            zeh            struct bm_stream 下沉到 .c，头文件改为不透明指针 + accessor
 * 2026-07-28       1.4            zeh            accessor 声明补 Doxygen 中文注释（含 NULL 入参语义）
 * 2026-08-01       1.5            zeh            明确 bm_stream_drain 的 budget=
 *                                                最大 ready 通知次数（非消费块数）
 * 2026-08-01       1.5            zeh           补齐 Doxygen 合规元数据
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_STREAM_H
#define BM_STREAM_H

#include "bm/hybrid/bm_block.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/core/bm_block_backend.h"

#include <stdint.h>

#ifndef BM_CONFIG_STREAM_MAX_BLOCKS
#define BM_CONFIG_STREAM_MAX_BLOCKS 4u
#endif

typedef enum {
    BM_STREAM_POLICY_DROP_NEWEST = 0,
    BM_STREAM_POLICY_DROP_OLDEST
} bm_stream_policy_t;

typedef struct {
    uint32_t overrun;
    uint32_t underrun;
    uint32_t drop;
    uint32_t late;
    uint32_t corrupt;
} bm_stream_stats_t;

typedef struct bm_stream bm_stream_t;

typedef void (*bm_stream_ready_fn_t)(bm_stream_t *stream,
                                     bm_block_t *block,
                                     void *context);

/* -------------------------------------------------------------------------
 * 内部字段 accessor（bm_stream 已改为不透明结构体）
 *
 * 统一约定：getter 入参 stream 为 NULL 时不解引用，返回下述文档的安全
 * 默认值；setter 入参 stream 为 NULL 时静默返回，不断言不崩溃。
 * ------------------------------------------------------------------------- */

/**
 * @brief 读取 block 描述符数组指针
 * @param stream stream 实例，NULL 时返回 NULL
 * @return block 数组指针；stream 为 NULL 时返回 NULL
 */
bm_block_t *bm_stream_blocks(const bm_stream_t *stream);
/**
 * @brief 读取当前块数
 * @param stream stream 实例，NULL 时返回 0
 * @return 块数；stream 为 NULL 时返回 0u
 */
uint32_t    bm_stream_block_count(const bm_stream_t *stream);
/**
 * @brief 读取块容量
 * @param stream stream 实例，NULL 时返回 0
 * @return 块容量；stream 为 NULL 时返回 0u
 */
uint32_t    bm_stream_block_capacity(const bm_stream_t *stream);
/**
 * @brief 读取过载策略
 * @param stream stream 实例，NULL 时返回默认策略
 * @return 过载策略；stream 为 NULL 时返回 BM_STREAM_POLICY_DROP_NEWEST
 */
bm_stream_policy_t bm_stream_policy_value(const bm_stream_t *stream);
/**
 * @brief 读取运行统计（只读）
 * @param stream stream 实例
 * @return 统计结构体指针；stream 为 NULL、未初始化或非本 CPU 所有时返回 NULL
 */
const bm_stream_stats_t *bm_stream_stats(const bm_stream_t *stream);
/**
 * @brief 读取 on_ready 回调指针
 * @param stream stream 实例，NULL 时返回 NULL
 * @return on_ready 回调；stream 为 NULL 时返回 NULL
 */
bm_stream_ready_fn_t bm_stream_on_ready(const bm_stream_t *stream);
/**
 * @brief 读取 on_ready 回调上下文
 * @param stream stream 实例，NULL 时返回 NULL
 * @return 回调上下文；stream 为 NULL 时返回 NULL
 */
void       *bm_stream_on_ready_context(const bm_stream_t *stream);
/**
 * @brief 读取初始化标志
 * @param stream stream 实例，NULL 时返回 0
 * @return 非 0 表示已初始化；stream 为 NULL 时返回 0
 */
int         bm_stream_initialized(const bm_stream_t *stream);
/**
 * @brief 读取下一提交序号
 * @param stream stream 实例，NULL 时返回 0
 * @return 序号值；stream 为 NULL 时返回 0u
 */
uint32_t    bm_stream_next_sequence(const bm_stream_t *stream);
/**
 * @brief 读取归属 CPU
 * @param stream stream 实例，NULL 时返回 0
 * @return owner_cpu；stream 为 NULL 时返回 0u
 */
uint8_t     bm_stream_owner_cpu(const bm_stream_t *stream);
/**
 * @brief 读取 pending_drain 标志
 * @param stream stream 实例，NULL 时返回 0
 * @return pending_drain；stream 为 NULL 时返回 0u
 */
uint8_t     bm_stream_pending_drain(const bm_stream_t *stream);

/**
 * @brief 设置 block 描述符数组指针
 * @param stream stream 实例，NULL 时静默返回
 * @param blocks block 数组指针
 */
void bm_stream_set_blocks(bm_stream_t *stream, bm_block_t *blocks);
/**
 * @brief 设置当前块数
 * @param stream stream 实例，NULL 时静默返回
 * @param count 块数
 */
void bm_stream_set_block_count(bm_stream_t *stream, uint32_t count);
/**
 * @brief 设置块容量
 * @param stream stream 实例，NULL 时静默返回
 * @param cap 块容量
 */
void bm_stream_set_block_capacity(bm_stream_t *stream, uint32_t cap);
/**
 * @brief 设置过载策略（仅 init 前可调；非法枚举值静默忽略）
 * @param stream stream 实例，NULL 时静默返回
 * @param policy BM_STREAM_POLICY_DROP_NEWEST / BM_STREAM_POLICY_DROP_OLDEST
 */
void bm_stream_set_policy(bm_stream_t *stream, bm_stream_policy_t policy);
/**
 * @brief 设置 on_ready 回调（仅 init 前可调）
 * @param stream stream 实例，NULL 时静默返回
 * @param handler 回调；默认剖面下非 NULL handler 被拒绝并告警
 *        （统一走 bm_exec_drain_streams），仅显式 legacy 剖面接受
 * @param context 回调上下文
 */
void bm_stream_set_ready_handler(bm_stream_t *stream,
                                 bm_stream_ready_fn_t handler,
                                 void *context);
/**
 * @brief 设置初始化标志
 * @param stream stream 实例，NULL 时静默返回
 * @param initialized 非 0 表示已初始化
 */
void bm_stream_set_initialized(bm_stream_t *stream, int initialized);
/**
 * @brief 设置下一提交序号
 * @param stream stream 实例，NULL 时静默返回
 * @param seq 序号值
 */
void bm_stream_set_next_sequence(bm_stream_t *stream, uint32_t seq);
/**
 * @brief 设置归属 CPU
 * @param stream stream 实例，NULL 时静默返回
 * @param cpu owner_cpu
 */
void bm_stream_set_owner_cpu(bm_stream_t *stream, uint8_t cpu);
/**
 * @brief 设置 pending_drain 标志
 * @param stream stream 实例，NULL 时静默返回
 * @param pending pending_drain 值
 */
void bm_stream_set_pending_drain(bm_stream_t *stream, uint8_t pending);

/**
 * @deprecated 下一轮发布删除，请改用 accessor API
 * 这些宏仅作为外部已发布用户的临时过渡。
 */
#define BM_STREAM_DEPRECATED_BLOCKS(s)         bm_stream_blocks(s)
#define BM_STREAM_DEPRECATED_BLOCK_COUNT(s)    bm_stream_block_count(s)
#define BM_STREAM_DEPRECATED_BLOCK_CAPACITY(s) bm_stream_block_capacity(s)
#define BM_STREAM_DEPRECATED_POLICY(s)         bm_stream_policy_value(s)
#define BM_STREAM_DEPRECATED_ON_READY(s)       bm_stream_on_ready(s)
#define BM_STREAM_DEPRECATED_ON_READY_CTX(s)   bm_stream_on_ready_context(s)
#define BM_STREAM_DEPRECATED_INITIALIZED(s)    bm_stream_initialized(s)
#define BM_STREAM_DEPRECATED_NEXT_SEQUENCE(s)  bm_stream_next_sequence(s)
#define BM_STREAM_DEPRECATED_OWNER_CPU(s)      bm_stream_owner_cpu(s)
#define BM_STREAM_DEPRECATED_PENDING_DRAIN(s)  bm_stream_pending_drain(s)

/**
 * @brief 累加流的 deadline 迟到计数
 * @param stream 流实例；无效或非本核实例时静默返回
 */
void bm_stream_mark_late(bm_stream_t *stream);

/**
 * @brief 初始化静态块流及其载荷区
 * @param stream 流实例
 * @param payloads 连续载荷缓冲区
 * @param block_count 块数量
 * @param block_bytes 每块容量字节数
 * @return BM_OK 成功；BM_ERR_INVALID 参数、容量或归属核无效
 */
int bm_stream_init(bm_stream_t *stream,
                   void *payloads,
                   uint32_t block_count,
                   uint32_t block_bytes);

/**
 * @brief 重置流中全部块、序号和统计状态
 * @param stream 流实例；NULL 或无效实例时静默返回
 */
void bm_stream_reset(bm_stream_t *stream);

/**
 * @brief 查询 READY 状态的块数量
 * @param stream 流实例
 * @return READY 块数；无效或非本核实例时返回 0
 */
uint32_t bm_stream_ready_count(const bm_stream_t *stream);

/**
 * @brief 生产者获取一个 FREE 块并转为 DMA_OWNED
 * @param stream 流实例
 * @param block 输出块指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 无可用块
 */
int bm_stream_producer_acquire(bm_stream_t *stream, bm_block_t **block);

/**
 * @brief 生产者提交 DMA_OWNED 块并发布为 READY
 * @param stream 流实例
 * @param block 待提交块
 * @param valid_bytes 有效载荷字节数
 * @param timestamp 时间戳；NULL 时保留零值
 * @return BM_OK 成功；BM_ERR_INVALID 参数、块状态或长度无效；其他为 cache 平台错误码
 */
int bm_stream_producer_commit(bm_stream_t *stream,
                             bm_block_t *block,
                             uint32_t valid_bytes,
                             const bm_timestamp_t *timestamp);

/**
 * @brief 取消已获取但未提交的生产块
 * @param stream 流实例
 * @param block 待取消块
 * @return BM_OK 成功；BM_ERR_INVALID 参数或块状态无效
 */
int bm_stream_producer_abort(bm_stream_t *stream, bm_block_t *block);

/**
 * @brief 消费者获取最旧 READY 块并转为 PROCESSING
 * @param stream 流实例
 * @param block 输出块指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_WOULD_BLOCK 无 READY 块；
 *         其他为 cache 平台错误码
 */
int bm_stream_consumer_acquire(bm_stream_t *stream, bm_block_t **block);

/**
 * @brief 消费者释放处理完成的块并转为 FREE
 * @param stream 流实例
 * @param block 待释放块
 * @return BM_OK 成功；BM_ERR_INVALID 参数或块状态无效
 */
int bm_stream_consumer_release(bm_stream_t *stream, bm_block_t *block);

/**
 * @brief 输出生产者获取 FREE 块并转为 DMA_OWNED
 * @param stream 流实例
 * @param block 输出块指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 无可用块
 */
int bm_stream_output_acquire(bm_stream_t *stream, bm_block_t **block);

/**
 * @brief 提交输出块并转为 OUTPUT_READY
 * @param stream 流实例
 * @param block 待提交块
 * @param valid_bytes 有效载荷字节数
 * @param timestamp 时间戳；NULL 时保留零值
 * @return BM_OK 成功；BM_ERR_INVALID 参数、块状态或长度无效；其他为 cache 平台错误码
 */
int bm_stream_output_commit(bm_stream_t *stream,
                            bm_block_t *block,
                            uint32_t valid_bytes,
                            const bm_timestamp_t *timestamp);

/**
 * @brief 主循环 drain：可经 ready 回调通知
 *
 * @param stream 流实例
 * @param budget 本轮最大 ready **通知次数**（不是“最大消费块数”）。
 *        handler 若未消费块（仍留在 READY/PROCESSING 或被回写为 READY），
 *        可对同一块重复通知直至本轮 budget 耗尽。
 * @return 实际调用 ready 回调的次数
 * @note 循环语义有意不因未消费而提前 break：budget 计量的是通知次数；
 *       需要“最多消费 N 块”语义时请走 bm_exec_drain_streams /
 *       bm_stream_consumer_acquire。
 */
int bm_stream_drain(bm_stream_t *stream, uint32_t budget);

/* =========================================================================
 * bm_bus BLOCK 模式适配器
 * ========================================================================= */

/**
 * @brief 获取 bm_stream 的 bm_block_backend_iface_t vtable 指针
 *
 * 返回指向全局静态 vtable 的指针，供 bm_bus BLOCK 模式通过
 * bm_bus_bind_block_backend 绑定，以 bm_stream_t * 作为 ctx 传入。
 *
 * 用法示例：
 * @code
 * bm_bus_bind_block_backend(&h_block,
 *                            bm_stream_as_block_backend(),
 *                            &my_stream);
 * @endcode
 *
 * @note bm_stream 本体零改动；adapter 实现在 bm_stream_block_adapter.c。
 *
 * @return 指向全局静态 bm_block_backend_iface_t 的常量指针
 */
const bm_block_backend_iface_t *bm_stream_as_block_backend(void);

#endif /* BM_STREAM_H */
