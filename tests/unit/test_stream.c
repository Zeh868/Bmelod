/**
 * @file test_stream.c
 * @brief bm_stream 零拷贝块流单元测试
 *
 * 覆盖生产者/消费者往返、ready 回调与 drop-newest 过载策略。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm_stream.h"
#include "bm_config.h"

#include <string.h>

typedef struct {
    uint32_t samples[8];
} test_payload_t;

static test_payload_t s_payloads[2];
static bm_block_t s_blocks[2];
static bm_stream_t s_stream;

static uint32_t g_ready_count;
static uint32_t g_last_sequence;

static void on_ready(bm_stream_t *stream, bm_block_t *block, void *context) {
    (void)stream;
    (void)context;
    g_ready_count++;
    g_last_sequence = block->sequence;
}

void setUp(void) {
    g_ready_count = 0u;
    g_last_sequence = 0u;
    memset(&s_stream, 0, sizeof(s_stream));
    s_stream.blocks = s_blocks;
    s_stream.block_count = 2u;
    s_stream.block_capacity = 2u;
    s_stream.policy = BM_STREAM_POLICY_DROP_NEWEST;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_init(&s_stream, s_payloads, 2u,
                                     sizeof(test_payload_t)));
    bm_stream_reset(&s_stream);
}

void tearDown(void) {
    bm_stream_set_ready_handler(&s_stream, NULL, NULL);
}

void test_stream_producer_consumer_roundtrip(void) {
    bm_block_t *prod;
    bm_block_t *cons;
    test_payload_t *payload;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &prod));
    payload = (test_payload_t *)prod->data;
    payload->samples[0] = 42u;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, prod,
                                                sizeof(test_payload_t), NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_consumer_acquire(&s_stream, &cons));
    TEST_ASSERT_EQUAL_PTR(prod, cons);
    TEST_ASSERT_EQUAL(42u, ((test_payload_t *)cons->data)->samples[0]);
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_consumer_release(&s_stream, cons));
}

void test_stream_ready_handler_rejected_by_default(void) {
    bm_block_t *prod;

    bm_stream_set_ready_handler(&s_stream, on_ready, NULL);
    TEST_ASSERT_NULL(s_stream.on_ready);

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &prod));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, prod, 16u, NULL));
    TEST_ASSERT_EQUAL(0, bm_stream_drain(&s_stream, 1u));
    TEST_ASSERT_EQUAL(0u, g_ready_count);
}

void test_stream_acquire_clears_stale_timestamp(void) {
    bm_block_t *prod;
    bm_block_t *cons;
    bm_timestamp_t ts = {
        .clock_id = 2u,
        .quality = 3u,
        .clock_epoch = 4u,
        .ticks = 1234u,
        .rate_hz = 1000u
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &prod));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, prod, 8u, &ts));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_consumer_acquire(&s_stream, &cons));
    TEST_ASSERT_EQUAL(1234u, cons->timestamp.ticks);
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_consumer_release(&s_stream, cons));

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &prod));
    TEST_ASSERT_EQUAL(0u, prod->timestamp.ticks);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, prod, 8u, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_consumer_acquire(&s_stream, &cons));
    TEST_ASSERT_EQUAL(0u, cons->timestamp.ticks);
    TEST_ASSERT_EQUAL(0u, cons->timestamp.rate_hz);
    TEST_ASSERT_EQUAL(0u, cons->timestamp.clock_epoch);
}

void test_stream_drop_newest_on_overrun(void) {
    bm_block_t *b0;
    bm_block_t *b1;
    bm_block_t *extra;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &b0));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, b0, 8u, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &b1));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, b1, 8u, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_stream_producer_acquire(&s_stream, &extra));
    TEST_ASSERT_EQUAL(1u, bm_stream_stats(&s_stream)->overrun);
}

void test_stream_consumes_oldest_sequence_after_slot_reuse(void) {
    bm_block_t *first;
    bm_block_t *second;
    bm_block_t *reused;
    bm_block_t *consumed;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &first));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, first, 8u, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &second));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, second, 8u, NULL));

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_consumer_acquire(&s_stream, &consumed));
    TEST_ASSERT_EQUAL_PTR(first, consumed);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_consumer_release(&s_stream, consumed));

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &reused));
    TEST_ASSERT_EQUAL_PTR(first, reused);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, reused, 8u, NULL));

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_consumer_acquire(&s_stream, &consumed));
    TEST_ASSERT_EQUAL_PTR(second, consumed);
    TEST_ASSERT_EQUAL(2u, consumed->sequence);
}

void test_stream_init_rejects_missing_or_insufficient_descriptors(void) {
    bm_stream_t invalid_stream;
    bm_block_t one_block[1];

    memset(&invalid_stream, 0, sizeof(invalid_stream));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_stream_init(&invalid_stream, s_payloads, 2u,
                                     sizeof(test_payload_t)));

    invalid_stream.blocks = one_block;
    invalid_stream.block_count = 1u;
    invalid_stream.block_capacity = 1u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_stream_init(&invalid_stream, s_payloads, 2u,
                                     sizeof(test_payload_t)));
}

void test_stream_mark_late_increments_stats(void) {
    bm_stream_mark_late(&s_stream);
    TEST_ASSERT_EQUAL(1u, bm_stream_stats(&s_stream)->late);
}

void test_stream_producer_abort_returns_block(void) {
    bm_block_t *block;

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &block));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_abort(&s_stream, block));
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_producer_acquire(&s_stream, &block));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_producer_commit(&s_stream, block, 8u, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stream_producer_consumer_roundtrip);
    RUN_TEST(test_stream_ready_handler_rejected_by_default);
    RUN_TEST(test_stream_acquire_clears_stale_timestamp);
    RUN_TEST(test_stream_drop_newest_on_overrun);
    RUN_TEST(test_stream_consumes_oldest_sequence_after_slot_reuse);
    RUN_TEST(test_stream_init_rejects_missing_or_insufficient_descriptors);
    RUN_TEST(test_stream_mark_late_increments_stats);
    RUN_TEST(test_stream_producer_abort_returns_block);
    return UNITY_END();
}
