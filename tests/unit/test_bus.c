/**
 * @file test_bus.c
 * @brief bm_bus 单元测试（Task 2：open/validate/acquire_write/commit/abort）
 *
 * TDD 覆盖：open/validate/acquire_write/commit/abort 基本语义。
 * test_queue_write_overflow_on_full 依赖 Task 3 的 bm_bus_reader_attach，
 * Task 2 阶段先注释，Task 3 完成后整体复跑启用。
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-25
 */

#include "unity.h"
#include "bm/core/bm_bus.h"

BM_BUS_DEFINE(tb_q, uint32_t, 4u, 1u, BM_BUS_QUEUE);
BM_BUS_DEFINE(tb_s, uint32_t, 8u, 3u, BM_BUS_SIGNAL);
BM_BUS_DEFINE(tb_l, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_bus_q, g_bus_s, g_bus_l;

void setUp(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_bus_q, &tb_q_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_bus_s, &tb_s_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_bus_l, &tb_l_storage, &cfg));
}
void tearDown(void) {
    bm_bus_close(&g_bus_q);
    bm_bus_close(&g_bus_s);
    bm_bus_close(&g_bus_l);
}

void test_open_validate_ok(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_validate(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_validate(&g_bus_s));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_validate(&g_bus_l));
}

void test_validate_null_h(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_validate(NULL));
}

void test_acquire_write_commit_basic(void) {
    void *slot;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_NOT_NULL(slot);
    *(uint32_t *)slot = 42u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
}

void test_acquire_write_abort(void) {
    void *slot;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_abort(&g_bus_q));
    /* abort 后可再次 acquire */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_abort(&g_bus_q));
}

void test_acquire_write_reentrancy_guard(void) {
    void *slot;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    /* 未 commit 前再次 acquire 返回 BM_ERR_BUSY */
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_bus_acquire_write(&g_bus_q, &slot));
    bm_bus_abort(&g_bus_q);
}

/* test_queue_write_overflow_on_full:
 * 依赖 Task 3 的 bm_bus_reader_attach（QUEUE 单消费者）。
 * Task 3 完成后取消注释并整体复跑确认。
 *
 * void test_queue_write_overflow_on_full(void) {
 *     bm_bus_reader_t r;
 *     void *slot;
 *     TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
 *     TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
 *     bm_bus_commit(&g_bus_q);
 *     TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
 *     bm_bus_commit(&g_bus_q);
 *     TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
 *     bm_bus_commit(&g_bus_q);
 *     TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_write(&g_bus_q, &slot));
 * }
 */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_validate_ok);
    RUN_TEST(test_validate_null_h);
    RUN_TEST(test_acquire_write_commit_basic);
    RUN_TEST(test_acquire_write_abort);
    RUN_TEST(test_acquire_write_reentrancy_guard);
    return UNITY_END();
}
