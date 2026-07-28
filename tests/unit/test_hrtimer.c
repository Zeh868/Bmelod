/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_hrtimer.c
 * @brief 高精度 Timer HAL/drv 与 native_sim 后端单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增高精度 Timer 单测
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

    /* 不 start，直接 set_compare 触发一次 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 150u));

    bm_hal_hrtimer_native_advance_us(140u);
    TEST_ASSERT_EQUAL(0u, s_cb_count);

    bm_hal_hrtimer_native_advance_us(20u);
    TEST_ASSERT_EQUAL(1u, s_cb_count);

    /* 动态改比较值（运行中变速） */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_hrtimer_set_compare(&bm_native_hrtimer0, 50u));
    bm_hal_hrtimer_native_advance_us(50u);
    TEST_ASSERT_EQUAL(2u, s_cb_count);
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hrtimer_init_and_query);
    RUN_TEST(test_hrtimer_periodic);
    RUN_TEST(test_hrtimer_oneshot);
    RUN_TEST(test_hrtimer_set_compare_dynamic);
    RUN_TEST(test_hrtimer_stats);
    RUN_TEST(test_hrtimer_multi_instance);
    RUN_TEST(test_hrtimer_invalid_mode);
    RUN_TEST(test_hrtimer_invalid_period);
    RUN_TEST(test_hrtimer_no_backend);
    RUN_TEST(test_hrtimer_unregister_callback);
    return UNITY_END();
}
