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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_freq_points_contract);
    RUN_TEST(test_freq_set_roundtrip);
    return UNITY_END();
}
