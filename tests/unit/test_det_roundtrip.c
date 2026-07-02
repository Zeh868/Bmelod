/**
 * @file test_det_roundtrip.c
 * @brief L1 功能确定性——A3 框架 round-trip 可复现性
 *
 * A3-a：bus LATEST publish→latest_read 同值（DET_REPRO_RUNS 次）。
 * A3-b：event register→subscribe→freeze→publish→process 两遍跑，
 *        分发给多订阅者的顺序逐位相同。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include "unity.h"
#include "bm/core/bm_bus.h"
#include "bm/core/bm_event.h"
#include <stdint.h>
#include <string.h>

/** 可复现性重复次数 */
#define DET_RT_RUNS 64u

/* ---------- A3-a bus 资源（文件作用域静态，零动态分配） ---------- */
/** @brief bus LATEST 测试实例：容量 4，单读者 */
BM_BUS_DEFINE(det_rt_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

/* ---------- A3-b event 资源 ---------- */
/** 测试事件类型 ID（0x0F = 15，BM_CONFIG_MAX_EVENT_TYPES - 1；高位避冲突且在有效域内） */
#define DET_RT_EVT_TYPE ((bm_event_type_t)0x0Fu)

/** 分发顺序日志：记录两遍 run 中各回调被调用的顺序编号 */
static uint32_t g_det_dispatch_log[4u];
/** 当前已记录的回调调用次数 */
static uint32_t g_det_dispatch_cnt;

/**
 * @brief 事件回调 A：记录"自己是第几个被调用的"到 g_det_dispatch_log
 * @param ev 事件指针（只读，不使用）
 * @param ud 用户数据（不使用）
 */
static void det_rt_cb_a(const bm_event_t *ev, void *ud)
{
    (void)ev; (void)ud;
    if (g_det_dispatch_cnt < 4u) {
        g_det_dispatch_log[g_det_dispatch_cnt] = 1u; /* 回调 A 的标识 */
        g_det_dispatch_cnt++;
    }
}

/**
 * @brief 事件回调 B：记录"自己是第几个被调用的"
 * @param ev 事件指针（只读，不使用）
 * @param ud 用户数据（不使用）
 */
static void det_rt_cb_b(const bm_event_t *ev, void *ud)
{
    (void)ev; (void)ud;
    if (g_det_dispatch_cnt < 4u) {
        g_det_dispatch_log[g_det_dispatch_cnt] = 2u; /* 回调 B 的标识 */
        g_det_dispatch_cnt++;
    }
}

void setUp(void)
{
    /* event 全局状态每 test 前复位，确保测试间隔离 */
    bm_event_reset();
}

void tearDown(void) {}

/**
 * @brief A3-a：bus LATEST publish→latest_read 同值，DET_RT_RUNS 遍逐位相同。
 *
 * 证明 bus round-trip 在同核稳态下具有功能确定性：
 * 每次 publish 一个已知值后 latest_read 取回，断言取回值与发布值逐位相同。
 */
void test_det_repro_bus_latest_roundtrip(void)
{
    bm_bus_t      bus;
    bm_bus_cfg_t  cfg;
    void         *wslot;
    uint32_t      readback;
    uint32_t      expected;
    uint32_t      i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.owner_cpu = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&bus, &det_rt_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_freeze(&bus));

    for (i = 0u; i < DET_RT_RUNS; i++) {
        expected = 0xAB000000u ^ i;

        TEST_ASSERT_EQUAL(BM_OK, bm_bus_acquire_write(&bus, &wslot));
        *(uint32_t *)wslot = expected;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_commit(&bus));

        readback = 0u;
        TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&bus, &readback));
        /* 取回值与发布值逐位相同 */
        TEST_ASSERT_EQUAL_UINT32(expected, readback);
    }

    /* 先复位再 close：bm_bus_reset 操作 storage，必须在 bm_bus_close 解绑前调用，
       否则 close 后 reset 返回 BM_ERR_INVALID（bm_bus.h:220 close 解除 storage 绑定）。 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_reset(&bus));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_close(&bus));
}

/**
 * @brief A3-b：event register→subscribe→freeze→publish→process 两遍跑，
 *        分发顺序完全相同。
 *
 * 订阅表冻结后链表不可变；两遍以相同顺序注册同两个回调，
 * 则两遍分发时回调被调用的顺序必须逐位一致（spec §4.1 可复现性）。
 */
void test_det_repro_event_dispatch_order(void)
{
    uint32_t log_run1[2u];
    uint32_t log_run2[2u];
    int      processed;

    /* ---- 第一遍 ---- */
    bm_event_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(DET_RT_EVT_TYPE, "DET_RT"));
    TEST_ASSERT_EQUAL(BM_OK, bm_event_subscribe(DET_RT_EVT_TYPE, det_rt_cb_a, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_event_subscribe(DET_RT_EVT_TYPE, det_rt_cb_b, NULL, NULL));
    bm_event_freeze_subscriptions();

    g_det_dispatch_cnt = 0u;
    memset(g_det_dispatch_log, 0, sizeof(g_det_dispatch_log));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(DET_RT_EVT_TYPE, 0u, NULL, 0u));
    processed = bm_event_process(1u);
    TEST_ASSERT_EQUAL_INT(1, processed); /* 1 条事件被处理 */
    TEST_ASSERT_EQUAL_UINT32(2u, g_det_dispatch_cnt); /* cb_a + cb_b 各调一次 */
    log_run1[0] = g_det_dispatch_log[0];
    log_run1[1] = g_det_dispatch_log[1];

    /* ---- 第二遍：相同注册顺序 ---- */
    bm_event_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_event_register_type(DET_RT_EVT_TYPE, "DET_RT"));
    TEST_ASSERT_EQUAL(BM_OK, bm_event_subscribe(DET_RT_EVT_TYPE, det_rt_cb_a, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_event_subscribe(DET_RT_EVT_TYPE, det_rt_cb_b, NULL, NULL));
    bm_event_freeze_subscriptions();

    g_det_dispatch_cnt = 0u;
    memset(g_det_dispatch_log, 0, sizeof(g_det_dispatch_log));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_event_publish_copy(DET_RT_EVT_TYPE, 0u, NULL, 0u));
    processed = bm_event_process(1u);
    TEST_ASSERT_EQUAL_INT(1, processed);
    TEST_ASSERT_EQUAL_UINT32(2u, g_det_dispatch_cnt);
    log_run2[0] = g_det_dispatch_log[0];
    log_run2[1] = g_det_dispatch_log[1];

    /* 两遍分发顺序逐位相同 */
    TEST_ASSERT_EQUAL_UINT32(log_run1[0], log_run2[0]);
    TEST_ASSERT_EQUAL_UINT32(log_run1[1], log_run2[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_det_repro_bus_latest_roundtrip);
    RUN_TEST(test_det_repro_event_dispatch_order);
    return UNITY_END();
}
