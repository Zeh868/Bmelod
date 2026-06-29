/**
 * @file test_console.c
 * @brief Console HAL 分通道与多核 bootstrap 策略单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 *
 */
#include "unity.h"
#include "hal/bm_hal_console.h"
#include "hal/bm_hal_cpu.h"
#include "bm_hal_cpu_mp_native.h"

#include <string.h>

void setUp(void) {
    bm_hal_cpu_init();
    (void)bm_hal_cpu_native_set_id(0u);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_console_init());
}

void tearDown(void) {
}

void test_console_log_write_stdio(void) {
#if BM_CONFIG_CONSOLE_LOG_BACKEND == BM_CONSOLE_BACKEND_STDIO
    const char msg[] = "console-log\n";
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_console_write(BM_CONSOLE_LOG,
                                           (const uint8_t *)msg,
                                           sizeof(msg) - 1u));
#else
    TEST_PASS();
#endif
}

void test_console_cli_bootstrap_only_mp(void) {
#if BM_CONFIG_CPU_COUNT > 1u && BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY && \
    BM_CONFIG_CONSOLE_CLI_BACKEND != BM_CONSOLE_BACKEND_NONE
    uint8_t byte;
    (void)bm_hal_cpu_native_set_id(1u);
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_hal_console_write(BM_CONSOLE_CLI,
                                           (const uint8_t *)"x", 1u));
    TEST_ASSERT_EQUAL(0u, bm_hal_console_read(BM_CONSOLE_CLI, &byte, 1u));
    (void)bm_hal_cpu_native_set_id(0u);
#else
    TEST_PASS();
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_console_log_write_stdio);
    RUN_TEST(test_console_cli_bootstrap_only_mp);
    return UNITY_END();
}
