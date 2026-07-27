/**
 * @file test_health_monitor.c
 * @brief health_monitor 组件单元测试
 *
 * 覆盖配置校验、多源上报聚合、故障清除与锁存保留、reset 语义、
 * 遥测变更发布与 NULL 边界。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            初始版本
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/health_monitor.h"
#include "bm/common/bm_types.h"

#include <string.h>

#define SRC_A 1u
#define SRC_B 2u
#define SRC_UNREGISTERED 99u

/* ---------- 遥测回调统计 ---------- */
static uint32_t g_tel_count;
static bm_health_monitor_telemetry_t g_last_tel;

static void tel_cb(void *user,
                   const bm_health_monitor_telemetry_t *telemetry) {
    (void)user;
    g_tel_count++;
    g_last_tel = *telemetry;
}

/* ---------- 标准 monitor 构造辅助 ---------- */
static bm_health_monitor_source_t g_sources[2];

static void make_monitor(bm_health_monitor_t *mon) {
    memset(mon, 0, sizeof(*mon));
    memset(g_sources, 0, sizeof(g_sources));
    g_sources[0].source_id = SRC_A;
    g_sources[1].source_id = SRC_B;
    mon->config.sources = g_sources;
    mon->config.source_count = 2u;
    mon->resources.publish_telemetry = tel_cb;
    mon->resources.publish_telemetry_user = NULL;
}

void setUp(void) {
    g_tel_count = 0u;
    memset(&g_last_tel, 0, sizeof(g_last_tel));
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

/**
 * @brief 正常 init：源表运行字段清零，source_id 保留
 */
void test_health_monitor_init_ok(void) {
    bm_health_monitor_t mon;
    make_monitor(&mon);
    g_sources[0].active_code = BM_FAULT_SENSOR_FROZEN;
    g_sources[0].report_count = 7u;

    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));
    TEST_ASSERT_EQUAL_HEX16(SRC_A, mon.config.sources[0].source_id);
    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_NONE, mon.config.sources[0].active_code);
    TEST_ASSERT_EQUAL_UINT32(0u, mon.config.sources[0].report_count);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_NONE,
                            mon.config.sources[0].worst_severity);
}

/**
 * @brief 单源上报：活动状态、worst_severity 升级、report_count 递增
 */
void test_health_monitor_report_updates_source(void) {
    bm_health_monitor_t mon;
    bm_health_monitor_telemetry_t snap;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_OVER_RANGE,
                                 BM_FAULT_SEVERITY_WARNING));
    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_SENSOR_OVER_RANGE,
                            mon.config.sources[0].active_code);
    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_SENSOR_OVER_RANGE,
                            mon.config.sources[0].latched_code);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_WARNING,
                            mon.config.sources[0].worst_severity);
    TEST_ASSERT_EQUAL_UINT32(1u, mon.config.sources[0].report_count);

    /* 严重度升级刷新 worst_severity */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_OVER_RANGE,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_ERROR,
                            mon.config.sources[0].worst_severity);
    TEST_ASSERT_EQUAL_UINT32(2u, mon.config.sources[0].report_count);

    /* 严重度降级不回退 worst_severity */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_OVER_RANGE,
                                 BM_FAULT_SEVERITY_INFO));
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_ERROR,
                            mon.config.sources[0].worst_severity);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_INFO,
                            mon.config.sources[0].severity);

    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_system_health(&mon, &snap));
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_INFO, snap.worst_severity);
    TEST_ASSERT_EQUAL_HEX16(SRC_A, snap.active_source_id);
}

/**
 * @brief 多源聚合：worst 取最大严重度，sources_active 计数正确
 */
void test_health_monitor_multi_source_aggregation(void) {
    bm_health_monitor_t mon;
    bm_health_monitor_telemetry_t snap;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_WARNING));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_B, BM_FAULT_SENSOR_NAN,
                                 BM_FAULT_SEVERITY_CRITICAL));

    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_system_health(&mon, &snap));
    TEST_ASSERT_EQUAL_UINT8(2u, snap.sources_active);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_CRITICAL, snap.worst_severity);
    TEST_ASSERT_EQUAL_HEX16(SRC_B, snap.active_source_id);
    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_SENSOR_NAN, snap.active_code);
}

/**
 * @brief BM_FAULT_NONE 清除活动故障：锁存与 worst_severity 保留
 */
void test_health_monitor_clear_keeps_latched(void) {
    bm_health_monitor_t mon;
    bm_health_monitor_telemetry_t snap;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_NONE,
                                 BM_FAULT_SEVERITY_NONE));

    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_NONE, mon.config.sources[0].active_code);
    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_SENSOR_FROZEN,
                            mon.config.sources[0].latched_code);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_ERROR,
                            mon.config.sources[0].worst_severity);

    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_system_health(&mon, &snap));
    TEST_ASSERT_EQUAL_UINT8(0u, snap.sources_active);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_NONE, snap.worst_severity);
    TEST_ASSERT_EQUAL_UINT16(1u, snap.sources_latched);
}

