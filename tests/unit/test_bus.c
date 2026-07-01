/**
 * @file test_bus.c
 * @brief bm_bus 单元测试（Task 2/3/4/5/6/7/8/9：open/validate/acquire_write/commit/abort/reader_attach/acquire_read/release/freeze/ready_count/stats/LATEST三缓冲/validate边界/freeze守卫/BLOCK IoC API/reset生命周期对称/seqlock多读者拷出）
 *
 * TDD 覆盖：open/validate/acquire_write/commit/abort 基本语义，
 * QUEUE 满立即拒绝（test_queue_write_overflow_on_full，仅依赖写路径），
 * QUEUE 读路径：reader_attach 唯一性、acquire_read、release、freeze（Phase 1 Task 3）；
 * SIGNAL 多读者独立游标、overflow 计数、ready_count/stats（Phase 1 Task 4）；
 * LATEST 三缓冲选槽语义：写路径实现、单读者约束、读到最新值、release 清 NONE（Phase 1 Task 5）；
 * validate 边界：cap<2 直接构造拒绝、LATEST cap<3 拒绝、LATEST cap=3 通过、
 *   BLOCK acquire_write 返回 NOT_SUPPORTED；freeze 守卫：幂等、freeze 后写路径不受阻（Phase 1 Task 6）。
 * BLOCK IoC API：bind_block_backend/produce_acquire/commit/abort/consume_acquire/release、
 *   unbound 返回 INVALID、valid_bytes/ts_ns passthrough、owner_cpu 透传（Task 7）。
 * bm_bus_reset() 生命周期对称：解冻、游标/计数清零、三模式覆盖、BLOCK 不破坏绑定、幂等（Task 8）。
 * bm_bus_latest_read seqlock 多观察者拷出：无需 attach、与单读者零拷贝并存、重试有界（Task 9）。
 * @author zeh (china_qzh@163.com)
 * @version 0.8
 * @date 2026-06-26
 */

#include "unity.h"
#include "bm/core/bm_bus.h"
#include "bm/hybrid/bm_stream.h"
#include <string.h>

BM_BUS_DEFINE(tb_q, uint32_t, 4u, 1u, BM_BUS_QUEUE);
BM_BUS_DEFINE(tb_s, uint32_t, 8u, 3u, BM_BUS_SIGNAL);
BM_BUS_DEFINE(tb_l, uint32_t, 4u, 1u, BM_BUS_LATEST);
/* Task 10（bm_tt_schedule 判龄前置）：内部 seq 访问器专用 bus，需 BM_BUS_ALLOW_INTERNAL 才可见 */
BM_BUS_DEFINE(seqbus, uint32_t, 3u, 1u, BM_BUS_LATEST);   /* cap>=3 三缓冲 */

static bm_bus_t g_bus_q, g_bus_s, g_bus_l;

#ifdef BM_BUS_ALLOW_INTERNAL
/**
 * @brief 小发布助手：acquire_write → memcpy → commit（仓内 LATEST 无单调用 write）
 */
static int seqbus_publish(bm_bus_t *h, uint32_t v) {
    void *slot;
    int rc = bm_bus_acquire_write(h, &slot);
    if (rc != BM_OK) {
        return rc;
    }
    (void)memcpy(slot, &v, sizeof v);
    return bm_bus_commit(h);
}
#endif /* BM_BUS_ALLOW_INTERNAL */

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

/* ------------------------------------------------------------------ */
/* Task 7: BLOCK IoC API                                               */
/* 使用 bm_stream 作为 block backend，通过 bm_stream_as_block_backend  */
/* 绑定到 BLOCK 模式 bus，验证端到端 produce/consume 路径。            */
/* 每个测试使用独立静态存储，避免 frozen 状态在测试间串扰。            */
/* ------------------------------------------------------------------ */

