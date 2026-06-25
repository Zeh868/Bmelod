/**
 * @file test_bus_migration.c
 * @brief bm_snapshot / bm_channel 迁移回归测试
 *
 * 原有语义改跑在 bm_bus LATEST（snapshot）与 QUEUE（channel）之上，
 * 验证收编后行为无回归。
 *
 * 语义映射：
 *   - snapshot → LATEST（单读者 SPSC，三缓冲覆盖式，读到最新值）
 *   - channel  → QUEUE（保序不丢，写满拒绝，读空 BM_ERR_WOULD_BLOCK）
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       0.1            zeh            Phase 1 Task 8 初稿：snapshot/channel 迁移回归
 *
 */
#include "unity.h"
#include "bm/core/bm_bus.h"
#include <string.h>

/* ---- snapshot → LATEST 迁移 ---- */
/* 原：BM_SNAPSHOT_DEFINE(my_snap, uint32_t) + BM_SNAPSHOT_PUBLISH/READ */
/* 新：BM_BUS_DEFINE(my_snap, uint32_t, 3u, 1u, BM_BUS_LATEST) */
BM_BUS_DEFINE(migr_snap, uint32_t, 3u, 1u, BM_BUS_LATEST);

/* ---- channel → QUEUE 迁移 ---- */
/* 原：BM_CHANNEL_DEFINE(my_ch, uint32_t, 8) + bm_channel_send/recv */
/* 新：BM_BUS_DEFINE(my_ch, uint32_t, 8u, 1u, BM_BUS_QUEUE) */
BM_BUS_DEFINE(migr_ch, uint32_t, 8u, 1u, BM_BUS_QUEUE);

static bm_bus_t g_snap, g_ch;
static bm_bus_reader_t g_snap_r, g_ch_r;

/**
 * @brief 每个用例前：重置 bus，附加唯一读者
 */
void setUp(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_snap, &migr_snap_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_ch,   &migr_ch_storage,   &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_snap, &g_snap_r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_ch,   &g_ch_r));
}

/**
 * @brief 每个用例后：关闭 bus
 */
void tearDown(void) {
    bm_bus_close(&g_snap);
    bm_bus_close(&g_ch);
}

/* ================================================================== */
/* snapshot 迁移用例                                                    */
/* ================================================================== */

/**
 * @brief snapshot 迁移：写者 publish，读者 read 得最新值
 *
 * 原 BM_SNAPSHOT_PUBLISH(snap, 100) / BM_SNAPSHOT_PUBLISH(snap, 200) 后
 * BM_SNAPSHOT_READ(snap) == 200。迁移后用 acquire_write/commit + acquire_read/release
 * 实现相同语义：连续两次覆盖写，读到最后一次写入的 200。
 */
void test_migr_snapshot_publish_read(void) {
    void *ws;
    const uint32_t *s;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_snap, &ws));
    *(uint32_t *)ws = 100u;
    bm_bus_commit(&g_snap);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_snap, &ws));
    *(uint32_t *)ws = 200u;
    bm_bus_commit(&g_snap);

    /* 读最新值 200 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_snap_r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(200u, *s);
    bm_bus_release(&g_snap_r);
}

/**
 * @brief snapshot 迁移：多次覆盖写，读者始终得到最后一次写的值
 *
 * 原 bm_snapshot 三缓冲覆盖语义：写 10 次，读者读到最后一次 (10*7=70)。
 * LATEST 三缓冲 commit 语义与 bm_snapshot 等价：每次 commit 更新 latest_published。
 */
void test_migr_snapshot_overwrite_semantics(void) {
    void *ws;
    const uint32_t *s;
    uint32_t i;

    for (i = 1u; i <= 10u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_snap, &ws));
        *(uint32_t *)ws = i * 7u;
        bm_bus_commit(&g_snap);
    }
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_snap_r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(70u, *s);  /* 10 * 7 */
    bm_bus_release(&g_snap_r);
}

/* ================================================================== */
/* channel 迁移用例                                                     */
/* ================================================================== */

/**
 * @brief channel 迁移：send(写) / recv(读) 保序
 *
 * 原 bm_channel_send 5 次后 bm_channel_recv 按序取出 1..5。
 * QUEUE 保序语义不变：acquire_write/commit 5 次后 acquire_read/release 依次得到 1..5；
 * 最后一次 acquire_read 返回 BM_ERR_WOULD_BLOCK（与原 bm_channel_recv 空返回码一致）。
 */
