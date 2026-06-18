/**
 * @file test_module.c
 * @brief 模块表 init/start/stop/deinit 生命周期与优先级顺序单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_event.h"
#include "bm_module.h"
#include "bm_log.h"

static int g_init_count = 0;
static int g_start_count = 0;
static int g_stop_count = 0;
static int g_deinit_count = 0;
static int g_fail_mod_b_init = 0;
static int g_fail_mod_b_start = 0;
static int g_fail_mod_a_stop = 0;
static int g_no_init_start_count = 0;
static int g_reenter_start = 0;
static int g_reenter_stop = 0;
static int g_nested_start_rc = BM_OK;
static int g_nested_stop_rc = BM_OK;

#define EVENT_BOOT_GUARD 3u

static int mod_a_init(void) { g_init_count++; return BM_OK; }
static int mod_a_start(void) { g_start_count++; return BM_OK; }
static int mod_a_stop(void) {
    g_stop_count++;
    if (g_reenter_stop) {
        g_nested_stop_rc = bm_module_stop_all();
    }
    return g_fail_mod_a_stop ? BM_ERR_BUSY : BM_OK;
}
static int mod_a_deinit(void) { g_deinit_count++; return BM_OK; }
static int mod_no_init_start(void) {
    g_no_init_start_count++;
    if (g_reenter_start) {
        g_nested_start_rc = bm_module_start_all();
    }
    return BM_OK;
}

static int mod_b_init(void) {
    g_init_count++;
    return g_fail_mod_b_init ? BM_ERR_INVALID : BM_OK;
}
static int mod_b_start(void) {
    g_start_count++;
    return g_fail_mod_b_start ? BM_ERR_INVALID : BM_OK;
}

BM_MODULE_DEFINE(mod_b, 5, mod_b_init, mod_b_start, NULL, NULL);

BM_MODULE_DEFINE(mod_a, 1, mod_a_init, mod_a_start, mod_a_stop, mod_a_deinit);
BM_MODULE_DEFINE(mod_no_init, 0, NULL, mod_no_init_start, NULL, NULL);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(mod_b),
    BM_MODULE_ENTRY(mod_a),
    BM_MODULE_ENTRY(mod_no_init));

void setUp(void) {
    BM_LOGI("test_mod", "setUp: reset lifecycle counters");
    g_init_count = 0;
    g_start_count = 0;
    g_stop_count = 0;
    g_deinit_count = 0;
    g_fail_mod_b_init = 0;
    g_fail_mod_b_start = 0;
    g_fail_mod_a_stop = 0;
    g_no_init_start_count = 0;
    g_reenter_start = 0;
    g_reenter_stop = 0;
    g_nested_start_rc = BM_OK;
    g_nested_stop_rc = BM_OK;
}
void tearDown(void) {
    bm_module_deinit_all();
}

#if BM_CONFIG_CPU_COUNT > 1u
void test_module_global_lifecycle_rejected_in_multi_cpu(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_init_all());
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_start_all());
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_stop_all());
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_deinit_all());
}
#else
void test_module_init_failure_rollback(void) {
    g_fail_mod_b_init = 1;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_init_all());
    TEST_ASSERT_EQUAL(2, g_init_count);
    TEST_ASSERT_EQUAL(1, g_deinit_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_module_deinit_all());
    TEST_ASSERT_EQUAL(1, g_deinit_count);
}

void test_module_start_failure_rollback(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    g_fail_mod_b_start = 1;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_start_all());
    TEST_ASSERT_EQUAL(2, g_start_count);
    TEST_ASSERT_EQUAL(1, g_stop_count);
}

void test_module_lifecycle_order(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    TEST_ASSERT_EQUAL(2, g_init_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all());
    TEST_ASSERT_EQUAL(2, g_start_count);
    TEST_ASSERT_EQUAL(1, g_no_init_start_count);

    TEST_ASSERT_EQUAL(BM_OK, bm_module_stop_all());
    TEST_ASSERT_EQUAL(1, g_stop_count); /* 仅 mod_a 注册了 stop */

    TEST_ASSERT_EQUAL(BM_OK, bm_module_deinit_all());
}

void test_module_can_restart_after_stop(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all());
    TEST_ASSERT_EQUAL(BM_OK, bm_module_stop_all());
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all());
    TEST_ASSERT_EQUAL(4, g_start_count);
    TEST_ASSERT_EQUAL(2, g_no_init_start_count);
}

void test_module_repeated_boot_preserves_event_bus(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_boot());
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_event_register_type(EVENT_BOOT_GUARD, "boot_guard"));

    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_module_boot());
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_event_register_type(EVENT_BOOT_GUARD, "boot_guard"));
}

void test_module_rejects_lifecycle_reentrancy(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    g_reenter_start = 1;
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all());
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, g_nested_start_rc);

    g_reenter_stop = 1;
    TEST_ASSERT_EQUAL(BM_OK, bm_module_stop_all());
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, g_nested_stop_rc);
}

void test_module_deinit_refuses_running_module_when_stop_fails(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all());

    g_fail_mod_a_stop = 1;
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_module_deinit_all());
    TEST_ASSERT_EQUAL(0, g_deinit_count);
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_module_start_all());

    g_fail_mod_a_stop = 0;
    TEST_ASSERT_EQUAL(BM_OK, bm_module_deinit_all());
    TEST_ASSERT_EQUAL(1, g_deinit_count);
}
#endif

int main(void) {
    UNITY_BEGIN();
#if BM_CONFIG_CPU_COUNT > 1u
    RUN_TEST(test_module_global_lifecycle_rejected_in_multi_cpu);
#else
    RUN_TEST(test_module_init_failure_rollback);
    RUN_TEST(test_module_start_failure_rollback);
    RUN_TEST(test_module_lifecycle_order);
    RUN_TEST(test_module_can_restart_after_stop);
    RUN_TEST(test_module_repeated_boot_preserves_event_bus);
    RUN_TEST(test_module_rejects_lifecycle_reentrancy);
    RUN_TEST(test_module_deinit_refuses_running_module_when_stop_fails);
#endif
    return UNITY_END();
}
