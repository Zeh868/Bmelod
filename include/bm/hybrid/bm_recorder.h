/**
 * @file bm_recorder.h
 * @brief 同核录波环原语（黑匣子）
 *
 * 同核单生产者单消费者（SPSC）：生产者（HRT/ISR）单写、覆盖最旧、保留
 * capacity 帧历史；消费者（SRT）单读异步导出。生产者写满槽位后发布索引、
 * 消费者读取并做 Lamport 式撕裂读校验，保证"要么拿到一致帧、要么记一次
 * 丢弃"，绝不返回半写帧。生产者与消费者须运行于同一核。
 * 与 bm_bus LATEST（只留最新值）、bm_stream（DMA 块所有权）定位不同：
 * 需要"一段历史帧、ISR 录、SRT 导"时用本原语。
 *
 * @core_affinity 生产者与消费者固定在 owner_cpu（同核 SPSC）。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            zeh            正式发布
 * 2026-06-21       1.1            zeh            收敛为同核 SPSC 实现
 *
 */
#ifndef BM_RECORDER_H
#define BM_RECORDER_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 录波环控制块 */
typedef struct {
    uint8_t *buf;        /**< 调用方提供的帧数组（capacity*elem_size 字节） */
    uint32_t elem_size;  /**< 单帧字节数 */
    uint32_t capacity;   /**< 帧数，必须为 2 的幂且 >=2 */
    uint32_t mask;       /**< capacity-1，用于环绕取模 */
    volatile uint32_t head;   /**< 自由递增写计数（仅生产者改） */
    volatile uint32_t tail;   /**< 自由递增读计数（仅消费者改） */
    uint32_t dropped;    /**< 累计因覆盖被消费者丢弃的帧数（SRT 侧维护） */
    uint8_t  owner_cpu;  /**< 生产者与消费者所在 CPU（约束读写只在该核调用） */
} bm_recorder_t;

/**
 * @brief 静态定义录波环实例及 backing buffer（编译期校验 depth 为 2 的幂）
 *
 * depth 必须满足 depth>=2 且为 2 的幂，否则触发负数组下标编译错误。
 * 示例：BM_RECORDER_DEFINE(foc_rec, foc_bb_frame_t, 1024);
 *
 * @param name  实例标识符
 * @param type  单帧类型
 * @param depth 帧深（2 的幂，>=2）
 */
#define BM_RECORDER_DEFINE(name, type, depth)                                  \
    typedef char _bm_rec_chk_##name[((depth) >= 2 &&                           \
        (((depth) & ((depth) - 1u)) == 0u)) ? 1 : -1];                         \
    static uint8_t _bm_rec_buf_##name[(depth) * sizeof(type)];                 \
    static bm_recorder_t name = { _bm_rec_buf_##name, sizeof(type),            \
        (depth), (depth) - 1u, 0, 0, 0u, 0u }

/**
 * @brief 运行时初始化录波环
 *
 * @param r         控制块指针
 * @param buf       调用方提供的帧数组（至少 capacity*elem_size 字节）
 * @param elem_size 单帧字节数（须 >0）
 * @param capacity  帧数（须为 2 的幂且 >=2）
 * @param owner_cpu 生产者与消费者所在 CPU（同核 SPSC）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法（buf/elem_size 为空、capacity
 *         非 2 的幂或 <2）
 */
int bm_recorder_init(bm_recorder_t *r, void *buf, uint32_t elem_size,
                     uint32_t capacity, uint8_t owner_cpu);

/**
 * @brief 重置录波环为空（SRT 域）
 *
 * @param r 控制块指针
 */
void bm_recorder_reset(bm_recorder_t *r);

/**
 * @brief 录入一帧（HRT/ISR 安全，生产者单写，覆盖最旧、不阻塞、不检查满）
 *
 * 流程：memcpy 帧体到 buf[head&mask] → 编译器屏障 → 发布 head（head+1）。
 * 无锁、无 log、无 alloc，有界 O(elem_size)。当消费者落后时直接覆盖最旧帧，
 * 丢弃由消费者侧统计。
 *
 * @param r     控制块指针
 * @param frame 待录入帧（elem_size 字节）
 */
void bm_recorder_push(bm_recorder_t *r, const void *frame);

/**
 * @brief 取出一帧（SRT 域，单读者，撕裂读校验）
 *
 * 采用 Lamport 式非阻塞读：读 head 求可读帧数；落后被覆盖则跳到
 * 最旧有效帧并累加 dropped；拷贝后再读一次 head，若拷贝期间该槽已被覆盖则
 * 作废本帧、累加 dropped、重试一次。保证绝不返回撕裂帧。
 *
 * @param r   控制块指针
 * @param out 接收缓冲区（至少 elem_size 字节）
 * @return 1 取得一致帧；0 无数据或本次读全被覆盖
 */
int bm_recorder_pop(bm_recorder_t *r, void *out);

/**
 * @brief 批量取出并回调（SRT 域，节流导出用）
 *
 * 循环 pop 最多 budget 帧（budget==0 表示取空）；sink 非空时每帧回调一次。
 *
 * @param r      控制块指针
 * @param sink   每帧回调（可为 NULL，仅丢弃）；f 指向帧体，ctx 为透传上下文
 * @param ctx    透传给 sink 的上下文
 * @param budget 本次最多处理帧数（0=取空）
 * @return 实际处理（成功 pop）的帧数
 */
uint32_t bm_recorder_drain(bm_recorder_t *r,
                           void (*sink)(const void *f, void *ctx), void *ctx,
                           uint32_t budget);

/**
 * @brief 查询当前可读帧数（head-tail，封顶 capacity）
 *
 * @param r 控制块指针
 * @return 可读帧数；r 无效返回 0
 */
uint32_t bm_recorder_count(const bm_recorder_t *r);

/**
 * @brief 查询累计丢帧数（持续溢出可见）
 *
 * @param r 控制块指针
 * @return 累计丢弃帧数；r 无效返回 0
 */
uint32_t bm_recorder_dropped(const bm_recorder_t *r);

#ifdef __cplusplus
}
#endif

#endif /* BM_RECORDER_H */
