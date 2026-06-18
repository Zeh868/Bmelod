/**
 * @file test_module_domain.c
 * @brief 模块域过滤单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm/core/bm_module_domain.h"
#include "bm_event.h"
#include "bm_log.h"

static int g_rt_init = 0;
static int g_srt_init = 0;
static int g_common_init = 0;

static int rt_init(void) { g_rt_init++; return BM_OK; }
static int srt_init(void) { g_srt_init++; return BM_OK; }
static int common_init(void) { g_common_init++; return BM_OK; }

BM_MODULE_DEFINE_DOMAIN(rt_mod, 1, BM_DOMAIN_RT, rt_init, NULL, NULL, NULL);
BM_MODULE_DEFINE_DOMAIN(srt_mod, 2, BM_DOMAIN_SRT, srt_init, NULL, NULL, NULL);
BM_MODULE_DEFINE_DOMAIN(common_mod, 3, BM_DOMAIN_COMMON, common_init, NULL, NULL, NULL);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(rt_mod),
    BM_MODULE_ENTRY(srt_mod),
    BM_MODULE_ENTRY(common_mod));

void setUp(void) {
    g_rt_init = 0;
    g_srt_init = 0;
    g_common_init = 0;
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
#endif

int main(void) {
    UNITY_BEGIN();
#if BM_CONFIG_CPU_COUNT > 1u
    RUN_TEST(test_domain_global_lifecycle_rejected_in_multi_cpu);
#else
    RUN_TEST(test_domain_filter_rt);
    RUN_TEST(test_domain_filter_srt);
#endif
    return UNITY_END();
}
