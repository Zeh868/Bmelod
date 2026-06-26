/**
 * @file test_health.c
 * @brief 运行时健康快照聚合层单元测试
 *
 * 覆盖：NULL 参数安全性、event 丢弃/重入拒绝字段与单独 getter 一致、
 * hrt deadline miss 字段与 getter 一致（非零）、ticker dropped 字段与 getter 一致（非零）、
 * bus write 字段与 getter 一致（非零）、多 bus 折入累加正确。
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-26
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-26       0.1            zeh            初稿
 */

#include "unity.h"
#include "bm/hybrid/bm_health.h"
#include "bm/hybrid/bm_hrt.h"
#include "bm/hybrid/bm_ticker.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_bus.h"
#include "bm_log.h"
#include "bm_hal_timer_native.h"
#include "bm_hal_uptime_native.h"

/* ========================================================================= */
/* 测试用 bus 定义（文件域 static，各测试通过 bm_bus_open 重置后使用）         */
/* ========================================================================= */

BM_BUS_DEFINE(th_bus_a, uint32_t, 4u, 1u, BM_BUS_QUEUE);
BM_BUS_DEFINE(th_bus_b, uint32_t, 4u, 1u, BM_BUS_QUEUE);

static bm_bus_t g_bus_a;
static bm_bus_t g_bus_b;

/* HRT 回调计数（各测试局部，setUp 清零） */
static uint32_t g_hrt_fired;

static void hrt_slot_cb(void *ctx) {
    (void)ctx;
    g_hrt_fired++;
}

/* event 回调占位（仅用于 subscribe 合法性）*/
static void dummy_event_cb(const bm_event_t *ev, void *user_data) {
    (void)ev;
    (void)user_data;
}

/* ========================================================================= */
/* Unity setUp / tearDown                                                     */
/* ========================================================================= */

void setUp(void) {
    bm_bus_cfg_t cfg = {.owner_cpu = 0u};

    BM_LOGI("test_health", "setUp");
    g_hrt_fired = 0u;

    /* 重置全局子系统 */
    bm_event_reset();
    bm_hal_uptime_native_reset();
    bm_hal_timer_native_reset_ticks();
    bm_hal_timer_native_set_init_result(BM_OK);
    bm_hrt_reset();
    bm_ticker_reset();

    /* 重置 bus（bm_bus_open 清零 storage 运行期状态） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_bus_a, &th_bus_a_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_bus_b, &th_bus_b_storage, &cfg));
}

void tearDown(void) {
    bm_hrt_reset();
    bm_ticker_reset();
    bm_event_reset();
    bm_bus_close(&g_bus_a);
    bm_bus_close(&g_bus_b);
}

/* ========================================================================= */
/* 测试：NULL 参数安全性                                                       */
/* ========================================================================= */

/**
 * @brief bm_health_snapshot(NULL) 不崩溃、无副作用
 */
void test_health_snapshot_null_noop(void) {
    bm_health_snapshot(NULL);
    /* 若无崩溃则通过 */
}

/**
 * @brief bm_health_snapshot_add_bus NULL 参数返回 BM_ERR_INVALID
 */
void test_health_add_bus_null_returns_invalid(void) {
    bm_health_snapshot_t snap;

    bm_health_snapshot(&snap);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_health_snapshot_add_bus(NULL, &g_bus_a));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_health_snapshot_add_bus(&snap, NULL));
}

/* ========================================================================= */
/* 测试：event 字段与 getter 一致                                              */
/* ========================================================================= */

/**
 * @brief 触发 event_dropped（队列满）后快照字段与 getter 一致且非零
 */
void test_health_event_dropped_matches_getter(void) {
    bm_health_snapshot_t snap;

    /* 填满事件队列 */
    bm_event_register_type(1u, "T1");
    while (bm_event_publish_copy(1u, 1u, NULL, 0u) == BM_OK) {
        /* 填满为止 */
    }
    /* 再发布一次，触发 dropped 计数 */
    (void)bm_event_publish_copy(1u, 1u, NULL, 0u);

    bm_health_snapshot(&snap);

    TEST_ASSERT_EQUAL(bm_event_get_dropped_count(),
                      snap.event_dropped);
    TEST_ASSERT_GREATER_THAN(0u, snap.event_dropped);

    /* dispatch_skipped 与 reentrancy_rejected 无论是否为 0，应与 getter 一致 */
    TEST_ASSERT_EQUAL(bm_event_get_dispatch_skipped_count(),
                      snap.event_dispatch_skipped);
    TEST_ASSERT_EQUAL(bm_event_get_reentrancy_rejected_count(),
                      snap.event_reentrancy_rejected);
}

/**
 * @brief 冻结后调用 subscribe 触发 reentrancy_rejected，快照字段与 getter 一致且非零
 */
void test_health_event_reentrancy_rejected_matches_getter(void) {
    bm_health_snapshot_t snap;

    bm_event_register_type(2u, "T2");
    /* 冻结订阅表；冻结后 subscribe 返回 BM_ERR_BUSY 并增 reentrancy_rejected */
    bm_event_freeze_subscriptions();
    /* 使用合法回调，触发冻结后拒绝路径（NULL 回调会在冻结校验之前被拒绝） */
    (void)bm_event_subscribe(2u, dummy_event_cb, NULL, NULL);

    bm_health_snapshot(&snap);

    TEST_ASSERT_EQUAL(bm_event_get_reentrancy_rejected_count(),
                      snap.event_reentrancy_rejected);
    TEST_ASSERT_GREATER_THAN(0u, snap.event_reentrancy_rejected);
}

