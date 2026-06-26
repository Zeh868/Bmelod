/**
 * @file test_wdg.c
 * @brief 软件看门狗模块超时与空指针单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm_wdg.h"
#include "bm_hal_timer.h"
#include "bm_hal_timer_native.h"
#include "bm_hal_uptime_native.h"
#include "bm_hal_wdg_native.h"
#include "bm_log.h"

#include <string.h>

void setUp(void) {
    bm_hal_uptime_native_reset();
    bm_wdg_reset();
    bm_hal_timer_native_reset_ticks();
    bm_hal_wdg_native_reset_feed_count();
    (void)bm_hal_timer_init(1000u);
}

void tearDown(void) {}

void test_wdg_register_rejects_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_wdg_register(NULL));
}

void test_wdg_register_rejects_duplicate(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("mod_a"));
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_wdg_register("mod_a"));
}

void test_wdg_register_copies_name(void) {
    char name[] = "mutable";

    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register(name));
    name[0] = 'x';
    bm_wdg_feed_module("mutable");
    bm_wdg_feed();
    TEST_ASSERT_EQUAL(1u, bm_hal_wdg_native_get_feed_count());
}

void test_wdg_register_rejects_empty_and_long_names(void) {
    char long_name[BM_CONFIG_WDG_MAX_NAME_LEN + 1u];

    memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1u] = '\0';
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_wdg_register(""));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_wdg_register(long_name));
}

void test_wdg_feed_module_null_safe(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("mod_a"));
    bm_wdg_feed_module(NULL);
    bm_wdg_feed();
    TEST_ASSERT_EQUAL(0u, bm_hal_wdg_native_get_feed_count());
}

void test_wdg_blocks_hw_feed_until_module_fed(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("alive"));

    bm_wdg_feed();
    TEST_ASSERT_EQUAL(0u, bm_hal_wdg_native_get_feed_count());

    bm_wdg_feed_module("alive");
    bm_wdg_feed();
    TEST_ASSERT_EQUAL(1u, bm_hal_wdg_native_get_feed_count());
}

void test_wdg_register_rejected_after_runtime_feed(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("alive"));

    bm_wdg_feed();
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_wdg_register("late"));
}

void test_wdg_feed_at_tick_zero(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("zero_tick"));

    bm_hal_timer_native_reset_ticks();
    bm_wdg_feed_module("zero_tick");
    bm_wdg_feed();

    TEST_ASSERT_EQUAL(1u, bm_hal_wdg_native_get_feed_count());
}

void test_wdg_feed_expired_module_blocks_hw_feed(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("stale"));

    /* 喂狗后推进 uptime 超过 BM_CONFIG_WDG_MODULE_TIMEOUT_MS（1000 ms = 1000000 µs） */
    bm_wdg_feed_module("stale");
    bm_hal_uptime_native_advance_us(1001000u);  /* 1001 ms，超出 1000 ms 超时 */
    bm_wdg_feed();

    TEST_ASSERT_EQUAL(0u, bm_hal_wdg_native_get_feed_count());
}

void test_wdg_feed_proceeds_without_hrt_timer_when_module_fed(void) {
    /*
     * #9-2a 行为变更说明：
     * 旧版使用 bm_hal_timer_get_ticks()，HRT 未初始化（freq=0）时
     * wdg_timeout_ticks 返回 BM_ERR_NOT_INIT，bm_wdg_feed 被阻断。
     *
     * 迁移至 bm_uptime_us() 后，超时计算不依赖 HRT，bm_uptime_us() 由
     * QueryPerformanceCounter/CLOCK_MONOTONIC 提供，不受 bm_hal_timer 状态影响。
     * 因此 HRT 未初始化不再阻断 hw feed——只要模块已喂且 elapsed < timeout。
     */
    bm_wdg_reset();
    bm_hal_timer_native_deinit();
    bm_hal_wdg_native_reset_feed_count();
    bm_hal_uptime_native_reset();

    TEST_ASSERT_EQUAL(BM_OK, bm_wdg_register("mod"));
    bm_wdg_feed_module("mod");
    bm_wdg_feed();

    /* 模块已喂且 elapsed ≈ 0 << timeout_us，hw feed 应成功 */
    TEST_ASSERT_EQUAL(1u, bm_hal_wdg_native_get_feed_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wdg_register_rejects_null);
    RUN_TEST(test_wdg_register_rejects_duplicate);
    RUN_TEST(test_wdg_register_copies_name);
    RUN_TEST(test_wdg_register_rejects_empty_and_long_names);
    RUN_TEST(test_wdg_feed_module_null_safe);
    RUN_TEST(test_wdg_blocks_hw_feed_until_module_fed);
    RUN_TEST(test_wdg_register_rejected_after_runtime_feed);
    RUN_TEST(test_wdg_feed_at_tick_zero);
    RUN_TEST(test_wdg_feed_expired_module_blocks_hw_feed);
    RUN_TEST(test_wdg_feed_proceeds_without_hrt_timer_when_module_fed);
    return UNITY_END();
}
