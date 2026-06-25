/**
 * @file test_bus.c
 * @brief bm_bus 单元测试（Task 2/3/4/5/6：open/validate/acquire_write/commit/abort/reader_attach/acquire_read/release/freeze/ready_count/stats/LATEST三缓冲/validate边界/freeze守卫）
 *
 * TDD 覆盖：open/validate/acquire_write/commit/abort 基本语义，
 * QUEUE 满立即拒绝（test_queue_write_overflow_on_full，仅依赖写路径），
 * QUEUE 读路径：reader_attach 唯一性、acquire_read、release、freeze（Phase 1 Task 3）；
 * SIGNAL 多读者独立游标、overflow 计数、ready_count/stats（Phase 1 Task 4）；
 * LATEST 三缓冲选槽语义：写路径实现、单读者约束、读到最新值、release 清 NONE（Phase 1 Task 5）；
 * validate 边界：cap<2 直接构造拒绝、LATEST cap<3 拒绝、LATEST cap=3 通过、
 *   BLOCK acquire_write 返回 NOT_SUPPORTED；freeze 守卫：幂等、freeze 后写路径不受阻（Phase 1 Task 6）。
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-06-25
 */

#include "unity.h"
#include "bm/core/bm_bus.h"
#include <string.h>

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
    /* 两读者各自 overflow 跳槽后游标均落在 wc-(cap-1)=2；release 推进各自游标，
     * 验证 r_slow 的 release 不影响 r_fast 的独立游标 */
    bm_bus_release(&r_slow);   /* read_cur[slow]: 2 -> 3 */

    /* r_fast 仍未 release，游标停在跳槽位置 2：再读应得值=2（BM_OK，diff=9-2=7<cap，
     * 不再 overflow），且 overflow_count 不再增长——证明 r_slow 的推进未污染 r_fast */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r_fast, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(2u, *s);
    TEST_ASSERT_EQUAL_UINT32(1u, tb_s_storage.readers[r_fast.slot_idx].overflow_count);
    bm_bus_release(&r_fast);   /* read_cur[fast]: 2 -> 3 */

    /* 此后两游标都在 3，但各自独立推进：r_slow 读值=3，r_fast 读值=3，
     * 二者顺序消费互不干扰 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r_slow, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(3u, *s);
    bm_bus_release(&r_slow);   /* read_cur[slow]: 3 -> 4 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r_fast, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(3u, *s);   /* r_fast 不受 r_slow 已推进到 4 的影响 */
    bm_bus_release(&r_fast);

    /* r_fast 还剩 4..8 共 5 项，r_slow 还剩 4..8 共 5 项，各自独立 */
    TEST_ASSERT_EQUAL_UINT32(5u, bm_bus_ready_count(&r_fast));
    TEST_ASSERT_EQUAL_UINT32(5u, bm_bus_ready_count(&r_slow));
}

/**
 * @brief SIGNAL release 复检消费窗口绕圈：读者借出有效帧后，写者在消费窗口内
 *        灌满一圈覆盖该槽，release 必须返回 BM_ERR_OVERFLOW（本帧作废），
 *        overflow_count 递增，游标跳到最旧可用槽。
 *
 * Fix 1 核心回归：acquire 端 lap 检测挡"读前已被绕过"，release 端复检挡
 * "读中被绕过"，两端对称防撕裂（零拷贝借用窗口的确定性保护）。
 */
void test_signal_release_detects_window_overflow(void) {
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;
    uint32_t i;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r));
    /* 写 1 帧并借出：rc=0, wc=1, diff=1<cap，有效帧 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_s, &ws));
    *(uint32_t *)ws = 7u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_s));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(7u, *s);

    /* 消费窗口内写者灌满一整圈（cap=8）：wc 推进到 9，(wc-rc)=9>=cap，本槽已被覆盖 */
    for (i = 0u; i < 8u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_s, &ws));
        *(uint32_t *)ws = 100u + i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_s));
    }

    /* release 复检：本帧已被绕过 → BM_ERR_OVERFLOW（作废本帧） */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_release(&r));
    TEST_ASSERT_EQUAL_UINT32(1u, tb_s_storage.readers[r.slot_idx].overflow_count);
    /* 游标跳到最旧可用槽 wc-(cap-1)=9-7=2，下次 acquire 取新数据 */
    TEST_ASSERT_EQUAL_UINT32(2u,
        bm_atomic_ipc_load_u32(&tb_s_storage.readers[r.slot_idx].read_cur));
}

