/**
 * @file test_module_domain.c
 * @brief 模块域过滤单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-15
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-07-15       1.1            zeh            增加域启动失败回滚隔离用例
 */

#include "unity.h"
#include "bm/core/bm_module_domain.h"
#include "bm_event.h"
#include "bm_log.h"

static int g_rt_init = 0;
static int g_srt_init = 0;
static int g_common_init = 0;

static int g_rt_start = 0;
static int g_srt_start = 0;
static int g_common_start = 0;
static int g_srt_start_fail = 0;

static int g_rt_stop = 0;

static int rt_init(void) { g_rt_init++; return BM_OK; }
static int srt_init(void) { g_srt_init++; return BM_OK; }
static int common_init(void) { g_common_init++; return BM_OK; }

static int rt_start(void) { g_rt_start++; return BM_OK; }
static int srt_start(void) {
    if (g_srt_start_fail) {
        return BM_ERR_INVALID;
    }
    g_srt_start++;
    return BM_OK;
}
static int common_start(void) { g_common_start++; return BM_OK; }

static int rt_stop(void) { g_rt_stop++; return BM_OK; }

BM_MODULE_DEFINE_DOMAIN(rt_mod, 1, BM_DOMAIN_RT, rt_init, rt_start, rt_stop, NULL);
BM_MODULE_DEFINE_DOMAIN(srt_mod, 2, BM_DOMAIN_SRT, srt_init, srt_start, NULL, NULL);
BM_MODULE_DEFINE_DOMAIN(common_mod, 3, BM_DOMAIN_COMMON, common_init, common_start, NULL, NULL);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(rt_mod),
    BM_MODULE_ENTRY(srt_mod),
    BM_MODULE_ENTRY(common_mod));

void setUp(void) {
    g_rt_init = 0;
    g_srt_init = 0;
    g_common_init = 0;
    g_rt_start = 0;
    g_srt_start = 0;
    g_common_start = 0;
    g_srt_start_fail = 0;
    g_rt_stop = 0;
}

void tearDown(void) {
    bm_module_deinit_all();
}

#if BM_CONFIG_CPU_COUNT > 1u
void test_domain_global_lifecycle_rejected_in_multi_cpu(void) {
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_module_init_all_for_domain(BM_DOMAIN_RT));
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_module_init_all_for_domain(BM_DOMAIN_SRT));
}
#else
void test_domain_filter_rt(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all_for_domain(BM_DOMAIN_RT));
    TEST_ASSERT_EQUAL(1, g_rt_init);
    TEST_ASSERT_EQUAL(0, g_srt_init);
    TEST_ASSERT_EQUAL(1, g_common_init);
}

void test_domain_filter_srt(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all_for_domain(BM_DOMAIN_SRT));
    TEST_ASSERT_EQUAL(0, g_rt_init);
    TEST_ASSERT_EQUAL(1, g_srt_init);
    TEST_ASSERT_EQUAL(1, g_common_init);
}

/**
 * @brief 域启动失败时，回滚不得波及其他域已运行的模块
 *
 * 先启动 RT 域（RT + COMMON），再让 SRT 域启动失败；
 * 失败回滚只应停止 SRT 域内的模块（含 COMMON），RT 模块必须保持 STARTED。
 */
void test_domain_start_failure_rollback_isolated(void) {
    /* 加载全部模块 */
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_all());
    TEST_ASSERT_EQUAL(1, g_rt_init);
    TEST_ASSERT_EQUAL(1, g_srt_init);
    TEST_ASSERT_EQUAL(1, g_common_init);

    /* 启动 RT 域：RT 与 COMMON 被启动 */
    TEST_ASSERT_EQUAL(BM_OK, bm_module_start_all_for_domain(BM_DOMAIN_RT));
    TEST_ASSERT_EQUAL(1, g_rt_start);
    TEST_ASSERT_EQUAL(0, g_srt_start);
    TEST_ASSERT_EQUAL(1, g_common_start);

    /* 让 SRT 模块在启动时失败 */
    g_srt_start_fail = 1;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_module_start_all_for_domain(BM_DOMAIN_SRT));

    /* RT 必须仍处于 STARTED 状态（回滚未波及其他域） */
    TEST_ASSERT_EQUAL(1, g_rt_start);

    /* 停止 RT 域；如果 RT 未被误回滚，rt_stop 会被调用一次 */
    TEST_ASSERT_EQUAL(BM_OK, bm_module_stop_all_for_domain(BM_DOMAIN_RT));
    TEST_ASSERT_EQUAL(1, g_rt_stop);
}
#endif

int main(void) {
    UNITY_BEGIN();
#if BM_CONFIG_CPU_COUNT > 1u
    RUN_TEST(test_domain_global_lifecycle_rejected_in_multi_cpu);
#else
    RUN_TEST(test_domain_filter_rt);
    RUN_TEST(test_domain_filter_srt);
    RUN_TEST(test_domain_start_failure_rollback_isolated);
#endif
    return UNITY_END();
}
