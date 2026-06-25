/**
 * @file test_bus.c
 * @brief bm_bus 单元测试（Task 2/3/4：open/validate/acquire_write/commit/abort/reader_attach/acquire_read/release/freeze/ready_count/stats）
 *
 * TDD 覆盖：open/validate/acquire_write/commit/abort 基本语义，
 * QUEUE 满立即拒绝（test_queue_write_overflow_on_full，仅依赖写路径），
 * QUEUE 读路径：reader_attach 唯一性、acquire_read、release、freeze（Phase 1 Task 3）；
 * SIGNAL 多读者独立游标、overflow 计数、ready_count/stats（Phase 1 Task 4）。
 * @author zeh (china_qzh@163.com)
 * @version 0.3
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

/**
 * @brief QUEUE 满立即拒绝写：唯一读者 attach 不消费，写满 cap-1 项后第 cap 次
 *        acquire_write 必须返回 BM_ERR_OVERFLOW（不覆盖未读，对齐不丢语义）。
 *
 * 仅依赖写路径（reader_attach + acquire_write + commit），不依赖 acquire_read，
 * 故 Task 2 阶段即可启用。覆盖 bus_queue_is_full 满判据 (wc-rc)>=cap-1 的真分支。
 */
void test_queue_write_overflow_on_full(void) {
    bm_bus_reader_t r;
    void *slot;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    /* cap=4，可存 cap-1=3 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
    /* 第 4 次：环满（3 项未读，wc-rc=3>=cap-1=3）→ 立即拒绝 */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_write(&g_bus_q, &slot));
}

void test_queue_attach_only_one(void) {
    bm_bus_reader_t r1, r2;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r1));
    /* QUEUE max_consumers=1，第二次 attach 应返回 BM_ERR_INVALID */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_reader_attach(&g_bus_q, &r2));
}

void test_queue_read_no_data(void) {
    bm_bus_reader_t r;
    const void *s;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    /* 无数据，返回 BM_ERR_WOULD_BLOCK */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &s));
}

void test_queue_write_then_read(void) {
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    *(uint32_t *)ws = 0xDEADu;
    bm_bus_commit(&g_bus_q);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(0xDEADu, *s);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
    /* release 后再读，无新数据 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, (const void **)&s));
}

void test_queue_writer_rejected_never_laps_reader(void) {
    /* QUEUE 不丢语义：写满（cap-1=3 项未读）后 acquire_write 拒绝，
     * 写者永远无法绕过读者，故 QUEUE 读端永不出现 BM_ERR_OVERFLOW。
     * 读者消费 1 项后腾出空间，写者方可再写。 */
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;
    uint32_t i;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    /* 写满 3 项 */
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
        *(uint32_t *)ws = i;
        bm_bus_commit(&g_bus_q);
    }
    /* 第 4 次：满拒绝 */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_write(&g_bus_q, &ws));
    /* 读者消费 1 项（最旧 = 0），保序，无 overflow */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(0u, *s);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
    /* 腾出一槽，写者可再写 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    *(uint32_t *)ws = 99u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
}

void test_queue_attach_after_freeze_rejected(void) {
    bm_bus_reader_t r;
    bm_bus_freeze(&g_bus_q);
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_bus_reader_attach(&g_bus_q, &r));
}

/* ------------------------------------------------------------------ */
/* Task 4：SIGNAL 多读者独立游标 + ready_count/stats 用例              */
/* ------------------------------------------------------------------ */

/**
 * @brief SIGNAL 三读者独立追赶：第 4 个 attach 超额返回 BM_ERR_INVALID，
 *        各读者游标独立，r0 消费完后无数据，r1/r2 仍可独立追赶。
 */
void test_signal_three_readers_independent(void) {
    bm_bus_reader_t r0, r1, r2, r3;
    const uint32_t *s;
    void *ws;
    uint32_t i;

    /* 附加 3 个读者（max_consumers=3） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r0));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r1));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r2));
    /* 第 4 个超过配额 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_reader_attach(&g_bus_s, &r3));

    /* 写 3 项 */
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_s, &ws));
        *(uint32_t *)ws = i + 10u;
        bm_bus_commit(&g_bus_s);
    }

    /* r0 读完 3 项，r1/r2 各自独立追赶 */
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r0, (const void **)&s));
        TEST_ASSERT_EQUAL_UINT32(i + 10u, *s);
        bm_bus_release(&r0);
    }
    /* r1 只读 1 项，r2 不读 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r1, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(10u, *s);
    bm_bus_release(&r1);

    /* r0 无新数据 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r0, (const void **)&s));
    /* r1 还有 2 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r1, (const void **)&s));
    bm_bus_release(&r1);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r1, (const void **)&s));
    bm_bus_release(&r1);
}

/**
 * @brief SIGNAL 慢读者独立 overflow：写 9 项（超 cap=8），r_slow/r_fast 各自
 *        独立触发 overflow，overflow_count 各计 1，互不干扰。
 */
