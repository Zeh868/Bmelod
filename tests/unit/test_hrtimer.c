/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_hrtimer.c
 * @brief 高精度 Timer HAL/drv 与 native_sim 后端单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增高精度 Timer 单测
 * 2026-07-28       1.1            zeh            补 PERIODIC 回调内 stop/重武装、
 *                                             大跨度 advance 有界、set_compare
 *                                             保留模式与运行态用例
 */
#include "unity.h"
#include "hal/bm_hal_hrtimer.h"
#include "bm_hal_hrtimer_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

static uint32_t s_cb_count;
static const bm_hal_hrtimer_t *s_cb_dev;
static void     *s_cb_user;

void setUp(void) {
    bm_hal_hrtimer_native_reset();
    s_cb_count = 0u;
    s_cb_dev   = NULL;
    s_cb_user  = NULL;
}

void tearDown(void) {
}

static void test_hrtimer_callback(const bm_hal_hrtimer_t *dev, void *user) {
    s_cb_count++;
    s_cb_dev   = dev;
    s_cb_user  = user;
}

static void test_hrtimer_init_and_query(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));

    TEST_ASSERT_EQUAL(1000000u, bm_hal_hrtimer_get_freq(&bm_native_hrtimer0));
    TEST_ASSERT_EQUAL(1000u, bm_hal_hrtimer_get_resolution_ns(&bm_native_hrtimer0));
    TEST_ASSERT_EQUAL(1u, bm_hal_hrtimer_get_min_period_us(&bm_native_hrtimer0));
    TEST_ASSERT_EQUAL(3600000000u,
        bm_hal_hrtimer_get_max_period_us(&bm_native_hrtimer0));
}

static void test_hrtimer_periodic(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_callback, (void *)0xABCDu));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    bm_hal_hrtimer_native_advance_us(50u);
    TEST_ASSERT_EQUAL(0u, s_cb_count);

    bm_hal_hrtimer_native_advance_us(60u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);
    TEST_ASSERT_EQUAL(&bm_native_hrtimer0, s_cb_dev);
    TEST_ASSERT_EQUAL((void *)0xABCDu, s_cb_user);

    bm_hal_hrtimer_native_advance_us(100u);
    TEST_ASSERT_EQUAL(2u, s_cb_count);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_stop(&bm_native_hrtimer0));
    bm_hal_hrtimer_native_advance_us(200u);
    TEST_ASSERT_EQUAL(2u, s_cb_count);
}

static void test_hrtimer_oneshot(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_callback, NULL));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_ONESHOT, 200u));

    bm_hal_hrtimer_native_advance_us(199u);
    TEST_ASSERT_EQUAL(0u, s_cb_count);

    bm_hal_hrtimer_native_advance_us(5u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);

    /* 单次模式应已自动停止 */
    bm_hal_hrtimer_native_advance_us(500u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);
}

static void test_hrtimer_set_compare_dynamic(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_callback, NULL));

    /* set_compare 不隐含启动：未 start 时不触发 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 150u));
    bm_hal_hrtimer_native_advance_us(200u);
    TEST_ASSERT_EQUAL(0u, s_cb_count);

    /* 周期模式运行中变速：保留 PERIODIC 模式，仅更新下一次到期 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 300u));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 150u));

    bm_hal_hrtimer_native_advance_us(140u); /* t=340 < 到期 350 */
    TEST_ASSERT_EQUAL(0u, s_cb_count);

    bm_hal_hrtimer_native_advance_us(20u);  /* t=360 >= 350，触发一次 */
    TEST_ASSERT_EQUAL(1u, s_cb_count);

    /* 再次动态改比较值，周期模式仍保留 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 50u));
    bm_hal_hrtimer_native_advance_us(50u);  /* t=410 >= 410，触发 */
    TEST_ASSERT_EQUAL(2u, s_cb_count);

    bm_hal_hrtimer_native_advance_us(50u);  /* t=460 >= 460，再次触发证明 PERIODIC 保留 */
    TEST_ASSERT_EQUAL(3u, s_cb_count);
}

static void test_hrtimer_stats(void) {
    bm_hrtimer_stats_t stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_callback, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    bm_hal_hrtimer_native_advance_us(205u);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_EQUAL(2u, stats.irq_count);

    /* 大幅跳变导致 deadline miss */
    bm_hal_hrtimer_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_GREATER_THAN(0u, stats.deadline_miss_count);
}

static uint32_t s_cb0_count;
static uint32_t s_cb1_count;

static void test_hrtimer_cb0(const bm_hal_hrtimer_t *dev, void *user) {
    (void)dev;
    (void)user;
    s_cb0_count++;
}

static void test_hrtimer_cb1(const bm_hal_hrtimer_t *dev, void *user) {
    (void)dev;
    (void)user;
    s_cb1_count++;
}

static void test_hrtimer_multi_instance(void) {
    s_cb0_count = 0u;
    s_cb1_count = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer1, NULL));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_cb0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer1,
                                    test_hrtimer_cb1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer1,
                             BM_HRTIMER_MODE_PERIODIC, 250u));

    bm_hal_hrtimer_native_advance_us(500u);
    TEST_ASSERT_EQUAL(5u, s_cb0_count);
    TEST_ASSERT_EQUAL(2u, s_cb1_count);
}

static void test_hrtimer_invalid_mode(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_hrtimer_start(&bm_native_hrtimer0, 99u, 100u));
}

static void test_hrtimer_invalid_period(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 0u));
}