/* 每个 BLOCK 测试独占一份 bus storage，防止 frozen/block_iface 跨测试污染 */
BM_BUS_DEFINE(tb_blk_unbound,  uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_wrong,    uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_freeze,   uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_rt,       uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_abort,    uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_ts,       uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_ts0,      uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_BUS_DEFINE(tb_blk_cpu,      uint8_t, 2u, 1u, BM_BUS_BLOCK);

/* stream 存储（每个测试独占，payload=uint32_t，depth=4） */
#define BLK_STREAM_DEPTH 4u
BM_STREAM_PAYLOADS(g_blk_freeze_stream,  uint32_t, BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_freeze_stream,               BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_freeze_stream,              BLK_STREAM_DEPTH);

BM_STREAM_PAYLOADS(g_blk_rt_stream,     uint32_t, BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_rt_stream,                    BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_rt_stream,                  BLK_STREAM_DEPTH);

BM_STREAM_PAYLOADS(g_blk_abort_stream,  uint32_t, BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_abort_stream,                 BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_abort_stream,               BLK_STREAM_DEPTH);

BM_STREAM_PAYLOADS(g_blk_ts_stream,     uint8_t,  BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_ts_stream,                    BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_ts_stream,                  BLK_STREAM_DEPTH);

BM_STREAM_PAYLOADS(g_blk_ts0_stream,    uint8_t,  BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_ts0_stream,                   BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_ts0_stream,                 BLK_STREAM_DEPTH);

/**
 * @brief 未绑定 backend 时，所有 BLOCK API 返回 BM_ERR_INVALID。
 *
 * 验证 bus_block_check 在 block_iface==NULL 时的保护逻辑。
 */
void test_block_unbound_returns_invalid(void) {
    bm_bus_t     h;
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    void        *blk = NULL;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_unbound_storage, &cfg));
    /* 未 bind，所有 BLOCK API 均返回 INVALID */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_block_produce_acquire(&h, &blk));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_block_produce_commit(&h, NULL, 0u, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_block_produce_abort(&h, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_block_consume_acquire(&h, &blk));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_block_consume_release(&h, NULL));
    bm_bus_close(&h);
}

/** @brief bind_block_backend 在非 BLOCK mode bus 上应返回 BM_ERR_NOT_SUPPORTED。 */
void test_block_bind_wrong_mode_rejected(void) {
    /* g_bus_q 是 QUEUE mode，bind 应被拒绝（非 BLOCK 模式） */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
        bm_bus_bind_block_backend(&g_bus_q, bm_stream_as_block_backend(), NULL));
}

/** @brief bind 后 freeze，再次 bind 被拒绝（freeze-before-only 语义）。 */
void test_block_bind_after_freeze_rejected(void) {
    bm_bus_t     h;
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_freeze_stream,
                       _bm_stream_payload_g_blk_freeze_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint32_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_freeze_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_freeze_stream));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&h));
    /* freeze 后再 bind 被拒绝（BM_ERR_BUSY：bus 已冻结） */
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_freeze_stream));
    bm_bus_close(&h);
}

/** @brief produce_acquire -> commit -> consume_acquire -> release 端到端路径。
 *
 *  验证：
 *  1. produce_acquire 返回非 NULL block
 *  2. commit 成功（BM_OK）
 *  3. consume_acquire 获得同一 block（指针相同或数据一致）
 *  4. consume_release 成功
 *  5. valid_bytes passthrough
 */
void test_block_produce_commit_consume_roundtrip(void) {
    bm_bus_t        h;
    bm_bus_cfg_t    cfg = { .owner_cpu = 0u };
    void           *wblk = NULL;
    void           *rblk = NULL;
    bm_block_t     *rblk_typed;
    const uint32_t *rdata;

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_rt_stream,
                       _bm_stream_payload_g_blk_rt_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint32_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_rt_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_rt_stream));

    /* produce: acquire */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_produce_acquire(&h, &wblk));
    TEST_ASSERT_NOT_NULL(wblk);

    /* 写入数据到 block->data */
    *(uint32_t *)((bm_block_t *)wblk)->data = 0xDEADBEEFu;

    /* commit with valid_bytes=4, ts_ns=1000 (1 us) */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_block_produce_commit(&h, wblk, sizeof(uint32_t), 1000u));

    /* consume: acquire */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_acquire(&h, &rblk));
    TEST_ASSERT_NOT_NULL(rblk);

    rblk_typed = (bm_block_t *)rblk;

    /* 验证 valid_bytes passthrough */
    TEST_ASSERT_EQUAL_UINT32(sizeof(uint32_t), rblk_typed->valid_bytes);

    /* 验证数据一致 */
    rdata = (const uint32_t *)rblk_typed->data;
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, *rdata);

    /* release */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_release(&h, rblk));

    bm_bus_close(&h);
}