/**
 * @brief reset 清空锁存与 worst_severity
 */
void test_health_monitor_reset_clears_latch(void) {
    bm_health_monitor_t mon;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    bm_health_monitor_reset(&mon);

    TEST_ASSERT_EQUAL_HEX16(BM_FAULT_NONE, mon.config.sources[0].latched_code);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_NONE,
                            mon.config.sources[0].worst_severity);
    TEST_ASSERT_EQUAL_UINT32(0u, mon.config.sources[0].report_count);
}

/**
 * @brief 未注册 source_id 返回 BM_ERR_INVALID，不影响已有状态
 */
void test_health_monitor_unregistered_source_rejected(void) {
    bm_health_monitor_t mon;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_health_monitor_report(&mon, SRC_UNREGISTERED,
                                 BM_FAULT_GENERIC_UNKNOWN,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL_UINT32(0u, g_tel_count);
}

/**
 * @brief 遥测仅在系统级快照变化时发布，且携带聚合结果
 */
void test_health_monitor_telemetry_published_on_change(void) {
    bm_health_monitor_t mon;
    make_monitor(&mon);
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    /* 首次上报：健康 → 活动故障，快照变化，发布 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL_UINT32(1u, g_tel_count);
    TEST_ASSERT_EQUAL_HEX32(BM_HEALTH_MONITOR_TEL_VALID, g_last_tel.status);
    TEST_ASSERT_EQUAL_UINT8(1u, g_last_tel.sources_active);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_ERROR, g_last_tel.worst_severity);
    TEST_ASSERT_EQUAL_UINT32(1u, g_last_tel.sequence);

    /* 同一源重复上报相同故障与严重度：快照不变，不重复发布 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL_UINT32(1u, g_tel_count);

    /* 第二个源出现更重故障：快照变化，序列号递增 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_B, BM_FAULT_SENSOR_NAN,
                                 BM_FAULT_SEVERITY_CRITICAL));
    TEST_ASSERT_EQUAL_UINT32(2u, g_tel_count);
    TEST_ASSERT_EQUAL_UINT32(2u, g_last_tel.sequence);
    TEST_ASSERT_EQUAL_UINT8(2u, g_last_tel.sources_active);
    TEST_ASSERT_EQUAL_HEX16(SRC_B, g_last_tel.active_source_id);

    /* 清除最重源：A 仍存在，worst 回落到 A 的 ERROR */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_B, BM_FAULT_NONE,
                                 BM_FAULT_SEVERITY_NONE));
    TEST_ASSERT_EQUAL_UINT32(3u, g_tel_count);
    TEST_ASSERT_EQUAL_UINT8(BM_FAULT_SEVERITY_ERROR, g_last_tel.worst_severity);
    TEST_ASSERT_EQUAL_HEX16(SRC_A, g_last_tel.active_source_id);
}

/**
 * @brief publish 回调为 NULL 时不发布也不崩溃
 */
void test_health_monitor_publish_null_safe(void) {
    bm_health_monitor_t mon;
    make_monitor(&mon);
    mon.resources.publish_telemetry = NULL;
    TEST_ASSERT_EQUAL(BM_OK, bm_health_monitor_init(&mon));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_health_monitor_report(&mon, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    TEST_ASSERT_EQUAL_UINT32(0u, g_tel_count);
}

/**
 * @brief init/validate 的 NULL 与非法配置边界
 */
void test_health_monitor_validate_and_init_boundaries(void) {
    bm_health_monitor_t mon;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_health_monitor_validate_config(NULL));

    make_monitor(&mon);
    mon.config.sources = NULL;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_health_monitor_validate_config(&mon.config));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_health_monitor_init(&mon));

    make_monitor(&mon);
    mon.config.source_count = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_health_monitor_validate_config(&mon.config));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_health_monitor_init(NULL));
}

/**
 * @brief report/reset/system_health 传 NULL 不崩溃
 */
void test_health_monitor_null_safe_ops(void) {
    bm_health_monitor_telemetry_t snap;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_health_monitor_report(NULL, SRC_A, BM_FAULT_SENSOR_FROZEN,
                                 BM_FAULT_SEVERITY_ERROR));
    bm_health_monitor_reset(NULL);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_health_monitor_system_health(NULL, &snap));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_health_monitor_system_health(
                          (const bm_health_monitor_t *)0, NULL));
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_health_monitor_init_ok);
    RUN_TEST(test_health_monitor_report_updates_source);
    RUN_TEST(test_health_monitor_multi_source_aggregation);
    RUN_TEST(test_health_monitor_clear_keeps_latched);
    RUN_TEST(test_health_monitor_reset_clears_latch);
    RUN_TEST(test_health_monitor_unregistered_source_rejected);
    RUN_TEST(test_health_monitor_telemetry_published_on_change);
    RUN_TEST(test_health_monitor_publish_null_safe);
    RUN_TEST(test_health_monitor_validate_and_init_boundaries);
    RUN_TEST(test_health_monitor_null_safe_ops);
    return UNITY_END();
}