static void test_hrtimer_no_backend(void) {
    const bm_hal_hrtimer_t no_backend = { NULL, NULL };
    bm_hrtimer_stats_t stats;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_init(&no_backend, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_start(&no_backend,
                             BM_HRTIMER_MODE_PERIODIC, 100u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_stop(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_set_compare(&no_backend, 100u));
    TEST_ASSERT_EQUAL(0u, bm_hal_hrtimer_get_freq(&no_backend));
    TEST_ASSERT_EQUAL(0u, bm_hal_hrtimer_get_resolution_ns(&no_backend));
    TEST_ASSERT_EQUAL(0u, bm_hal_hrtimer_get_max_period_us(&no_backend));
    TEST_ASSERT_EQUAL(0u, bm_hal_hrtimer_get_min_period_us(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_get_stats(&no_backend, &stats));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_hrtimer_set_callback(&no_backend, test_hrtimer_callback, NULL));
}

static void test_hrtimer_unregister_callback(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_callback, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    bm_hal_hrtimer_native_advance_us(100u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);

    /* cb 为 NULL 时取消注册 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0, NULL, NULL));
    bm_hal_hrtimer_native_advance_us(200u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);
}

static uint32_t s_stop_cb_count;

static void test_hrtimer_stop_in_cb(const bm_hal_hrtimer_t *dev, void *user) {
    (void)user;
    s_stop_cb_count++;
    if (s_stop_cb_count >= 2u) {
        (void)bm_hal_hrtimer_stop(dev);
    }
}

static void test_hrtimer_periodic_stop_in_callback(void) {
    bm_hrtimer_stats_t stats;

    s_stop_cb_count = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_stop_in_cb, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    /* 回调内 stop 应被尊重：大跨度 advance 也只触发 2 次 */
    bm_hal_hrtimer_native_advance_us(1000u);
    TEST_ASSERT_EQUAL(2u, s_stop_cb_count);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_EQUAL(2u, stats.irq_count);
}

static uint32_t s_rearm_cb_count;

static void test_hrtimer_rearm_in_cb(const bm_hal_hrtimer_t *dev, void *user) {
    (void)user;
    s_rearm_cb_count++;
    if (s_rearm_cb_count == 1u) {
        /* 回调内重新 start：到期时刻从新基准计算，不被 += period 覆盖 */
        (void)bm_hal_hrtimer_start(dev, BM_HRTIMER_MODE_PERIODIC, 500u);
    }
}

static void test_hrtimer_periodic_rearm_in_callback(void) {
    s_rearm_cb_count = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_rearm_in_cb, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 100u));

    /* t=150 触发第 1 次并重新 start：下次到期应为 150+500=650 */
    bm_hal_hrtimer_native_advance_us(150u);
    TEST_ASSERT_EQUAL(1u, s_rearm_cb_count);

    bm_hal_hrtimer_native_advance_us(400u); /* t=550 < 650，不再触发 */
    TEST_ASSERT_EQUAL(1u, s_rearm_cb_count);

    bm_hal_hrtimer_native_advance_us(100u); /* t=650 >= 650，触发第 2 次 */
    TEST_ASSERT_EQUAL(2u, s_rearm_cb_count);
}

static void test_hrtimer_large_advance_no_callback(void) {
    bm_hrtimer_stats_t stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    /* 不注册回调 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 1u));

    /* 大跨度 advance + 小周期：算术合并一次性推进，不逐次循环 */
    bm_hal_hrtimer_native_advance_us(3600000000u);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_EQUAL_UINT32(3600000000u, stats.irq_count);
    TEST_ASSERT_GREATER_THAN(0u, stats.deadline_miss_count);

    /* 到期时刻已推进到未来：小步 advance 逐次触发 */
    bm_hal_hrtimer_native_advance_us(1u);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_EQUAL_UINT32(3600000001u, stats.irq_count);
}

static uint32_t s_cap_cb_count;

static void test_hrtimer_cap_cb(const bm_hal_hrtimer_t *dev, void *user) {
    (void)dev;
    (void)user;
    s_cap_cb_count++;
}

static void test_hrtimer_large_advance_callback_capped(void) {
    bm_hrtimer_stats_t stats;

    s_cap_cb_count = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_hrtimer_init(&bm_native_hrtimer0, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_callback(&bm_native_hrtimer0,
                                    test_hrtimer_cap_cb, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_start(&bm_native_hrtimer0,
                             BM_HRTIMER_MODE_PERIODIC, 1u));

    /* 单次 advance 回调派发上限 100000，超出计入 deadline miss 并推进 */
    bm_hal_hrtimer_native_advance_us(200000u);
    TEST_ASSERT_EQUAL(100000u, s_cap_cb_count);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_get_stats(&bm_native_hrtimer0, &stats));
    TEST_ASSERT_EQUAL_UINT32(100000u, stats.irq_count);
    TEST_ASSERT_TRUE(stats.deadline_miss_count >= 100000u);

    /* 到期时刻已推进到未来 */
    bm_hal_hrtimer_native_advance_us(1u);
    TEST_ASSERT_EQUAL(100001u, s_cap_cb_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hrtimer_init_and_query);
    RUN_TEST(test_hrtimer_periodic);
    RUN_TEST(test_hrtimer_oneshot);
    RUN_TEST(test_hrtimer_set_compare_dynamic);
    RUN_TEST(test_hrtimer_periodic_stop_in_callback);
    RUN_TEST(test_hrtimer_periodic_rearm_in_callback);
    RUN_TEST(test_hrtimer_large_advance_no_callback);
    RUN_TEST(test_hrtimer_large_advance_callback_capped);
    RUN_TEST(test_hrtimer_stats);
    RUN_TEST(test_hrtimer_multi_instance);
    RUN_TEST(test_hrtimer_invalid_mode);
    RUN_TEST(test_hrtimer_invalid_period);
    RUN_TEST(test_hrtimer_no_backend);
    RUN_TEST(test_hrtimer_unregister_callback);
    return UNITY_END();
}