/** @brief produce_acquire -> abort 后，consume_acquire 应返回无数据（非 BM_OK）。
 *
 *  abort 不应向 stream 提交数据，消费者不应看到任何 READY block。
 */
void test_block_produce_abort_no_ready(void) {
    bm_bus_t     h;
    bm_bus_cfg_t cfg  = { .owner_cpu = 0u };
    void        *wblk = NULL;
    void        *rblk = NULL;

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_abort_stream,
                       _bm_stream_payload_g_blk_abort_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint32_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_abort_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_abort_stream));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_produce_acquire(&h, &wblk));
    TEST_ASSERT_NOT_NULL(wblk);
    /* abort: 丢弃，不提交 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_produce_abort(&h, wblk));

    /* 消费端应看不到数据（stream empty -> 非 BM_OK） */
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_bus_block_consume_acquire(&h, &rblk));
    TEST_ASSERT_NULL(rblk);

    bm_bus_close(&h);
}

/** @brief ts_ns passthrough：commit 带非零 ts_ns，block->timestamp.ticks == ts_ns。
 *
 *  #9-1c：adapter 改为 ns 直存（ticks = ts_ns、rate_hz = 1e9），零截断。
 */
void test_block_ts_ns_passthrough(void) {
    bm_bus_t       h;
    bm_bus_cfg_t   cfg    = { .owner_cpu = 0u };
    void          *wblk   = NULL;
    void          *rblk   = NULL;
    bm_block_t    *rtyped;
    const uint64_t TS_NS  = 5000000u; /* 5 ms = 5_000_000 ns */

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_ts_stream,
                       _bm_stream_payload_g_blk_ts_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint8_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_ts_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_ts_stream));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_produce_acquire(&h, &wblk));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_block_produce_commit(&h, wblk, 1u, TS_NS));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_acquire(&h, &rblk));
    rtyped = (bm_block_t *)rblk;

    /* #9-1c：ns 直存，ticks 应为 TS_NS 原值（无 /1000 截断） */
    TEST_ASSERT_EQUAL_UINT64(TS_NS, rtyped->timestamp.ticks);
    /* rate_hz 应为 1 GHz（ns 粒度） */
    TEST_ASSERT_EQUAL_UINT32(1000000000u, rtyped->timestamp.rate_hz);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_release(&h, rblk));
    bm_bus_close(&h);
}

/** @brief ts_ns==0 统一时钟兜底：commit 不带时间戳时 adapter 用 bm_uptime_ns() 打戳。
 *
 *  #9-1c：缺省时间戳不再留 0，应得到非零 ticks 且 rate_hz=1e9（ns 粒度）。
 */
void test_block_ts_ns_zero_uses_uptime(void) {
    bm_bus_t       h;
    bm_bus_cfg_t   cfg    = { .owner_cpu = 0u };
    void          *wblk   = NULL;
    void          *rblk   = NULL;
    bm_block_t    *rtyped;
    uint64_t       t_before;

    /*
     * 预热单调时钟：首次调用 bm_uptime_ns() 仅建立基线（某些后端首读返回 0），
     * 后续 bus 建链/打戳调用必晚于此，确保兜底 ticks 单调严格推进。
     */
    t_before = bm_uptime_ns();

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_ts0_stream,
                       _bm_stream_payload_g_blk_ts0_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint8_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_ts0_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_ts0_stream));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_produce_acquire(&h, &wblk));
    /* ts_ns=0：触发 uptime 兜底 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_block_produce_commit(&h, wblk, 1u, 0u));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_acquire(&h, &rblk));
    rtyped = (bm_block_t *)rblk;

    /*
     * 兜底打戳证据：
     *  - rate_hz==1e9 唯一来自 bm_timestamp_from_uptime()（旧 NULL 路径会留 0），
     *    确定性证明兜底路径已执行；
     *  - ticks >= 预热时刻，确认用的是单调时钟而非 0。
     */
    TEST_ASSERT_EQUAL_UINT32(1000000000u, rtyped->timestamp.rate_hz);
    TEST_ASSERT_TRUE(rtyped->timestamp.ticks >= t_before);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_block_consume_release(&h, rblk));
    bm_bus_close(&h);
}