void test_migr_channel_send_recv_ordered(void) {
    void *ws;
    const uint32_t *s;
    uint32_t i;

    /* 发送 5 项 */
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_ch, &ws));
        *(uint32_t *)ws = i + 1u;
        bm_bus_commit(&g_ch);
    }
    /* 接收，保序验证 */
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_ch_r, (const void **)&s));
        TEST_ASSERT_EQUAL_UINT32(i + 1u, *s);
        bm_bus_release(&g_ch_r);
    }
    /* 队列空：与原 bm_channel_recv 空返回 BM_ERR_WOULD_BLOCK 一致 */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&g_ch_r, (const void **)&s));
}

/**
 * @brief channel 迁移：overflow 场景——写端满拒绝、不丢（与 bm_channel_send 一致）
 *
 * 原 bm_channel_send 满时返回 BM_ERR_OVERFLOW，不覆盖未读。
 * QUEUE acquire_write 满时同样返回 BM_ERR_OVERFLOW，首项保持完整（不丢、保序）。
 * cap=8，可存 cap-1=7 项；读者 g_ch_r 不消费。
 */
void test_migr_channel_overflow_detected(void) {
    void *ws;
    const uint32_t *s;
    uint32_t i;

    /* cap=8，保留一槽，可存 cap-1=7 项；读者 g_ch_r 不消费 */
    for (i = 0u; i < 7u; i++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_ch, &ws));
        *(uint32_t *)ws = i;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_ch));
    }
    /* 第 8 次：环满 → 写端 acquire_write 立即返回 BM_ERR_OVERFLOW，不覆盖未读 */
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_bus_acquire_write(&g_ch, &ws));
    /* 未读数据完好保留：读者读出的首项仍是最早写入的 0（不丢、保序） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&g_ch_r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(0u, *s);
    bm_bus_release(&g_ch_r);
    /* 腾出一槽后写端可继续（不丢语义下写者自行重试） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_ch, &ws));
    *(uint32_t *)ws = 7u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_ch));
}

/**
 * @brief channel 迁移：reset 等价（close + open 重新打开）
 *
 * 原 bm_channel_reset 清空游标使队列归零。
 * QUEUE 等价操作：bm_bus_close + bm_bus_open 重置 storage，read/write 游标归零；
 * 验证两项行为：
 *   1. 新 reader_attach 后队列读空返回 BM_ERR_WOULD_BLOCK（与原 bm_channel_recv 空一致）；
 *   2. reset 后 acquire_write/commit 新值，再 acquire_read 能取回该新值
 *      （验证 reset 后游标归零的正向读写）。
 */
void test_migr_channel_reset_equivalent(void) {
    bm_bus_reader_t fresh_r;
    void *ws;
    const uint32_t *s;
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_ch, &ws));
    bm_bus_commit(&g_ch);

    /* 等价 reset：close + open 清零游标 */
    bm_bus_close(&g_ch);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_ch, &migr_ch_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_ch, &fresh_r));

    /* 验证 1：重置后队列为空（游标归零，历史数据不可见） */
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&fresh_r, (const void **)&s));

    /* 验证 2：reset 后游标归零的正向读写——写入新值 42 后读者能取回 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_ch, &ws));
    *(uint32_t *)ws = 42u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_ch));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_read(&fresh_r, (const void **)&s));
    TEST_ASSERT_EQUAL_UINT32(42u, *s);
    bm_bus_release(&fresh_r);
}

/* ================================================================== */
/* main                                                                 */
/* ================================================================== */

/**
 * @brief 测试入口，逐一注册并运行所有迁移回归用例
 *
 * @return Unity 测试结果（0=全通过，非 0=有失败）
 */
int main(void) {
    UNITY_BEGIN();
    /* snapshot → LATEST */
    RUN_TEST(test_migr_snapshot_publish_read);
    RUN_TEST(test_migr_snapshot_overwrite_semantics);
    /* channel → QUEUE */
    RUN_TEST(test_migr_channel_send_recv_ordered);
    RUN_TEST(test_migr_channel_overflow_detected);
    RUN_TEST(test_migr_channel_reset_equivalent);
    return UNITY_END();
}
