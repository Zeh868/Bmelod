/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_hal_cpu_freq.c
 * @brief CPU 主频 HAL 接口机制单测
 *
 * 覆盖 bm_hal_cpu_freq_hz/bm_hal_cpu_freq_points/bm_hal_cpu_freq_set
 * 三个接口的**机制**契约（入参校验、点表非空、set/get 往返一致），
 * 不依赖 BM_CONFIG_CPU_FREQ_HZ 的具体数值，避免桩按 config 默认值
 * 编译时（缺省 0u）测试对具体频率值产生耦合。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "hal/bm_hal_cpu.h"
#include "bm/common/bm_types.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief freq_points 契约：非空、count>=1、NULL 入参报错
 */
static void test_freq_points_contract(void) {
    const uint32_t *pts = NULL;
    uint32_t n = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_freq_points(&pts, &n));
    TEST_ASSERT_NOT_NULL(pts);
    TEST_ASSERT_TRUE(n >= 1u);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_cpu_freq_points(NULL, &n));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_cpu_freq_points(&pts, NULL));
}

/**
 * @brief freq_set 到支持集内某点后 freq_hz 应反映之；非法点返回 BM_ERR_INVALID
 */
static void test_freq_set_roundtrip(void) {
    const uint32_t *pts = NULL;
    uint32_t n = 0u;
    (void)bm_hal_cpu_freq_points(&pts, &n);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_freq_set(pts[0]));
    TEST_ASSERT_EQUAL_UINT32(pts[0], bm_hal_cpu_freq_hz());
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_cpu_freq_set(0xFFFFFFFEu)); /* 不在支持集 */
}

/* 对账纯逻辑：一致→BM_OK；主频不符/点越集/ref不在点集→BM_ERR_INVALID */
static void test_freq_check_logic(void) {
    const uint32_t pts[] = { 80000000u, 160000000u, 240000000u };
    /* 一致：cfg 主频=240M 在点集内，cfg 点集 ⊆ port 点集 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_cpu_freq_check(240000000u, pts, 3u, 240000000u, pts, 3u));
    /* 主频不符 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_cpu_freq_check(240000000u, pts, 3u, 160000000u, pts, 3u));
    /* cfg 有个点(500M)不在 port 支持集 */
    {
        const uint32_t bad[] = { 240000000u, 500000000u };
        TEST_ASSERT_EQUAL(BM_ERR_INVALID,
            bm_hal_cpu_freq_check(240000000u, bad, 2u, 240000000u, pts, 3u));
    }
    /* ref(200M) 不在点集内 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_hal_cpu_freq_check(200000000u, pts, 3u, 200000000u, pts, 3u));
    /* cfg_freq==0（未声明）→ 跳过、BM_OK */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_cpu_freq_check(0u, NULL, 0u, 240000000u, pts, 3u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_freq_points_contract);
    RUN_TEST(test_freq_set_roundtrip);
    RUN_TEST(test_freq_check_logic);
    return UNITY_END();
}