/** @brief owner_cpu 透传：bm_bus_open cfg.owner_cpu=1，storage.owner_cpu 应保存为 1。 */
void test_block_owner_cpu_passthrough(void) {
    bm_bus_t     h;
    bm_bus_cfg_t cfg = { .owner_cpu = 1u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_cpu_storage, &cfg));
    /* 验证 storage 中 owner_cpu 被正确保存 */
    TEST_ASSERT_EQUAL_UINT32(1u, tb_blk_cpu_storage.owner_cpu);
    bm_bus_close(&h);
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

/* ------------------------------------------------------------------ */
/* Task 8：bm_bus_reset() 生命周期对称                                 */
/* ------------------------------------------------------------------ */

/* BLOCK reset 测试专用存储：独立 storage 防止 frozen 跨测试污染 */
BM_BUS_DEFINE(tb_blk_reset, uint8_t, 2u, 1u, BM_BUS_BLOCK);
BM_STREAM_PAYLOADS(g_blk_reset_stream, uint32_t, BLK_STREAM_DEPTH);
BM_STREAM_BLOCKS(g_blk_reset_stream,             BLK_STREAM_DEPTH);
BM_STREAM_INSTANCE(g_blk_reset_stream,           BLK_STREAM_DEPTH);

/**
 * @brief reset 解冻验证：freeze 后 reader_attach 返回 BM_ERR_BUSY；
 *        reset 后 frozen=0，reader_attach 再次成功。
 *
 * 覆盖 bm_bus_reset() 的核心语义：freeze 的对称解冻面。
 */
void test_reset_unfreeze_allows_reader_attach(void) {
    bm_bus_reader_t r;

    /* freeze → attach 被拒 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_bus_reader_attach(&g_bus_q, &r));

    /* reset → 解冻 → attach 成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_q));
    TEST_ASSERT_EQUAL(0u, (uint32_t)tb_q_storage.frozen);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
}

/**
 * @brief reset 游标归零验证（QUEUE）：写若干项后 reset，write_cur=0、
 *        reader_count=0、reader slot 全部清空（attached=0、overflow_count=0）。
 */
void test_reset_clears_cursors_and_readers_queue(void) {
    bm_bus_reader_t r;
    void *ws;
    uint32_t i;

    /* 写 2 项并 attach 读者 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_q, &r));
    for (i = 0u; i < 2u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
        *(uint32_t *)ws = i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_q));
    }
    /* write_cur 应为 2（已 commit 2 次） */
    TEST_ASSERT_EQUAL_UINT32(2u, bm_atomic_ipc_load_u32(&tb_q_storage.write_cur));
    TEST_ASSERT_EQUAL_UINT32(1u, tb_q_storage.reader_count);

    /* reset */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_q));

    /* 游标归零 */
    TEST_ASSERT_EQUAL_UINT32(0u, bm_atomic_ipc_load_u32(&tb_q_storage.write_cur));
    /* reader_count 归零 */
    TEST_ASSERT_EQUAL_UINT32(0u, tb_q_storage.reader_count);
    /* reader slot 全部清空 */
    TEST_ASSERT_EQUAL_UINT32(0u,
        bm_atomic_ipc_load_u32(&tb_q_storage.readers[r.slot_idx].read_cur));
    TEST_ASSERT_EQUAL_UINT32(0u,
        tb_q_storage.readers[r.slot_idx].overflow_count);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)tb_q_storage.readers[r.slot_idx].attached);
}

/**
 * @brief reset 游标归零验证（SIGNAL）：三读者写若干项后 reset，
 *        write_cur=0、reader_count=0、各 slot 全部清空。
 */