/**
 * @brief QUEUE release 永不返回 OVERFLOW（回归）：写者满即拒，(wc-rc) 永 < cap，
 *        release 复检分支永不触发，恒返回 BM_OK 并推进游标。
 */
void test_queue_release_never_overflows(void) {
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;
    uint32_t i;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    /* 写满 cap-1=3 项；写者第 4 次会被拒（不影响本测试） */
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
        *(uint32_t *)ws = i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
    }
    /* 借出并 release：即便环满，QUEUE release 仍返回 BM_OK（diff=3<cap=4） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
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

    /* 附加读者，避免 QUEUE 写满拒绝 */
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

/* ------------------------------------------------------------------ */
/* Task 5：LATEST 三缓冲选槽语义，reader_attach 单读者约束             */
/* ------------------------------------------------------------------ */

/**
 * @brief LATEST 写入后单读者读到该值（三缓冲写路径基本验证）。
 *
 * tb_l cap=4（>= 3），写 111 后 attach 读者，读取必须得到 111。
 * 此测试验证 acquire_write/commit LATEST 分支基本功能。
 */
void test_latest_single_write_read(void) {
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;

    /* 先写再 attach，验证读者读到初始发布值 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 111u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(111u, *s);
    bm_bus_release(&r);
}

/**
 * @brief LATEST 多次覆盖发布后读者读到最新值（三缓冲无损更新验证）。
 *
 * 连续写 111 → 222 → 333，每次 commit 覆盖上一次；读者 acquire_read
 * 必须得到最后写入的 333，而非历史值。
 */
void test_latest_read_always_newest(void) {
    bm_bus_reader_t r;
    const uint32_t *s;
    void *ws;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));

    /* 写 111 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 111u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    /* 写 222（覆盖）*/
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 222u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    /* 写 333（再次覆盖）*/
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 333u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    /* 读：必须得到最新值 333 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(333u, *s);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
}

/**
 * @brief LATEST reader_attach 单读者约束：第二次 attach 返回 BM_ERR_INVALID。
 *
 * 权威规范：LATEST 仅允许 1 个读者，由 reader_count 追踪——reader_count==0
 * 表示空闲可 attach，>=1 表示已有读者，拒绝。
 */
void test_latest_only_one_reader(void) {
    bm_bus_reader_t r0, r1;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r0));
    /* 第二个读者：reader_count 已为 1，LATEST 单读者约束返回 BM_ERR_INVALID */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_reader_attach(&g_bus_l, &r1));
}

/**
 * @brief LATEST 未发布即读：attach 后、任何 commit 之前 acquire_read
 *        应返回 BM_ERR_WOULD_BLOCK（覆盖 latest_published == NONE 安全路径）。
 */
