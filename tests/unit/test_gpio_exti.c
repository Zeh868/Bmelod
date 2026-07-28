/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_gpio_exti.c
 * @brief GPIO EXTI HAL/drv 与 native_sim 后端单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 GPIO EXTI 单测
 */
#include "unity.h"
#include "hal/bm_hal_gpio.h"
#include "bm_hal_gpio_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#define TEST_PIN  BM_GPIO_PIN_ENCODE(0, 5)  /* PA5 */

static uint32_t s_cb_pin;
static int      s_cb_count;

void setUp(void) {
    bm_hal_gpio_native_reset();
    s_cb_pin = 0u;
    s_cb_count = 0;
}

void tearDown(void) {
}

static void test_exti_callback(uint32_t pin, void *user) {
    (void)user;
    s_cb_pin = pin;
    s_cb_count++;
}

static void test_gpio_exti_configure_and_fire(void) {
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_configure(&bm_native_gpio, TEST_PIN, BM_GPIO_INPUT));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_configure(&bm_native_gpio, TEST_PIN,
                                   BM_GPIO_EXTI_RISING,
                                   test_exti_callback, NULL));

    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(1, s_cb_count);
    TEST_ASSERT_EQUAL(TEST_PIN, s_cb_pin);
}

static void test_gpio_exti_enable_disable(void) {
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_configure(&bm_native_gpio, TEST_PIN,
                                   BM_GPIO_EXTI_FALLING,
                                   test_exti_callback, NULL));

    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(1, s_cb_count);

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_enable(&bm_native_gpio, TEST_PIN, 0));
    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(1, s_cb_count); /* 禁止后不再触发 */

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_enable(&bm_native_gpio, TEST_PIN, 1));
    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(2, s_cb_count);
}

static void test_gpio_exti_clear_pending(void) {
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_configure(&bm_native_gpio, TEST_PIN,
                                   BM_GPIO_EXTI_BOTH,
                                   test_exti_callback, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_clear_pending(&bm_native_gpio, TEST_PIN));
}

static void test_gpio_exti_unregister(void) {
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_configure(&bm_native_gpio, TEST_PIN,
                                   BM_GPIO_EXTI_RISING,
                                   test_exti_callback, NULL));
    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(1, s_cb_count);

    /* cb 为 NULL 时取消注册 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_exti_configure(&bm_native_gpio, TEST_PIN,
                                   BM_GPIO_EXTI_RISING,
                                   NULL, NULL));
    bm_hal_gpio_native_fire_exti(TEST_PIN);
    TEST_ASSERT_EQUAL(1, s_cb_count);
}

static void test_gpio_exti_no_backend(void) {
    const bm_hal_gpio_t no_backend = { NULL, NULL };

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_gpio_exti_configure(&no_backend, TEST_PIN,
                                   BM_GPIO_EXTI_RISING, test_exti_callback, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_gpio_exti_enable(&no_backend, TEST_PIN, 1));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_gpio_exti_clear_pending(&no_backend, TEST_PIN));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gpio_exti_configure_and_fire);
    RUN_TEST(test_gpio_exti_enable_disable);
    RUN_TEST(test_gpio_exti_clear_pending);
    RUN_TEST(test_gpio_exti_unregister);
    RUN_TEST(test_gpio_exti_no_backend);
    return UNITY_END();
}