void test_reset_clears_cursors_and_readers_signal(void) {
    bm_bus_reader_t r0, r1;
    void *ws;
    uint32_t i;

    /* attach 2 个读者并写 3 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r0));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_s, &r1));
    for (i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_s, &ws));
        *(uint32_t *)ws = i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_s));
    }
    TEST_ASSERT_EQUAL_UINT32(3u, bm_atomic_ipc_load_u32(&tb_s_storage.write_cur));

    /* reset */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_s));

    TEST_ASSERT_EQUAL_UINT32(0u, bm_atomic_ipc_load_u32(&tb_s_storage.write_cur));
    TEST_ASSERT_EQUAL_UINT32(0u, tb_s_storage.reader_count);
    /* 所有 slot（含未 attach 的）已被清零 */
    for (i = 0u; i < tb_s_storage.max_consumers; i++) {
        TEST_ASSERT_EQUAL_UINT32(0u,
            bm_atomic_ipc_load_u32(&tb_s_storage.readers[i].read_cur));
        TEST_ASSERT_EQUAL_UINT32(0u, tb_s_storage.readers[i].overflow_count);
        TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)tb_s_storage.readers[i].attached);
    }
}

/**
 * @brief reset LATEST 模式：latest_published/reading/writing 复位，
 *        reset 后 acquire_read 返回 BM_ERR_WOULD_BLOCK（无数据），
 *        reader_attach 成功（reader_count 已清零）。
 */
void test_reset_latest_state_cleared(void) {
    bm_bus_reader_t r;
    void *ws;
    const void *s;

    /* 先写一个值并 attach 读者 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 99u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    /* 验证有数据 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, &s));
    bm_bus_release(&r);

    /* reset：清除所有运行期状态 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_l));

    /* latest_published 应为 BM_BUS_LATEST_NONE */
    TEST_ASSERT_EQUAL_UINT32(BM_BUS_LATEST_NONE,
        bm_atomic_ipc_load_u32(&tb_l_storage.latest_published));
    /* latest_reading 应为 BM_BUS_LATEST_NONE */
    TEST_ASSERT_EQUAL_UINT32(BM_BUS_LATEST_NONE,
        bm_atomic_ipc_load_u32(&tb_l_storage.latest_reading));
    /* reader_count 归零，可重新 attach */
    TEST_ASSERT_EQUAL_UINT32(0u, tb_l_storage.reader_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    /* reset 后无数据，acquire_read 应返回 BM_ERR_WOULD_BLOCK */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &s));
}

/**
 * @brief BLOCK 模式 reset：core 状态复位，block_iface/block_ctx 绑定保留不变，
 *        frozen=0（解冻，可重新 bind）。
 *
 * 验证 reset 仅复位 bus core 层状态，不触碰后端绑定——后端有独立生命周期。
 */
void test_reset_block_preserves_binding(void) {
    bm_bus_t     h;
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    const bm_block_backend_iface_t *iface_before;

    TEST_ASSERT_EQUAL(BM_OK,
        bm_stream_init(&g_blk_reset_stream,
                       _bm_stream_payload_g_blk_reset_stream,
                       BLK_STREAM_DEPTH,
                       sizeof(uint32_t)));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&h, &tb_blk_reset_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_bus_bind_block_backend(&h, bm_stream_as_block_backend(), &g_blk_reset_stream));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&h));

    /* 记录绑定前的 block_iface 指针 */
    iface_before = tb_blk_reset_storage.block_iface;
    TEST_ASSERT_NOT_NULL(iface_before);

    /* reset：core 状态复位，binding 保留 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&h));

    /* frozen=0（解冻） */
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)tb_blk_reset_storage.frozen);
    /* block_iface/block_ctx 保持不变 */
    TEST_ASSERT_EQUAL_PTR(iface_before, tb_blk_reset_storage.block_iface);
    TEST_ASSERT_EQUAL_PTR(&g_blk_reset_stream, tb_blk_reset_storage.block_ctx);

    bm_bus_close(&h);
}

/**
 * @brief reset 幂等：连续两次 reset 返回 BM_OK，无副作用。
 */
void test_reset_idempotent(void) {
    /* 先写一项，再连续 reset 两次 */
    void *ws;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_q, &ws));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_abort(&g_bus_q));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_q));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_q));

    /* 两次 reset 后游标仍为 0，frozen=0 */
    TEST_ASSERT_EQUAL_UINT32(0u, bm_atomic_ipc_load_u32(&tb_q_storage.write_cur));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)tb_q_storage.frozen);
}