void test_latest_read_before_any_write(void) {
    bm_bus_reader_t r;
    const void *s;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    /* 尚无 commit，latest_published == BM_BUS_LATEST_NONE → 无数据 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &s));
}

/**
 * @brief LATEST release 后 latest_reading 清为 BM_BUS_LATEST_NONE。
 *
 * release 语义：清 reading 标记，使写者 choose_slot 可重用该槽。
 * 直接检查 storage 字段 latest_reading。
 */
void test_latest_release_clears_reading(void) {
    bm_bus_reader_t r;
    const void *s;
    void *ws;

    /* 写一个值 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 42u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, &s));
    /* acquire_read 后 latest_reading 应已被设为 published 槽 */
    TEST_ASSERT_NOT_EQUAL(BM_BUS_LATEST_NONE,
        (uint32_t)bm_atomic_ipc_load_u32(&tb_l_storage.latest_reading));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
    /* release 后 latest_reading 必须清为 BM_BUS_LATEST_NONE */
    TEST_ASSERT_EQUAL_UINT32(BM_BUS_LATEST_NONE,
        (uint32_t)bm_atomic_ipc_load_u32(&tb_l_storage.latest_reading));
}

/* ------------------------------------------------------------------ */
/* Task 6：validate 边界 + freeze 守卫 + LATEST cap>=3 强制校验       */
/* ------------------------------------------------------------------ */

/**
 * @brief cap<2 直接构造 storage（绕过 BM_BUS_DEFINE 的编译期断言）验证
 *        运行期 bus_storage_valid 拦截：bm_bus_open 返回 BM_ERR_INVALID。
 *
 * BM_BUS_DEFINE 在编译期以 _Static_assert 拦截 cap<2，
 * 此用例通过手工填充 capacity=1 的 storage 验证运行期校验同样有效
 * （防御深度：编译期与运行期双层拦截）。
 */
void test_validate_cap_less_than_2_rejected_at_macro(void) {
    bm_bus_storage_t bad_st;
    bm_bus_t         bad_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[8];
    bm_bus_reader_slot_t readers[1];

    memset(&bad_st, 0, sizeof(bad_st));
    bad_st.data_buf      = data_buf;
    bad_st.readers       = readers;
    bad_st.elem_size     = 4u;
    bad_st.capacity      = 1u;   /* 非法：< 2 */
    bad_st.max_consumers = 1u;
    bad_st.mode          = BM_BUS_QUEUE;

    /* open 内 bus_storage_valid 应拦截 capacity<2 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_open(&bad_h, &bad_st, &cfg));
}

/**
 * @brief LATEST 专属约束：spec §7 三缓冲多核防撕裂需 cap>=3；
 *        构造 mode=LATEST、capacity=2 的 storage，bm_bus_open 须返回 BM_ERR_INVALID。
 *
 * TDD RED 阶段：cap<3 校验注释后此用例断言失败（open 返回 BM_OK）；
 * TDD GREEN 阶段：恢复校验后通过。
 */
void test_validate_latest_cap_2_rejected(void) {
    bm_bus_storage_t bad_st;
    bm_bus_t         bad_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[2u * sizeof(uint32_t)];
    bm_bus_reader_slot_t readers[1];

    memset(&bad_st, 0, sizeof(bad_st));
    bad_st.data_buf      = data_buf;
    bad_st.readers       = readers;
    bad_st.elem_size     = sizeof(uint32_t);
    bad_st.capacity      = 2u;   /* LATEST 非法：< 3 */
    bad_st.max_consumers = 1u;
    bad_st.mode          = BM_BUS_LATEST;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_open(&bad_h, &bad_st, &cfg));
}

/**
 * @brief LATEST capacity=3 合法：bm_bus_open 返回 BM_OK（对照用例）。
 *
 * 与 test_validate_latest_cap_2_rejected 形成边界对，确认 cap=3 是最小合法值。
 */
void test_validate_latest_cap_3_ok(void) {
    bm_bus_storage_t ok_st;
    bm_bus_t         ok_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[3u * sizeof(uint32_t)];
    bm_bus_reader_slot_t readers[1];

    memset(&ok_st, 0, sizeof(ok_st));
    ok_st.data_buf      = data_buf;
    ok_st.readers       = readers;
    ok_st.elem_size     = sizeof(uint32_t);
    ok_st.capacity      = 3u;
    ok_st.max_consumers = 1u;
    ok_st.mode          = BM_BUS_LATEST;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&ok_h, &ok_st, &cfg));
}