void test_signal_reader_overflow_independent(void) {
    bm_bus_reader_t r_fast, r_slow;
    const uint32_t *s;
    void *ws;
    uint32_t i;

    /* cap=8, max_consumers=3；SIGNAL 写者永不拒绝（覆盖语义） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r_fast));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r_slow));

    /* 写 9 项（超 cap=8）。SIGNAL acquire_write 始终 BM_OK（覆盖最旧），
     * 与 QUEUE 满拒绝形成对照 */
    for (i = 0u; i < 9u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_s, &ws));
        *(uint32_t *)ws = i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_s));
    }
    /* r_slow: write_cur=9, read_cur=0, 差=9>=cap=8 → 读端检出 overflow */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_read(&r_slow, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(1u, tb_s_storage.readers[r_slow.slot_idx].overflow_count);
    /* r_slow overflow 跳到最旧可用槽（write_cur-(cap-1)=2），不影响 r_fast 独立游标：
     * r_fast 游标仍在初始 attach 位置（write_cur 当时=0），同样被绕过 → 也检出 overflow，
     * 但二者 overflow_count 各自独立累计 */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_read(&r_fast, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(1u, tb_s_storage.readers[r_fast.slot_idx].overflow_count);
    /* r_slow 已跳槽，再读得最旧可用数据（值=2），r_fast 不受其 release 影响 */
    bm_bus_release(&r_slow);
}

/**
 * @brief ready_count 基本语义：QUEUE 写入 2 项后返回 2，消费 1 项后返回 1。
 */
void test_ready_count_queue(void) {
    bm_bus_reader_t r;
    const void *s;
    void *ws;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    /* 无数据时 ready_count == 0 */
    TEST_ASSERT_EQUAL_UINT32(0u, bm_bus_ready_count(&r));

    /* 写 2 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    *(uint32_t *)ws = 1u;
    bm_bus_commit(&g_bus_q);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    *(uint32_t *)ws = 2u;
    bm_bus_commit(&g_bus_q);

    TEST_ASSERT_EQUAL_UINT32(2u, bm_bus_ready_count(&r));

    /* 消费 1 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, &s));
    bm_bus_release(&r);
    TEST_ASSERT_EQUAL_UINT32(1u, bm_bus_ready_count(&r));
}

/**
 * @brief ready_count null 指针返回 0。
 */
void test_ready_count_null(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, bm_bus_ready_count(NULL));
}

/**
 * @brief stats 基本语义：commit N 次后 write_count == N，overflow_count Phase 1 占位为 0。
 */
void test_stats_basic(void) {
    bm_bus_stats_t st;
    void *ws;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_stats(&g_bus_q, &st));
    TEST_ASSERT_EQUAL_UINT32(0u, st.write_count);

    /* attach reader so queue won't refuse */
    bm_bus_reader_t r;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    bm_bus_commit(&g_bus_q);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    bm_bus_commit(&g_bus_q);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_stats(&g_bus_q, &st));
    TEST_ASSERT_EQUAL_UINT32(2u, st.write_count);
    TEST_ASSERT_EQUAL_UINT32(0u, st.overflow_count); /* Phase 1 占位 */
}

/**
 * @brief stats 参数校验：空指针返回 BM_ERR_INVALID。
 */
void test_stats_invalid_params(void) {
    bm_bus_stats_t st;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_stats(NULL, &st));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_stats(&g_bus_q, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_validate_ok);
    RUN_TEST(test_validate_null_h);
    RUN_TEST(test_acquire_write_commit_basic);
    RUN_TEST(test_acquire_write_abort);
    RUN_TEST(test_acquire_write_reentrancy_guard);
    RUN_TEST(test_queue_write_overflow_on_full);
    RUN_TEST(test_queue_attach_only_one);
    RUN_TEST(test_queue_read_no_data);
    RUN_TEST(test_queue_write_then_read);
    RUN_TEST(test_queue_writer_rejected_never_laps_reader);
    RUN_TEST(test_queue_attach_after_freeze_rejected);
    /* Task 4：SIGNAL 多读者 + ready_count/stats */
    RUN_TEST(test_signal_three_readers_independent);
    RUN_TEST(test_signal_reader_overflow_independent);
    RUN_TEST(test_ready_count_queue);
    RUN_TEST(test_ready_count_null);
    RUN_TEST(test_stats_basic);
    RUN_TEST(test_stats_invalid_params);
    return UNITY_END();
}