/**
 * @brief reset 参数校验：h=NULL 返回 BM_ERR_INVALID；h->storage=NULL 返回 BM_ERR_INVALID。
 */
void test_reset_invalid_params(void) {
    bm_bus_t h_no_storage = { .storage = NULL };
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_reset(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_reset(&h_no_storage));
}

/* ------------------------------------------------------------------ */
/* Task 9：bm_bus_latest_read() seqlock 多观察者拷出 API               */
/* ------------------------------------------------------------------ */

/**
 * @brief 多观察者拷出读基本功能：发布后任意次 bm_bus_latest_read 均返回最新值。
 *
 * 连续发布 111 → 222 → 333，三次调用不同 dst 缓冲区，均得到最后发布的值 333；
 * 验证无需 reader_attach、不影响 latest_published/latest_reading 状态。
 */
void test_latest_multi_read_basic(void) {
    void *ws;
    uint32_t dst0 = 0u, dst1 = 0u, dst2 = 0u;

    /* 连续发布 111 → 222 → 333 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 111u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 222u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 333u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    /* 三个「观察者」各自拷出，均得到最新值 333 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst0));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst1));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst2));
    TEST_ASSERT_EQUAL_UINT32(333u, dst0);
    TEST_ASSERT_EQUAL_UINT32(333u, dst1);
    TEST_ASSERT_EQUAL_UINT32(333u, dst2);
}

/**
 * @brief 尚无发布值时 bm_bus_latest_read 返回 BM_ERR_WOULD_BLOCK。
 *
 * open 后、任何 commit 之前 latest_published == BM_BUS_LATEST_NONE。
 */
void test_latest_multi_read_no_data(void) {
    uint32_t dst = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_latest_read(&g_bus_l, &dst));
}

/**
 * @brief 单读者零拷贝借还与多观察者拷出并存：同一 LATEST bus 上两种读法各自正确。
 *
 * 写 555 后：
 *   - bm_bus_latest_read 拷出得 555；
 *   - bm_bus_acquire_read（单读者零拷贝）读到 555；
 *   - acquire_read 持有 latest_reading 标记期间，bm_bus_latest_read 仍返回 BM_OK；
 *   - release 后 bm_bus_latest_read 依然正确。
 */
void test_latest_multi_read_coexists_with_zero_copy(void) {
    bm_bus_reader_t r;
    const uint32_t *slot_ptr;
    uint32_t dst = 0u;
    void *ws;

    /* 写 555 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 555u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    /* 多观察者拷出 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst));
    TEST_ASSERT_EQUAL_UINT32(555u, dst);

    /* 单读者零拷贝借还 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_l, &r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&r, (const void **)&slot_ptr));
    TEST_ASSERT_EQUAL_UINT32(555u, *slot_ptr);

    /* acquire_read 后 latest_reading 已置槽，bm_bus_latest_read 不依赖 latest_reading，
     * 仍可并发拷出 */
    dst = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst));
    TEST_ASSERT_EQUAL_UINT32(555u, dst);

    /* release：清 latest_reading；多观察者路径不受影响 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_release(&r));
    dst = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst));
    TEST_ASSERT_EQUAL_UINT32(555u, dst);
}

/**
 * @brief 非 LATEST 模式调用 bm_bus_latest_read 返回 BM_ERR_NOT_SUPPORTED。
 *
 * QUEUE / SIGNAL 模式不支持多观察者拷出读。
 */
void test_latest_multi_read_wrong_mode(void) {
    uint32_t dst = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_bus_latest_read(&g_bus_q, &dst));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_bus_latest_read(&g_bus_s, &dst));
}

/**
 * @brief 参数校验：h=NULL 或 dst=NULL 返回 BM_ERR_INVALID。
 */
void test_latest_multi_read_invalid_params(void) {
    uint32_t dst = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_latest_read(NULL, &dst));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_latest_read(&g_bus_l, NULL));
}

