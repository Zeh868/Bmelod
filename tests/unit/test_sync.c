/**
 * @file test_sync.c
 * @brief 同步域 configure/arm/trigger 状态机单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_sync.h"
#include "bm_config.h"
#include "bm_log.h"
#include "bm_sync_hal_native.h"

/* 占位 HAL 定时器与成员，满足 bm_sync_domain_t 字段要求 */
struct bm_hal_timer {
    int placeholder;
};

static const struct bm_hal_timer dummy_timer_storage;
static const bm_exec_t dummy_member;
static const bm_exec_t *const members[] = { &dummy_member };
static const uint32_t phases[] = { 0u };

static bm_sync_domain_t domain;
static bm_sync_domain_t domain_b;

static void init_domain(void) {
    domain.name = "test";
    domain.master_timer = &dummy_timer_storage;
    domain.members = members;
    domain.phase_ticks = phases;
    domain.member_count = 1u;
}

void setUp(void) {
    BM_LOGI("test_sync", "setUp: init domain and safe_stop");
    init_domain();
    bm_sync_safe_stop(&domain);
    bm_sync_hal_native_reset();
}

void tearDown(void) {
    bm_sync_safe_stop(&domain);
}

void test_sync_configure_arm_trigger(void) {
    /* 正常路径：配置 → 武装 → 触发 */
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_IDLE, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_TRANSITION,
                      bm_sync_hal_native_configure_observed_state());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_CONFIGURED, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_arm(&domain));
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_TRANSITION,
                      bm_sync_hal_native_arm_observed_state());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_ARMED, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_trigger(&domain));
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_TRANSITION,
                      bm_sync_hal_native_trigger_observed_state());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_CONFIGURED, bm_sync_get_state());
}

void test_sync_safe_stop_null_clears_active(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    bm_sync_safe_stop(NULL);
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_TRANSITION,
                      bm_sync_hal_native_safe_stop_observed_state());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_IDLE, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_arm(&domain));
}

void test_sync_trigger_before_arm_fails(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    BM_LOGE("test_sync", "expect NOT_INIT when trigger before arm");
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_trigger(&domain));
}

void test_sync_retrigger_requires_rearm(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_arm(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_trigger(&domain));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_trigger(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_arm(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_trigger(&domain));
}

void test_sync_rejects_excessive_member_count(void) {
    init_domain();
    domain.member_count = BM_CONFIG_MAX_SYNC_MEMBERS + 1u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_configure(&domain));
}

void test_sync_rejects_excessive_phase_ticks(void) {
    static uint32_t bad_phases[] = { BM_CONFIG_SYNC_MAX_PHASE_TICKS + 1u };

    init_domain();
    domain.phase_ticks = bad_phases;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_configure(&domain));
}

void test_sync_configure_rejects_active_domain_switch(void) {
    init_domain();
    domain_b = domain;
    domain_b.name = "test_b";

    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_arm(&domain));
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_sync_configure(&domain_b));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_trigger(&domain));
}

void test_sync_rejects_duplicate_members(void) {
    static const bm_exec_t *const duplicate_members[] = {
        &dummy_member, &dummy_member
    };
    static const uint32_t duplicate_phases[] = { 0u, 1u };

    init_domain();
    domain.members = duplicate_members;
    domain.phase_ticks = duplicate_phases;
    domain.member_count = 2u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_configure(&domain));
}

void test_sync_configure_failure_safe_stops_hal(void) {
    bm_sync_hal_native_fail_configure(1);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_configure(&domain));
    TEST_ASSERT_EQUAL(1, bm_sync_hal_native_safe_stop_count());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_IDLE, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_arm(&domain));
}

void test_sync_arm_failure_safe_stops_hal(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    bm_sync_hal_native_fail_arm(1);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_arm(&domain));
    TEST_ASSERT_EQUAL(1, bm_sync_hal_native_safe_stop_count());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_IDLE, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_trigger(&domain));
}

void test_sync_trigger_failure_safe_stops_hal(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_configure(&domain));
    TEST_ASSERT_EQUAL(BM_OK, bm_sync_arm(&domain));
    bm_sync_hal_native_fail_trigger(1);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_sync_trigger(&domain));
    TEST_ASSERT_EQUAL(1, bm_sync_hal_native_safe_stop_count());
    TEST_ASSERT_EQUAL(0, bm_sync_hal_native_triggered());
    TEST_ASSERT_EQUAL(BM_SYNC_STATE_IDLE, bm_sync_get_state());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_sync_arm(&domain));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sync_configure_arm_trigger);
    RUN_TEST(test_sync_trigger_before_arm_fails);
    RUN_TEST(test_sync_safe_stop_null_clears_active);
    RUN_TEST(test_sync_retrigger_requires_rearm);
    RUN_TEST(test_sync_rejects_excessive_member_count);
    RUN_TEST(test_sync_rejects_excessive_phase_ticks);
    RUN_TEST(test_sync_configure_rejects_active_domain_switch);
    RUN_TEST(test_sync_rejects_duplicate_members);
    RUN_TEST(test_sync_configure_failure_safe_stops_hal);
    RUN_TEST(test_sync_arm_failure_safe_stops_hal);
    RUN_TEST(test_sync_trigger_failure_safe_stops_hal);
    return UNITY_END();
}
