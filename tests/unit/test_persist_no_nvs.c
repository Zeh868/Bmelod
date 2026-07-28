/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_persist_no_nvs.c
 * @brief 无 NVS capability 时的 persist RAM 与 commit 拒绝测试
 *
 * 本测试独立编译 bm_persist.c，不链接任何 NVS 后端，验证一般 backend
 * 即使没有 bm_hal_nvs_load/save 符号也能完成链接并保持 RAM KV 行为；
 * commit 须返回 BM_ERR_NOT_SUPPORTED（不得静默成功）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增无 NVS capability 回归测试
 * 2026-07-28       1.1            zeh            commit 语义改为返回
 *                                                BM_ERR_NOT_SUPPORTED
 * 2026-07-28       1.2            zeh            文件头去掉 “no-op 提交” 旧表述
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
 * @brief 无 NVS 时 init/set/get 正常，commit 返回 BM_ERR_NOT_SUPPORTED；
 *        重新 init 后从空表启动（commit 未真正落盘）。
 */
static void test_persist_without_nvs_uses_ram_and_commit_not_supported(void) {
    uint32_t written = 0x12345678u;
    uint32_t readback = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_persist_init());
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_persist_set("runtime", &written, sizeof(written)));
    /* 无 NVS 后端：commit 必须明确返回不支持，不能静默成功 */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_persist_commit());
    /* RAM KV 仍可用 */
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_persist_get("runtime", &readback, sizeof(readback),
                                     &len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(written), len);
    TEST_ASSERT_EQUAL_UINT32(written, readback);

    /* 重新 init 后未落盘的数据丢失，从空表启动 */
    TEST_ASSERT_EQUAL(BM_OK, bm_persist_init());
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
                      bm_persist_get("runtime", &readback, sizeof(readback),
                                     &len));
}

/** @brief 运行无 NVS capability 回归用例。 */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_persist_without_nvs_uses_ram_and_commit_not_supported);
    return UNITY_END();
}
