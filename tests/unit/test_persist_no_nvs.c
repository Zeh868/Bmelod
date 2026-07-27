/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_persist_no_nvs.c
 * @brief 无 NVS capability 时的 persist RAM 与 no-op 提交测试
 *
 * 本测试独立编译 bm_persist.c，不链接任何 NVS 后端，验证一般 backend
 * 即使没有 bm_hal_nvs_load/save 符号也能完成链接并保持 RAM KV 行为。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增无 NVS capability 回归测试
 */
#include "unity.h"
#include "bm/common/bm_persist.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

/** @brief Unity 用例前置钩子；本测试无需额外准备。 */
void setUp(void) {
}

/** @brief Unity 用例后置钩子；无外部存储需要清理。 */
void tearDown(void) {
}

/**
 * @brief 无 NVS 时 init/set/get/commit 正常，重新 init 后从空表启动。
 */
static void test_persist_without_nvs_uses_ram_and_commit_is_noop(void) {
    uint32_t written = 0x12345678u;
    uint32_t readback = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_persist_init());
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_persist_set("runtime", &written, sizeof(written)));
    TEST_ASSERT_EQUAL(BM_OK, bm_persist_commit());
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_persist_get("runtime", &readback, sizeof(readback),
                                     &len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(written), len);
    TEST_ASSERT_EQUAL_UINT32(written, readback);

    TEST_ASSERT_EQUAL(BM_OK, bm_persist_init());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
                      bm_persist_get("runtime", &readback, sizeof(readback),
                                     &len));
}

/** @brief 运行无 NVS capability 回归用例。 */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_persist_without_nvs_uses_ram_and_commit_is_noop);
    return UNITY_END();
}
