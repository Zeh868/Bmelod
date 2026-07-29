/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_dma_circular_ring.c
 * @brief DMA 循环模式环形缓冲记账单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-29
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-29       1.0            zeh            新增 DMA 循环环形缓冲记账单测
 * 2026-07-29       1.1            zeh            修正 NDTR 模拟值与手动 buffer 写入
 *
 */
#include "unity.h"
#include "bm_dma_circular_ring.h"

#include <string.h>

#define TEST_RING_LEN 8u

static uint8_t s_buf[TEST_RING_LEN];
static bm_dma_circ_ring_t s_ring;

void setUp(void) {
    (void)memset(s_buf, 0, sizeof(s_buf));
    (void)memset(&s_ring, 0, sizeof(s_ring));
}

void tearDown(void) {
}

/**
 * @brief 基础 round-trip：写、读、pending 归零。
 */
static void test_ring_basic_write_read(void) {
    uint8_t out[TEST_RING_LEN];
    uint8_t data[] = { 0x01, 0x02, 0x03 };

    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));
    TEST_ASSERT_EQUAL(TEST_RING_LEN,
                      bm_dma_circ_ring_free_space(&s_ring));

    /* 模拟 DMA 向 buffer 写入 3 字节（NDTR 从 8 降到 5） */
    s_buf[0] = 0x01;
    s_buf[1] = 0x02;
    s_buf[2] = 0x03;
    TEST_ASSERT_EQUAL(3u, bm_dma_circ_ring_update(&s_ring, 5u));
    TEST_ASSERT_EQUAL(3u, bm_dma_circ_ring_pending(&s_ring));

    TEST_ASSERT_EQUAL(3u,
                      bm_dma_circ_ring_consume(&s_ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));
}

/**
 * @brief 恰好接收 1 个完整缓冲区：pending 应等于 len，不能误判为空。
 */
static void test_ring_exactly_one_full_buffer(void) {
    uint8_t out[TEST_RING_LEN];
    uint32_t i;

    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);

    /* 模拟 DMA 写入 4 字节 */
    for (i = 0u; i < 4u; ++i) {
        s_buf[i] = (uint8_t)(0xA0u + i);
    }
    TEST_ASSERT_EQUAL(4u, bm_dma_circ_ring_update(&s_ring, 4u));
    TEST_ASSERT_EQUAL(4u, bm_dma_circ_ring_pending(&s_ring));

    /* DMA TC：完整一圈 8 字节 */
    for (i = 4u; i < 8u; ++i) {
        s_buf[i] = (uint8_t)(0xA0u + i);
    }
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(8u, bm_dma_circ_ring_pending(&s_ring));

    /* 消费全部 8 字节 */
    TEST_ASSERT_EQUAL(8u,
                      bm_dma_circ_ring_consume(&s_ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));
}

/**
 * @brief 连续接收多个完整缓冲区：每圈 produced 单调递增，pending 正确。
 */
static void test_ring_multiple_full_rounds(void) {
    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);

    /* 第一圈完成 */
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(8u, s_ring.produced);
    TEST_ASSERT_EQUAL(8u, bm_dma_circ_ring_pending(&s_ring));

    /* 消费 3 字节 */
    s_ring.consumed = 3u;
    TEST_ASSERT_EQUAL(5u, bm_dma_circ_ring_pending(&s_ring));

    /* 第二圈写入 2 字节：NDTR = 8 - 2 = 6 */
    TEST_ASSERT_EQUAL(2u, bm_dma_circ_ring_update(&s_ring, 6u));
    TEST_ASSERT_EQUAL(7u, bm_dma_circ_ring_pending(&s_ring));

    /* 第二圈完成 */
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(16u, s_ring.produced);
    TEST_ASSERT_EQUAL(13u, bm_dma_circ_ring_pending(&s_ring));

    /* 第三圈完成 */
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(24u, s_ring.produced);
    TEST_ASSERT_EQUAL(21u, bm_dma_circ_ring_pending(&s_ring));
}

/**
 * @brief IDLE 恰好发生在缓冲区边界：事件长度应为 len，且下一轮可继续。
 */
static void test_ring_idle_at_boundary(void) {
    uint8_t out[TEST_RING_LEN];
    uint32_t i;

    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);

    /* 写入 8 字节到边界 */
    for (i = 0u; i < 8u; ++i) {
        s_buf[i] = (uint8_t)(0xC0u + i);
    }
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(8u, bm_dma_circ_ring_pending(&s_ring));

    /* IDLE 在边界触发：消费整圈 */
    TEST_ASSERT_EQUAL(8u,
                      bm_dma_circ_ring_consume(&s_ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));

    /* 下一圈继续写入 3 字节：NDTR = 8 - 3 = 5 */
    s_buf[0] = 0xD1;
    s_buf[1] = 0xD2;
    s_buf[2] = 0xD3;
    TEST_ASSERT_EQUAL(3u, bm_dma_circ_ring_update(&s_ring, 5u));
    TEST_ASSERT_EQUAL(3u, bm_dma_circ_ring_pending(&s_ring));
}