/**
 * @brief Fix 2：SIGNAL/QUEUE 强制容量为 2 的幂（防 2^32 游标回绕静默损坏）。
 *        构造 mode=SIGNAL、capacity=6（非 2 的幂）的 storage，bm_bus_open 须返回
 *        BM_ERR_INVALID。自由递增游标在非 2 的幂容量下，2^32 回绕处取模不连续，
 *        会有一次静默错读；强制 2 的幂使 cur%cap 在回绕处无缝。
 */
void test_validate_signal_cap_not_pow2_rejected(void) {
    bm_bus_storage_t bad_st;
    bm_bus_t         bad_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[6u * sizeof(uint32_t)];
    bm_bus_reader_slot_t readers[3];

    memset(&bad_st, 0, sizeof(bad_st));
    bad_st.data_buf      = data_buf;
    bad_st.readers       = readers;
    bad_st.elem_size     = sizeof(uint32_t);
    bad_st.capacity      = 6u;   /* 非 2 的幂：SIGNAL/QUEUE 非法 */
    bad_st.max_consumers = 3u;
    bad_st.mode          = BM_BUS_SIGNAL;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_open(&bad_h, &bad_st, &cfg));
}

/**
 * @brief Fix 2 对照：QUEUE capacity=6 同样被拒（非 2 的幂）；LATEST 不受此约束
 *        （见 test_validate_latest_cap_3_ok，cap=3 仍合法，不用 write_cur）。
 */
void test_validate_queue_cap_not_pow2_rejected(void) {
    bm_bus_storage_t bad_st;
    bm_bus_t         bad_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[6u * sizeof(uint32_t)];
    bm_bus_reader_slot_t readers[1];

    memset(&bad_st, 0, sizeof(bad_st));
    bad_st.data_buf      = data_buf;
    bad_st.readers       = readers;
    bad_st.elem_size     = sizeof(uint32_t);
    bad_st.capacity      = 6u;   /* 非 2 的幂：QUEUE 非法 */
    bad_st.max_consumers = 1u;
    bad_st.mode          = BM_BUS_QUEUE;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_open(&bad_h, &bad_st, &cfg));
}

/**
 * @brief BLOCK mode：bm_bus_open 本身合法（mode 合法），
 *        acquire_write 返回 BM_ERR_NOT_SUPPORTED（Phase 2 占位守卫）。
 */
void test_validate_mode_block_not_supported_for_ring_ops(void) {
    bm_bus_storage_t blk_st;
    bm_bus_t         blk_h;
    bm_bus_cfg_t     cfg = { .owner_cpu = 0u };
    uint8_t          data_buf[16];
    bm_bus_reader_slot_t readers[1];
    void            *ws;

    memset(&blk_st, 0, sizeof(blk_st));
    blk_st.data_buf      = data_buf;
    blk_st.readers       = readers;
    blk_st.elem_size     = 4u;
    blk_st.capacity      = 4u;
    blk_st.max_consumers = 1u;
    blk_st.mode          = BM_BUS_BLOCK;

    /* BLOCK mode open 合法 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&blk_h, &blk_st, &cfg));
    /* acquire_write 在 BLOCK mode 返回 NOT_SUPPORTED（Phase 2 占位）*/
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_bus_acquire_write(&blk_h, &ws));
}

/**
 * @brief freeze 幂等：连续两次 freeze 均返回 BM_OK，第二次无副作用。
 */
void test_freeze_idempotent(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_q));  /* 幂等：frozen=1 不变 */
}

/**
 * @brief freeze 只锁 reader_attach；写路径（acquire_write）在 freeze 后不受阻。
 *
 * 验证 freeze 仅影响新读者的接入，不影响已建立拓扑的写者正常发布。
 */
