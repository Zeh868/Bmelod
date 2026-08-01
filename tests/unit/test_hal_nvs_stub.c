/**
 * @file test_hal_nvs_stub.c
 * @brief NVS 无后端宏守卫桩测试（load/save fail-closed 返回 BM_ERR_NOT_INIT）
 *
 * 只编 Source/hal/bm_hal_nvs.c + 本测试源：不注入
 * BM_DRV_HAS_NVS_BACKEND、不链接任何 NVS 后端，桩定义生效；
 * 有后端路径零回归由 test_persist（native 后端强符号照常链接）保证。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（P2 NVS fail-closed 宏守卫桩）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm_hal_nvs.h"
#include "bm/common/bm_types.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief 无后端桩：load/save 均返回 BM_ERR_NOT_INIT（含 NULL buf 语义）
 */
void test_hal_nvs_stub_not_init(void) {
    uint8_t buf[8] = { 0u };

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_nvs_load(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_nvs_save(buf, sizeof(buf)));
    /* NULL buf / 零长：桩不做参数校验，统一 fail-closed 返回 NOT_INIT */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_nvs_load(NULL, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_nvs_save(NULL, 0u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hal_nvs_stub_not_init);
    return UNITY_END();
}
