/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_recorder.c
 * @brief 同核录波环原语实现
 *
 * 同核单生产者单消费者（SPSC）：生产者（HRT/ISR）单写、覆盖最旧、发布索引；
 * 消费者（SRT）单读 + Lamport 式撕裂读校验。32 位自由递增计数自然回绕，
 * 无符号减法在回绕下仍给出正确的"已写帧数差"，无需取模即可定位槽位。
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
#include "bm/hybrid/bm_recorder.h"

#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
/** 编译器屏障：防止编译器跨此点重排访存（同核无需硬件栅栏）。 */
#define BM_REC_BARRIER() _ReadWriteBarrier()
#else
/** 编译器屏障：防止编译器跨此点重排访存（同核无需硬件栅栏）。 */
#define BM_REC_BARRIER() __asm__ volatile("" ::: "memory")
#endif

/**
 * @brief 读取索引
 *
 * @param p 索引地址
 * @return 当前索引值
 */
static inline uint32_t bm_rec_load(const volatile uint32_t *p) {
    return *p;
}

/**
 * @brief 写入索引
 *
 * @param p 索引地址
 * @param v 新值
 */
static inline void bm_rec_store(volatile uint32_t *p, uint32_t v) {
    *p = v;
}

/**
 * @brief 校验 capacity 是否为 2 的幂且 >=2
 *
 * @param capacity 待校验帧数
 * @return 非 0 合法；0 非法
 */
static inline int bm_rec_capacity_ok(uint32_t capacity) {
    return (capacity >= 2u) && ((capacity & (capacity - 1u)) == 0u);
}

int bm_recorder_init(bm_recorder_t *r, void *buf, uint32_t elem_size,
                     uint32_t capacity, uint8_t owner_cpu) {
    if (r == NULL || buf == NULL || elem_size == 0u ||
        !bm_rec_capacity_ok(capacity)) {
        return BM_ERR_INVALID;
    }
    r->buf = (uint8_t *)buf;
    r->elem_size = elem_size;
    r->capacity = capacity;
    r->mask = capacity - 1u;
    bm_rec_store(&r->head, 0u);
    bm_rec_store(&r->tail, 0u);
    r->dropped = 0u;
    r->owner_cpu = owner_cpu;
    return BM_OK;
}

void bm_recorder_reset(bm_recorder_t *r) {
    if (r == NULL) {
        return;
    }
    bm_rec_store(&r->head, 0u);
    bm_rec_store(&r->tail, 0u);
    r->dropped = 0u;
}

void bm_recorder_push(bm_recorder_t *r, const void *frame) {
    uint32_t head;
    uint8_t *slot;

    if (r == NULL || frame == NULL) {
        return;
    }
    /* 生产者单写：head 仅本函数推进，普通读即可取当前值。 */
    head = bm_rec_load(&r->head);
    slot = r->buf + (size_t)(head & r->mask) * r->elem_size;
    memcpy(slot, frame, r->elem_size);
    /* 帧体先于索引可见：编译器屏障后再发布 head，防止两者被重排。 */
    BM_REC_BARRIER();
    bm_rec_store(&r->head, head + 1u);
}

int bm_recorder_pop(bm_recorder_t *r, void *out) {
    uint32_t h0;
    uint32_t tail;
    uint32_t avail;
    uint32_t h1;
    const uint8_t *slot;
    int retried = 0;

    if (r == NULL || out == NULL) {
        return 0;
    }

    for (;;) {
        /* 步骤 1：读 head，求可读帧数。无符号减法回绕安全。 */
        h0 = bm_rec_load(&r->head);
        tail = r->tail; /* tail 仅消费者改，普通读 */
        avail = h0 - tail;
        if (avail == 0u) {
            return 0; /* 无数据 */
        }
        /* 步骤 2：消费者落后被覆盖，跳到最旧有效帧并记丢弃。 */
        if (avail > r->capacity) {
            r->dropped += avail - r->capacity;
            tail = h0 - r->capacity;
        }
        /* 步骤 3：拷贝候选帧体。 */
        slot = r->buf + (size_t)(tail & r->mask) * r->elem_size;
        BM_REC_BARRIER();
        memcpy(out, slot, r->elem_size);
        BM_REC_BARRIER();
        /* 步骤 4：再读 head，确认拷贝期间该槽未被生产者追平覆盖。 */
        h1 = bm_rec_load(&r->head);
        if ((h1 - tail) > r->capacity) {
            /* 本帧在拷贝中被覆盖：作废、记一次丢弃、重试一次。 */
            r->dropped += 1u;
            r->tail = h1 - r->capacity;
            if (!retried) {
                retried = 1;
                continue;
            }
            return 0; /* 再次失败：放弃本次（已计丢弃） */
        }
        /* 步骤 5：拷贝一致，推进 tail。 */
        r->tail = tail + 1u;
        return 1;
    }
}

/** drain 单帧栈缓冲上限（字节），覆盖黑匣子定长帧（FOC 帧 ~52B）。 */
#define BM_RECORDER_DRAIN_FRAME_MAX 256u

uint32_t bm_recorder_drain(bm_recorder_t *r,
                           void (*sink)(const void *f, void *ctx), void *ctx,
                           uint32_t budget) {
    uint32_t processed = 0u;
    uint8_t frame[BM_RECORDER_DRAIN_FRAME_MAX];

    if (r == NULL || r->buf == NULL || r->elem_size == 0u ||
        r->elem_size > BM_RECORDER_DRAIN_FRAME_MAX) {
        return 0u; /* 帧过大无法用栈缓冲安全导出 */
    }
    /* 逐帧 pop 到栈缓冲并回调，避免直接暴露环内槽位（可能被覆盖）。 */
    while (budget == 0u || processed < budget) {
        if (bm_recorder_pop(r, frame) != 1) {
            break;
        }
        if (sink != NULL) {
            sink(frame, ctx);
        }
        ++processed;
    }
    return processed;
}

uint32_t bm_recorder_count(const bm_recorder_t *r) {
    uint32_t head;
    uint32_t tail;
    uint32_t avail;

    if (r == NULL) {
        return 0u;
    }
    head = r->head;
    tail = r->tail;
    avail = head - tail;
    return (avail > r->capacity) ? r->capacity : avail;
}

uint32_t bm_recorder_dropped(const bm_recorder_t *r) {
    return (r != NULL) ? r->dropped : 0u;
}