void test_acquire_write_after_freeze_still_allowed(void) {
    void *ws;
    /* freeze 后写路径应仍然正常 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    bm_bus_abort(&g_bus_q);
}

#ifdef BM_ENABLE_BUS_TEST_HOOK
/* DET-01 验证支撑：模拟写者在每个读窗口持续覆盖发布，使 LATEST
 * spin-until-stable 始终失稳，从而触达重试上界路径。 */
static uint32_t s_latest_hook_calls;

static void latest_contention_hook(bm_bus_storage_t *st) {
    uint32_t cur  = (uint32_t)bm_atomic_ipc_load_u32(&st->latest_published);
    uint32_t next = (cur + 1u) % st->capacity; /* 0..cap-1，恒为有效非 NONE 槽 */
    s_latest_hook_calls++;
    bm_atomic_ipc_store_u32(&st->latest_published, next);
}

/**
 * @brief DET-01：LATEST acquire_read 在写者持续抢占下重试有界并非阻塞返回。
 *
 * 注入 latest_contention_hook 模拟写者在每次读窗口内覆盖发布，迫使
 * spin-until-stable 始终失稳。修复前循环无界，本用例会挂死（RED）；
 * 修复后至多重试 BM_CONFIG_BUS_LATEST_MAX_RETRIES 次即返回 BM_ERR_WOULD_BLOCK，
 * 钩子恰好被调用 MAX 次，且 reading 标记清回 NONE（GREEN）。
 */
void test_latest_read_retry_bounded_under_contention(void) {
    bm_bus_reader_t r;
    const void *s = NULL;
    void *ws;

    /* 先发布一个有效值，使 latest_published != NONE，进入 spin 路径 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 7u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));

    s_latest_hook_calls = 0u;
    bm_bus_test_latest_read_hook = latest_contention_hook;

    /* 持续抢占：必须有界放弃，返回 WOULD_BLOCK（修复前此调用永不返回）*/
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &s));

    bm_bus_test_latest_read_hook = NULL;

    /* 每次重试迭代调用钩子一次，第 MAX 次后放弃 → 钩子恰好被调 MAX 次 */
    TEST_ASSERT_EQUAL_UINT32(BM_CONFIG_BUS_LATEST_MAX_RETRIES,
                             s_latest_hook_calls);
    /* 放弃后 reading 标记应清回 NONE（与无数据早返回一致）*/
    TEST_ASSERT_EQUAL_UINT32(BM_BUS_LATEST_NONE,
        (uint32_t)bm_atomic_ipc_load_u32(&tb_l_storage.latest_reading));
}
#endif /* BM_ENABLE_BUS_TEST_HOOK */

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
    RUN_TEST(test_signal_release_detects_window_overflow);
    RUN_TEST(test_queue_release_never_overflows);
    RUN_TEST(test_ready_count_queue);
    RUN_TEST(test_ready_count_null);
    RUN_TEST(test_stats_basic);
    RUN_TEST(test_stats_invalid_params);
    /* Task 5：LATEST 三缓冲语义 */
    RUN_TEST(test_latest_single_write_read);
    RUN_TEST(test_latest_read_always_newest);
    RUN_TEST(test_latest_only_one_reader);
    RUN_TEST(test_latest_read_before_any_write);
    RUN_TEST(test_latest_release_clears_reading);
    /* Task 6：validate 边界 + freeze 守卫 */
    RUN_TEST(test_validate_cap_less_than_2_rejected_at_macro);
    RUN_TEST(test_validate_latest_cap_2_rejected);
    RUN_TEST(test_validate_latest_cap_3_ok);
    RUN_TEST(test_validate_signal_cap_not_pow2_rejected);
    RUN_TEST(test_validate_queue_cap_not_pow2_rejected);
    RUN_TEST(test_validate_mode_block_not_supported_for_ring_ops);
    RUN_TEST(test_freeze_idempotent);
    RUN_TEST(test_acquire_write_after_freeze_still_allowed);
#ifdef BM_ENABLE_BUS_TEST_HOOK
    /* DET-01：LATEST 重试上界（需 BM_ENABLE_BUS_TEST_HOOK 编入测试缝）*/
    RUN_TEST(test_latest_read_retry_bounded_under_contention);
#endif
    return UNITY_END();
}
