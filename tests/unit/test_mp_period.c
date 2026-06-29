/**
 * @file test_mp_period.c
 * @brief 主循环周期判定纯函数 bm_mp_main_loop_period_elapsed 单元测试
 *
 * 覆盖：退化输入（period/freq 为 0）、边界（恰好到期）、以及 32 位 tick
 * 计数回绕（now < start）下单周期判定仍正确。该函数是确定性流式速率约束
 * 的核心，回绕错误会导致主循环周期被错误判定，破坏 WCET 闭包假设。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/mp/bm_mp.h"
#include "bm_module.h"

#include <stdint.h>

/*
 * 被测纯函数位于 bm_mp.c，链接该目标文件会拖入对应用模块表的引用。
 * 提供一个不被调用的最小模块表仅为满足链接（避免零长数组的非标准用法）。
 */
static int period_dummy_init(void) {
    return BM_OK;
}
BM_MODULE_DEFINE(period_dummy, 0u, period_dummy_init, NULL, NULL, NULL);
BM_MODULE_TABLE(BM_MODULE_ENTRY(period_dummy));

void setUp(void) {
}

void tearDown(void) {
}

/* period_us == 0：周期约束关闭，恒视为已到期。 */
void test_period_zero_always_elapsed(void) {
    TEST_ASSERT_EQUAL_INT(1,
                          bm_mp_main_loop_period_elapsed(0u, 0u, 1000000u, 0u));
    TEST_ASSERT_EQUAL_INT(1, bm_mp_main_loop_period_elapsed(5u, 5u, 1u, 0u));
}

/* freq == 0：无有效时基，恒视为已到期（不可阻塞主循环）。 */
void test_freq_zero_always_elapsed(void) {
    TEST_ASSERT_EQUAL_INT(1, bm_mp_main_loop_period_elapsed(0u, 0u, 0u, 1000u));
    TEST_ASSERT_EQUAL_INT(1,
                          bm_mp_main_loop_period_elapsed(10u, 50u, 0u, 1000u));
}

/* 1 MHz（1 tick = 1 us）：恰好到期与未到期的边界。 */
void test_boundary_1mhz(void) {
    const uint32_t freq = 1000000u;
    const uint32_t period_us = 1000u;

    TEST_ASSERT_EQUAL_INT(
        0,
        bm_mp_main_loop_period_elapsed(0u, 999u, freq, period_us));
    TEST_ASSERT_EQUAL_INT(
        1,
        bm_mp_main_loop_period_elapsed(0u, 1000u, freq, period_us));
    TEST_ASSERT_EQUAL_INT(
        1,
        bm_mp_main_loop_period_elapsed(0u, 1001u, freq, period_us));
}

/* 1 kHz（1 tick = 1000 us）：粗时基下的边界。 */
void test_boundary_1khz(void) {
    const uint32_t freq = 1000u;
    const uint32_t period_us = 5000u;

    TEST_ASSERT_EQUAL_INT(
        0,
        bm_mp_main_loop_period_elapsed(0u, 4u, freq, period_us));
    TEST_ASSERT_EQUAL_INT(
        1,
        bm_mp_main_loop_period_elapsed(0u, 5u, freq, period_us));
}

/* 32 位 tick 回绕：now < start 时模算差值仍给出正确的单周期判定。 */
void test_tick_wraparound(void) {
    const uint32_t freq = 1000000u; /* 1 tick = 1 us */
    const uint32_t period_us = 1000u;
    const uint32_t start = 0xFFFFFFFFu;

    /* start=0xFFFFFFFF, now=499 => 跨回绕经过 500 tick = 500 us < 1000 */
    TEST_ASSERT_EQUAL_INT(
        0,
        bm_mp_main_loop_period_elapsed(start, 499u, freq, period_us));
    /* start=0xFFFFFFFF, now=999 => 经过 1000 tick = 1000 us >= 1000 */
    TEST_ASSERT_EQUAL_INT(
        1,
        bm_mp_main_loop_period_elapsed(start, 999u, freq, period_us));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_period_zero_always_elapsed);
    RUN_TEST(test_freq_zero_always_elapsed);
    RUN_TEST(test_boundary_1mhz);
    RUN_TEST(test_boundary_1khz);
    RUN_TEST(test_tick_wraparound);
    return UNITY_END();
}