/**
 * @brief reset 后 latest_seq=0、bm_bus_latest_read 返回 BM_ERR_WOULD_BLOCK。
 *
 * 写 42 后 reset：seq 归零、latest_published=NONE，再次拷出读返回无数据。
 */
void test_latest_multi_read_after_reset(void) {
    void *ws;
    uint32_t dst = 0u;

    /* 写一个值并确认拷出正常 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 42u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_bus_l, &dst));
    TEST_ASSERT_EQUAL_UINT32(42u, dst);

    /* reset：seq 归零、latest_published=NONE */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&g_bus_l));
    TEST_ASSERT_EQUAL_UINT32(0u,
        (uint32_t)bm_atomic_ipc_load_u32(&tb_l_storage.latest_seq));

    /* reset 后无数据 */
    dst = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_latest_read(&g_bus_l, &dst));
}

#ifdef BM_BUS_ALLOW_INTERNAL
/**
 * @brief bm_bus_latest_read_seq 回传稳定 seq：未发布 WOULD_BLOCK、发布后 seq 为偶且随发布前进。
 *
 * 供 bm_tt_schedule seq-delta 判龄使用的内部 API 验证：仅在 BM_BUS_ALLOW_INTERNAL 下可见。
 */
void test_latest_read_seq_returns_stable_seq(void) {
    bm_bus_t bus;
    bm_bus_cfg_t cfg = {0};
    uint32_t got = 0u, seq0 = 0u, seq1 = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&bus, &seqbus_storage, &cfg));
    /* 未发布：WOULD_BLOCK */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_latest_read_seq(&bus, &got, &seq0));
    /* 发布一次 */
    TEST_ASSERT_EQUAL(BM_OK, seqbus_publish(&bus, 0x1234u));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&bus, &got, &seq0));
    TEST_ASSERT_EQUAL_HEX32(0x1234u, got);
    TEST_ASSERT_EQUAL_UINT32(0u, seq0 & 1u);          /* 稳定=偶 */
    /* 再发布：seq 前进（+2） */
    TEST_ASSERT_EQUAL(BM_OK, seqbus_publish(&bus, 0x5678u));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&bus, &got, &seq1));
    TEST_ASSERT_EQUAL_HEX32(0x5678u, got);
    TEST_ASSERT_TRUE((uint32_t)(seq1 - seq0) >= 2u);  /* 无符号 delta 前进 */
    bm_bus_reset(&bus);   /* 幂等复位，供 setUp/tearDown 复用 */
}

/**
 * @brief 参数校验：h=NULL、dst=NULL、out_seq=NULL 均返回 BM_ERR_INVALID。
 */
void test_latest_read_seq_invalid_params(void) {
    uint32_t dst = 0u, seq = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_latest_read_seq(NULL, &dst, &seq));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_latest_read_seq(&g_bus_l, NULL, &seq));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bus_latest_read_seq(&g_bus_l, &dst, NULL));
}

/**
 * @brief 非 LATEST 模式调用 bm_bus_latest_read_seq 返回 BM_ERR_NOT_SUPPORTED。
 *
 * QUEUE / SIGNAL 模式不支持内部 seq 访问器。
 */
void test_latest_read_seq_wrong_mode(void) {
    uint32_t dst = 0u, seq = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_bus_latest_read_seq(&g_bus_q, &dst, &seq));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_bus_latest_read_seq(&g_bus_s, &dst, &seq));
}
#endif /* BM_BUS_ALLOW_INTERNAL */

#ifdef BM_ENABLE_BUS_TEST_HOOK
/* DET-01 seqlock 扩展：模拟写者在拷贝窗口持续推进 seq，使 seqlock 始终失稳，
 * 触达重试上界路径（bm_bus_latest_read 有界验证）。 */
static uint32_t s_multi_read_hook_calls;

static void latest_multi_read_contention_hook(bm_bus_storage_t *st) {
    uint32_t seq = (uint32_t)bm_atomic_ipc_load_u32(&st->latest_seq);
    /* +2 保持偶数但改变序号值，使读侧 seq2 != seq1，迫使重试 */
    bm_atomic_ipc_store_u32(&st->latest_seq, seq + 2u);
    s_multi_read_hook_calls++;
}

