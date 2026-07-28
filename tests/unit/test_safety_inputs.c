/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_safety_inputs.c
 * @brief 安全输入组件单测：消抖、限位开关、TMC DIAG、急停输入
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增安全输入组件单测
 */
#include "unity.h"
#include "bm/component/bm_input_debounce.h"
#include "bm/component/bm_limit_switch.h"
#include "bm/component/bm_tmc_diag.h"
#include "bm/component/bm_estop_input.h"
#include "hal/bm_hal_gpio.h"
#include "bm_hal_gpio_native.h"
#include "bm_hal_uptime_native.h"
#include "hal/bm_hal_uptime.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#define LS_PIN  BM_GPIO_PIN_ENCODE(0, 0)  /* PA0 */
#define DG_PIN  BM_GPIO_PIN_ENCODE(0, 1)  /* PA1 */
#define ES_PIN  BM_GPIO_PIN_ENCODE(0, 2)  /* PA2 */

static int s_ls_event;
static int s_es_event;

void setUp(void) {
    bm_hal_gpio_native_reset();
    s_ls_event = 0;
    s_es_event = 0;
    bm_hal_uptime_native_reset();
}

void tearDown(void) {
}

static void ls_callback(void *user, uint32_t pin, int level, uint64_t ts) {
    (void)user;
    (void)pin;
    (void)ts;
    if (level) {
        s_ls_event++;
    }
}

static void es_callback(void *user, uint32_t pin, int active, uint64_t ts) {
    (void)user;
    (void)pin;
    (void)ts;
    if (active) {
        s_es_event++;
    }
}

/* -------------------------------------------------------------------------- */
/*  消抖                                                                       */
/* -------------------------------------------------------------------------- */

static void test_debounce_basic(void) {
    bm_input_debounce_t deb = {
        .config = { .stable_us = 100u },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_input_debounce_init(&deb));
    TEST_ASSERT_EQUAL(0, bm_input_debounce_filtered(&deb));

    /* 第一次翻转，未稳定 */
    TEST_ASSERT_EQUAL(0, bm_input_debounce_update(&deb, 1, 50u));
    TEST_ASSERT_EQUAL(0, bm_input_debounce_filtered(&deb));

    /* 在稳定时间内抖动回 0 */
    TEST_ASSERT_EQUAL(0, bm_input_debounce_update(&deb, 0, 80u));
    TEST_ASSERT_EQUAL(0, bm_input_debounce_filtered(&deb));

    /* 再次翻转到 1 */
    TEST_ASSERT_EQUAL(0, bm_input_debounce_update(&deb, 1, 90u));
    /* 稳定时间从 90u 开始算，到 190u 满足，产生稳定沿事件 */
    TEST_ASSERT_EQUAL(1, bm_input_debounce_update(&deb, 1, 190u));
    TEST_ASSERT_EQUAL(1, bm_input_debounce_filtered(&deb));
}

/* -------------------------------------------------------------------------- */
/*  限位开关                                                                   */
/* -------------------------------------------------------------------------- */

static void test_limit_switch_no_debounce(void) {
    bm_limit_switch_t ls = {
        .config = {
            .gpio = &bm_native_gpio,
            .pin = LS_PIN,
            .flags = BM_GPIO_EXTI_RISING,
            .stable_us = 0u,
        },
        .resources = { .event_cb = ls_callback, .user = NULL },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_limit_switch_init(&ls));
    TEST_ASSERT_EQUAL(0, bm_limit_switch_triggered(&ls));

    bm_hal_gpio_native_set_pin(LS_PIN, 1);
    bm_hal_gpio_native_fire_exti(LS_PIN);
    TEST_ASSERT_EQUAL(1, bm_limit_switch_triggered(&ls));
    TEST_ASSERT_EQUAL(1, bm_limit_switch_latched(&ls));
    TEST_ASSERT_EQUAL(1, s_ls_event);
}

static void test_limit_switch_latch_clear(void) {
    bm_limit_switch_t ls = {
        .config = {
            .gpio = &bm_native_gpio,
            .pin = LS_PIN,
            .flags = BM_GPIO_EXTI_RISING,
            .stable_us = 0u,
        },
        .resources = { .event_cb = NULL, .user = NULL },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_limit_switch_init(&ls));
    bm_hal_gpio_native_set_pin(LS_PIN, 1);
    bm_hal_gpio_native_fire_exti(LS_PIN);
    TEST_ASSERT_EQUAL(1, bm_limit_switch_latched(&ls));

    bm_limit_switch_clear_latch(&ls);
    TEST_ASSERT_EQUAL(0, bm_limit_switch_latched(&ls));
}

/* -------------------------------------------------------------------------- */
/*  TMC DIAG                                                                   */
/* -------------------------------------------------------------------------- */

static void test_tmc_diag_active_low(void) {
    bm_tmc_diag_t diag = {
        .config = {
            .gpio = &bm_native_gpio,
            .pin = DG_PIN,
            .active_low = 1,
        },
        .resources = { .diag_cb = NULL, .user = NULL },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_tmc_diag_init(&diag));
    TEST_ASSERT_EQUAL(0, bm_tmc_diag_latched(&diag));

    /* 初始上拉，高电平 -> 未激活 */
    bm_hal_gpio_native_set_pin(DG_PIN, 1);
    bm_hal_gpio_native_fire_exti(DG_PIN);
    TEST_ASSERT_EQUAL(0, bm_tmc_diag_active(&diag));
    TEST_ASSERT_EQUAL(0, bm_tmc_diag_latched(&diag));

    /* 拉低 -> 激活 */
    bm_hal_gpio_native_set_pin(DG_PIN, 0);
    bm_hal_gpio_native_fire_exti(DG_PIN);
    TEST_ASSERT_EQUAL(1, bm_tmc_diag_active(&diag));
    TEST_ASSERT_EQUAL(1, bm_tmc_diag_latched(&diag));
}

/* -------------------------------------------------------------------------- */
/*  急停输入                                                                   */
/* -------------------------------------------------------------------------- */

static void test_estop_input_debounce(void) {
    bm_estop_input_t estop = {
        .config = {
            .gpio = &bm_native_gpio,
            .pin = ES_PIN,
            .active_low = 0,
            .stable_us = 100u,
        },
        .resources = { .estop_cb = es_callback, .user = NULL },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_estop_input_init(&estop));

    /* 未稳定：高电平 50us */
    bm_hal_gpio_native_set_pin(ES_PIN, 1);
    bm_estop_input_poll(&estop);
    TEST_ASSERT_EQUAL(0, bm_estop_input_active(&estop));
    TEST_ASSERT_EQUAL(0, s_es_event);

    /* 推进虚拟时间到稳定 */
    bm_hal_uptime_native_advance_us(200u);
    bm_estop_input_poll(&estop);
    TEST_ASSERT_EQUAL(1, bm_estop_input_active(&estop));
    TEST_ASSERT_EQUAL(1, bm_estop_input_latched(&estop));
    TEST_ASSERT_EQUAL(1, s_es_event);
}

/* -------------------------------------------------------------------------- */
/*  主函数                                                                      */
/* -------------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_debounce_basic);
    RUN_TEST(test_limit_switch_no_debounce);
    RUN_TEST(test_limit_switch_latch_clear);
    RUN_TEST(test_tmc_diag_active_low);
    RUN_TEST(test_estop_input_debounce);
    return UNITY_END();
}