/**
 * @brief 消费速度低于 DMA 写入速度：新数据超过空闲空间时报告溢出并丢弃旧数据。
 */
static void test_ring_slow_consumer_overflow(void) {
    uint32_t free_before;
    uint32_t new_bytes;

    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);

    /* 写入 6 字节：NDTR = 8 - 6 = 2 */
    TEST_ASSERT_EQUAL(6u, bm_dma_circ_ring_update(&s_ring, 2u));
    TEST_ASSERT_EQUAL(6u, bm_dma_circ_ring_pending(&s_ring));
    TEST_ASSERT_EQUAL(2u, bm_dma_circ_ring_free_space(&s_ring));

    /* 再写 4 字节：需先完成当前圈，再进入下一圈。
     * 当前 produced=6，完成一圈后 produced=8，下一圈 4 字节后 produced=12。 */
    free_before = bm_dma_circ_ring_free_space(&s_ring); /* 2 */
    bm_dma_circ_ring_mark_full_round(&s_ring);          /* 第 1 圈完成 */
    TEST_ASSERT_EQUAL(8u, bm_dma_circ_ring_pending(&s_ring));
    new_bytes = bm_dma_circ_ring_update(&s_ring, 4u);  /* 下一圈写入 4 字节 */
    TEST_ASSERT_EQUAL(4u, new_bytes);

    /* 驱动侧检测到 new_bytes > free_before，执行 drop_all */
    TEST_ASSERT_TRUE(new_bytes > free_before);
    bm_dma_circ_ring_drop_all(&s_ring);
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));

    /* 溢出后新数据仍可继续接收：当前 produced=12，再写 2 字节对应 NDTR=2 */
    TEST_ASSERT_EQUAL(2u, bm_dma_circ_ring_update(&s_ring, 2u));
    TEST_ASSERT_EQUAL(2u, bm_dma_circ_ring_pending(&s_ring));
}

/**
 * @brief 消费时跨圈环绕正确：先写满一圈并消费完，再写跨越边界的 6 字节。
 */
static void test_ring_wraparound_consume(void) {
    uint8_t out[TEST_RING_LEN];
    uint32_t i;

    bm_dma_circ_ring_reset(&s_ring, s_buf, TEST_RING_LEN);

    /* 写满第一圈 */
    for (i = 0u; i < 8u; ++i) {
        s_buf[i] = (uint8_t)(0xA0u + i);
    }
    bm_dma_circ_ring_mark_full_round(&s_ring);
    TEST_ASSERT_EQUAL(8u, bm_dma_circ_ring_pending(&s_ring));

    /* 消费完整圈 */
    TEST_ASSERT_EQUAL(8u,
                      bm_dma_circ_ring_consume(&s_ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL(0u, bm_dma_circ_ring_pending(&s_ring));

    /* 继续写入 6 字节跨越边界：覆盖 0..5 */
    s_buf[0] = 0xB1;
    s_buf[1] = 0xB2;
    s_buf[2] = 0xB3;
    s_buf[3] = 0xB4;
    s_buf[4] = 0xB5;
    s_buf[5] = 0xB6;
    TEST_ASSERT_EQUAL(6u, bm_dma_circ_ring_update(&s_ring, 2u));
    TEST_ASSERT_EQUAL(6u, bm_dma_circ_ring_pending(&s_ring));

    TEST_ASSERT_EQUAL(6u,
                      bm_dma_circ_ring_consume(&s_ring, out, sizeof(out)));
    TEST_ASSERT_EQUAL(0xB1, out[0]);
    TEST_ASSERT_EQUAL(0xB2, out[1]);
    TEST_ASSERT_EQUAL(0xB3, out[2]);
    TEST_ASSERT_EQUAL(0xB4, out[3]);
    TEST_ASSERT_EQUAL(0xB5, out[4]);
    TEST_ASSERT_EQUAL(0xB6, out[5]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ring_basic_write_read);
    RUN_TEST(test_ring_exactly_one_full_buffer);
    RUN_TEST(test_ring_multiple_full_rounds);
    RUN_TEST(test_ring_idle_at_boundary);
    RUN_TEST(test_ring_slow_consumer_overflow);
    RUN_TEST(test_ring_wraparound_consume);
    return UNITY_END();
}