/**
 * @brief DET-01 seqlock 扩展：bm_bus_latest_read 写者持续抢占下重试有界并非阻塞返回。
 *
 * 注入 latest_multi_read_contention_hook 模拟写者在每次拷贝窗口推进 seq，
 * 迫使 seqlock 始终失稳。修复前无界循环；修复后至多重试
 * BM_CONFIG_BUS_LATEST_MAX_RETRIES 次即返回 BM_ERR_WOULD_BLOCK，
 * 钩子恰好被调用 MAX 次（DET-01 有界验证）。
 */
void test_latest_multi_read_retry_bounded_under_contention(void) {
    void *ws;
    uint32_t dst = 0u;

    /* 先发布有效值，使 latest_published != NONE，进入 seqlock 循环 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_l, &ws));
    *(uint32_t *)ws = 77u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_l));

    s_multi_read_hook_calls = 0u;
    bm_bus_test_latest_multi_read_hook = latest_multi_read_contention_hook;

    /* 持续抢占：必须有界放弃，返回 WOULD_BLOCK */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_latest_read(&g_bus_l, &dst));

    bm_bus_test_latest_multi_read_hook = NULL;

    /* 钩子恰好被调用 MAX 次（每次 seq 比对失败触发一次） */
    TEST_ASSERT_EQUAL_UINT32(BM_CONFIG_BUS_LATEST_MAX_RETRIES,
                             s_multi_read_hook_calls);
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
    /* Task 7: BLOCK IoC API */
    RUN_TEST(test_block_unbound_returns_invalid);
    RUN_TEST(test_block_bind_wrong_mode_rejected);
    RUN_TEST(test_block_bind_after_freeze_rejected);
    RUN_TEST(test_block_produce_commit_consume_roundtrip);
    RUN_TEST(test_block_produce_abort_no_ready);
    RUN_TEST(test_block_ts_ns_passthrough);
    RUN_TEST(test_block_ts_ns_zero_uses_uptime);
    RUN_TEST(test_block_owner_cpu_passthrough);
    RUN_TEST(test_freeze_idempotent);
    RUN_TEST(test_acquire_write_after_freeze_still_allowed);
#ifdef BM_ENABLE_BUS_TEST_HOOK
    /* DET-01：LATEST 重试上界（需 BM_ENABLE_BUS_TEST_HOOK 编入测试缝）*/
    RUN_TEST(test_latest_read_retry_bounded_under_contention);
#endif
    /* Task 8：bm_bus_reset() 生命周期对称 */
    RUN_TEST(test_reset_unfreeze_allows_reader_attach);
    RUN_TEST(test_reset_clears_cursors_and_readers_queue);
    RUN_TEST(test_reset_clears_cursors_and_readers_signal);
    RUN_TEST(test_reset_latest_state_cleared);
    RUN_TEST(test_reset_block_preserves_binding);
    RUN_TEST(test_reset_idempotent);
    RUN_TEST(test_reset_invalid_params);
    /* Task 9：bm_bus_latest_read seqlock 多观察者拷出 API */
    RUN_TEST(test_latest_multi_read_basic);
    RUN_TEST(test_latest_multi_read_no_data);
    RUN_TEST(test_latest_multi_read_coexists_with_zero_copy);
    RUN_TEST(test_latest_multi_read_wrong_mode);
    RUN_TEST(test_latest_multi_read_invalid_params);
    RUN_TEST(test_latest_multi_read_after_reset);
#ifdef BM_ENABLE_BUS_TEST_HOOK
    /* DET-01 seqlock 扩展：bm_bus_latest_read 重试上界（需 BM_ENABLE_BUS_TEST_HOOK 编入测试缝）*/
    RUN_TEST(test_latest_multi_read_retry_bounded_under_contention);
#endif
#ifdef BM_BUS_ALLOW_INTERNAL
    /* Task 10：bm_bus_latest_read_seq 内部 seq 访问器（需 BM_BUS_ALLOW_INTERNAL 编入测试缝）*/
    RUN_TEST(test_latest_read_seq_returns_stable_seq);
    RUN_TEST(test_latest_read_seq_invalid_params);
    RUN_TEST(test_latest_read_seq_wrong_mode);
#endif
    return UNITY_END();
}