/* ========================================================================= */
/* 测试：hrt_deadline_missed_total 字段与 getter 一致                          */
/* ========================================================================= */

/**
 * @brief 触发 HRT deadline miss 后快照字段与 getter 一致且非零
 */
void test_health_hrt_deadline_missed_matches_getter(void) {
    static const bm_hrt_slot_t slots[] = {
        {1000u, BM_HRT_TRIGGER_TIMER, hrt_slot_cb, NULL, "health_hrt"},
    };
    bm_health_snapshot_t snap;

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());

    /* jump_ticks 模拟超过一个周期未触发，制造 deadline miss */
    bm_hal_timer_native_jump_ticks(25u);

    bm_health_snapshot(&snap);

    TEST_ASSERT_EQUAL(bm_hrt_get_deadline_missed_total(),
                      snap.hrt_deadline_missed_total);
    TEST_ASSERT_GREATER_THAN(0u, snap.hrt_deadline_missed_total);
}

/* ========================================================================= */
/* 测试：ticker_dropped_total 字段与 getter 一致                               */
/* ========================================================================= */

/**
 * @brief 事件队列满时 ticker poll 触发 dropped，快照字段与 getter 一致且非零
 */
void test_health_ticker_dropped_matches_getter(void) {
    static const bm_ticker_slot_t t_slots[] = {
        {10u, 3u, 1u, "health_tick"},
    };
    bm_health_snapshot_t snap;

    bm_event_register_type(3u, "T3");

    /* 填满事件队列使 ticker poll 必然失败 */
    while (bm_event_publish_copy(3u, 1u, NULL, 0u) == BM_OK) {
        /* 填满 */
    }

    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(t_slots, 1u));

    /* 推进时间使 ticker 到期 */
    bm_hal_uptime_native_advance_us(20000u);
    (void)bm_ticker_poll();

    bm_health_snapshot(&snap);

    TEST_ASSERT_EQUAL(bm_ticker_get_dropped_total(),
                      snap.ticker_dropped_total);
    TEST_ASSERT_GREATER_THAN(0u, snap.ticker_dropped_total);
}

/* ========================================================================= */
/* 测试：bus_write_total 字段正确                                              */
/* ========================================================================= */

/**
 * @brief 向 bus 写入后折入快照，bus_write_total 与 stats.write_count 一致且非零
 */
void test_health_bus_write_matches_getter(void) {
    bm_bus_reader_t r;
    bm_bus_stats_t stats;
    bm_health_snapshot_t snap;
    void *slot;

    /* attach + freeze 以允许写操作 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_a, &r));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_a));

    /* 写入 2 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_a, &slot));
    *(uint32_t *)slot = 1u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_a));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_a, &slot));
    *(uint32_t *)slot = 2u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_a));

    bm_health_snapshot(&snap);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_snapshot_add_bus(&snap, &g_bus_a));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_stats(&g_bus_a, &stats));
    TEST_ASSERT_EQUAL(stats.write_count, snap.bus_write_total);
    TEST_ASSERT_GREATER_THAN(0u, snap.bus_write_total);

    /* overflow_count 由 bm_bus_stats 决定（当前实现为 0，此处验证一致性） */
    TEST_ASSERT_EQUAL(stats.overflow_count, snap.bus_overflow_total);
}

/* ========================================================================= */
/* 测试：多 bus 折入累加                                                        */
/* ========================================================================= */

/**
 * @brief 两个 bus 分别写入后折入同一快照，bus_write_total 等于两者之和
 */
void test_health_multi_bus_add_accumulates(void) {
    bm_bus_reader_t r_a, r_b;
    bm_bus_stats_t stats_a, stats_b;
    bm_health_snapshot_t snap;
    void *slot;

    /* Bus A：attach + freeze，写 2 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_a, &r_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_a, &slot));
    *(uint32_t *)slot = 10u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_a, &slot));
    *(uint32_t *)slot = 20u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_a));

    /* Bus B：attach + freeze，写 3 项 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reader_attach(&g_bus_b, &r_b));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&g_bus_b));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_b, &slot));
    *(uint32_t *)slot = 100u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_b));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_b, &slot));
    *(uint32_t *)slot = 200u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_b));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&g_bus_b, &slot));
    *(uint32_t *)slot = 300u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&g_bus_b));

    /* 取快照并折入两个 bus */
    bm_health_snapshot(&snap);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_snapshot_add_bus(&snap, &g_bus_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_health_snapshot_add_bus(&snap, &g_bus_b));

    /* 验证累加正确性 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_stats(&g_bus_a, &stats_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_stats(&g_bus_b, &stats_b));
    TEST_ASSERT_EQUAL(stats_a.write_count + stats_b.write_count,
                      snap.bus_write_total);
    TEST_ASSERT_EQUAL(5u, snap.bus_write_total);
}

/* ========================================================================= */
/* main                                                                       */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_health_snapshot_null_noop);
    RUN_TEST(test_health_add_bus_null_returns_invalid);
    RUN_TEST(test_health_event_dropped_matches_getter);
    RUN_TEST(test_health_event_reentrancy_rejected_matches_getter);
    RUN_TEST(test_health_hrt_deadline_missed_matches_getter);
    RUN_TEST(test_health_ticker_dropped_matches_getter);
    RUN_TEST(test_health_bus_write_matches_getter);
    RUN_TEST(test_health_multi_bus_add_accumulates);
    return UNITY_END();
}
